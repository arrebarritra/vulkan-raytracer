#version 460
#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_debug_printf : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int32 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require

#include "payload.glsl"
#include "geometry.glsl"
#include "material.glsl"
#include "texture.glsl"
#include "constants.glsl"
#include "random.glsl"
#include "bsdf.glsl"

layout(binding = 0, set = 0) uniform accelerationStructureEXT topLevelAS;
layout(binding = 4, set = 0, scalar) uniform PathTracingProperties{
    uint sampleCount, maxRayDepth;
} pathTracing;

layout(location = 0) rayPayloadInEXT RayPayload payloadIn;
hitAttributeEXT vec2 attribs;

struct HitInfo {
    vec3 pos, normal, tangent, bitangent, baseColour;
    vec3 emissiveColour;
    float roughness, metallic;
    float transmissionFactor;
};

#include "light.glsl"

HitInfo unpackTriangle(uint idx, vec3 weights, out Material material) {
    GeometryInfo geometryInfo = geometryInfos[gl_InstanceCustomIndexEXT];
    material = materials[geometryInfo.materialIdx];
    Indices indexBuffer = Indices(geometryInfo.indexBufferAddress);
    Vertices vertexBuffer = Vertices(geometryInfo.vertexBufferAddress);
    vec3 view = -gl_WorldRayDirectionEXT;

    HitInfo hitInfo;
    hitInfo.pos = vec3(0.0);
    hitInfo.normal = vec3(0.0);
    hitInfo.tangent = vec3(0.0);
    hitInfo.bitangent = vec3(0.0);
    float tangentSign = vertexBuffer.vertices[indexBuffer.indices[3 * idx]].tangent.w;
    vec2 uv = vec2(0.0);
    for (int i = 0; i < 3; i++) {
        uint index = indexBuffer.indices[3 * idx + i];
        Vertex vertex = vertexBuffer.vertices[index];

        hitInfo.pos += vertex.pos * weights[i];
        hitInfo.normal += vertex.normal * weights[i];
        hitInfo.tangent += vertex.tangent.xyz * weights[i];
        uv += vertex.uv * weights[i];
    }

    hitInfo.pos = vec3(gl_ObjectToWorldEXT * vec4(hitInfo.pos, 1.0));

    hitInfo.baseColour = material.baseColourFactor.rgb;
    if (material.baseColourTexIdx != -1)
        hitInfo.baseColour *= textureGet(material.baseColourTexIdx, uv).rgb;
    
    hitInfo.emissiveColour = material.emissiveFactor * material.emissiveStrength;
    if (material.emissiveTexIdx != -1)
        hitInfo.emissiveColour *= textureGet(material.emissiveTexIdx, uv).rgb;

    hitInfo.transmissionFactor = material.transmissionFactor;
    if (material.transmissionTexIdx != -1)
        hitInfo.transmissionFactor *= textureGet(material.transmissionTexIdx, uv).r;

    mat3 rotation = transpose(mat3(gl_WorldToObjectEXT));
    hitInfo.normal = normalize(rotation * hitInfo.normal);
    if (hitInfo.tangent != vec3(0.0)) {
        hitInfo.tangent = normalize(rotation * hitInfo.tangent);
        hitInfo.tangent = normalize(hitInfo.tangent - dot(hitInfo.normal, hitInfo.tangent) * hitInfo.normal); // re-orthogonalise
        hitInfo.bitangent = cross(hitInfo.normal, hitInfo.tangent) * tangentSign;
        hitInfo.normal = mat3(hitInfo.tangent, hitInfo.bitangent, hitInfo.normal) * normalize(textureGet(material.normalTexIdx, uv).rgb * 2.0 - 1.0);
        // orthogonalize against normal mapped normal
        hitInfo.tangent = normalize(hitInfo.tangent - dot(hitInfo.normal, hitInfo.tangent) * hitInfo.normal); // re-orthogonalise
        hitInfo.bitangent = cross(hitInfo.normal, hitInfo.tangent) * tangentSign;
    } else {
        branchlessONB(hitInfo.normal, hitInfo.tangent, hitInfo.bitangent);
    }

    hitInfo.normal *= dot(hitInfo.normal, view) >= 0.0 ? 1.0 : -1.0;

    hitInfo.roughness = material.roughnessFactor;
    hitInfo.metallic = material.metallicFactor;
    if (material.metallicRoughnessTexIdx != -1) {
        vec2 metallicRoughness = textureGet(material.metallicRoughnessTexIdx, uv).bg;
        hitInfo.metallic *= metallicRoughness.x;
        hitInfo.roughness *= metallicRoughness.y;
    }

    return hitInfo;
}

void main() {
    uint seed = tea(gl_LaunchIDEXT.y * gl_LaunchIDEXT.x + gl_LaunchIDEXT.y * gl_LaunchIDEXT.x + gl_LaunchIDEXT.x, pathTracing.sampleCount + 1);

    const vec3 barycentricCoords = vec3(1.0f - attribs.x - attribs.y, attribs.x, attribs.y);
    vec3 view = -gl_WorldRayDirectionEXT;
    Material mat;
    HitInfo hitInfo = unpackTriangle(gl_PrimitiveID, barycentricCoords, mat);

    payloadIn.lightSample = vec3(0.0);
    vec3 lightDir;
    float pdfLightSample = 0.0;
    bool analyticLight = rnd(seed) < 0.5;
    uint numAnalyticLights = numPointLights + numDirectionalLights;
    if (hitInfo.emissiveColour == vec3(0.0)) {
        // Calculate direct light contribution from point and directional lights, and emissive surfaces
        vec3 shadowRayOrigin = hitInfo.pos;

        if (analyticLight && numAnalyticLights > 0) {
            payloadIn.lightSample = sampleAnalyticLight(seed, shadowRayOrigin, hitInfo.normal, lightDir);
            pdfLightSample = numAnalyticLights;
        } else if(numEmissiveTriangles > 0) {
            payloadIn.lightSample = sampleEmissiveTriangle(seed, shadowRayOrigin, hitInfo.normal, lightDir, pdfLightSample);
        }
    }
    pdfLightSample /= max(1.0, float(numAnalyticLights > 0) + float(numEmissiveTriangles > 0));

    // Calculate Monte Carlo estimator term for light sampling and material sampling, sample new direction
    
    // Calculate tangent space vectors for sampling methods
    mat3 TBN = mat3(hitInfo.tangent, hitInfo.bitangent, hitInfo.normal);
    mat3 invTBN = transpose(TBN);
    vec3 tView = invTBN * view;

    // Evaluate material sampling
    payloadIn.direction = TBN * sampleMaterial(seed, hitInfo.baseColour, hitInfo.metallic, hitInfo.roughness, hitInfo.transmissionFactor, mat.ior, tView, payloadIn.reflectivity);

    // Evaluate direct light sampling
    {
        vec3 bsdf = materialBSDF(hitInfo.baseColour, hitInfo.metallic, hitInfo.roughness, hitInfo.transmissionFactor, mat.ior, tView, invTBN * lightDir);
        pdfLightSample += materialPDF(hitInfo.baseColour, hitInfo.metallic, hitInfo.roughness, hitInfo.transmissionFactor, mat.ior, tView, invTBN * lightDir);
        pdfLightSample /= 2.0;

        payloadIn.lightSample *= bsdf == vec3(0.0) ? vec3(0.0) : bsdf / pdfLightSample * dot(hitInfo.normal, lightDir);
    }

    payloadIn.origin = hitInfo.pos + (dot(payloadIn.direction, hitInfo.normal) >= 0.0 ? 1.0 : -1.0) * BIAS * hitInfo.normal;
    payloadIn.emittedLight = hitInfo.emissiveColour;
    if (hitInfo.emissiveColour != vec3(0.0)) payloadIn.scatter = false;
    if (pathTracing.sampleCount == 0u && payloadIn.emittedLight == vec3(0.0)) {
        payloadIn.emittedLight = hitInfo.baseColour;
    }
}