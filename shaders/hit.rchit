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
#include "sampling.glsl"
#include "light.glsl"
#include "bsdf.glsl"
#include "hit.glsl"

layout(binding = 0, set = 0) uniform accelerationStructureEXT topLevelAS;
layout(binding = 4, set = 0, scalar) uniform PathTracingProperties{
    uint sampleCount, maxRayDepth;
    float skyboxStrength;
} pathTracing;

layout(location = 0) rayPayloadInEXT RayPayload payloadIn;
layout(location = 3) rayPayloadEXT EmissivePDFPayload emissivePDFPayload;
hitAttributeEXT vec2 attribs;

HitInfo unpackTriangle(uint idx, vec3 weights) {
    GeometryInfo geometryInfo = geometryInfos[gl_InstanceCustomIndexEXT];
    Material material = materials[geometryInfo.materialIdx];
    Indices indexBuffer = Indices(geometryInfo.indexBufferAddress);
    Vertices vertexBuffer = Vertices(geometryInfo.vertexBufferAddress);
    vec3 view = -gl_WorldRayDirectionEXT;

    HitInfo hitInfo;
    hitInfo.t = gl_HitTEXT;

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

    mat3 rotation = transpose(mat3(gl_WorldToObjectEXT));
    hitInfo.normal = normalize(rotation * hitInfo.normal);
    if (hitInfo.tangent != vec3(0.0)) {
        hitInfo.tangent = normalize(rotation * hitInfo.tangent);
        hitInfo.bitangent = cross(hitInfo.normal, hitInfo.tangent) * tangentSign;
        if (material.normalTexIdx != -1)
            hitInfo.normal = normalize(mat3(hitInfo.tangent, hitInfo.bitangent, hitInfo.normal) * normalize(textureGet(material.normalTexIdx, uv).rgb * 2.0 - 1.0));
        // Create ONB
        hitInfo.tangent = normalize(hitInfo.tangent - dot(hitInfo.normal, hitInfo.tangent) * hitInfo.normal); // re-orthogonalise
        hitInfo.bitangent = cross(hitInfo.normal, hitInfo.tangent) * tangentSign;
    } else {
        branchlessONB(hitInfo.normal, hitInfo.tangent, hitInfo.bitangent);
    }
    hitInfo.frontFace = dot(hitInfo.normal, view) >= 0.0;
    hitInfo.normal = hitInfo.frontFace ? hitInfo.normal : -hitInfo.normal;

    hitInfo.hitMat.baseColour = material.baseColourFactor.rgb;
    if (material.baseColourTexIdx != -1)
        hitInfo.hitMat.baseColour *= textureGet(material.baseColourTexIdx, uv).rgb;
    
    hitInfo.hitMat.emissiveColour = material.emissiveFactor;
    if (material.emissiveTexIdx != -1)
        hitInfo.hitMat.emissiveColour *= textureGet(material.emissiveTexIdx, uv).rgb;

    hitInfo.hitMat.transmissionFactor = material.transmissionFactor;
    if (material.transmissionTexIdx != -1)
        hitInfo.hitMat.transmissionFactor *= textureGet(material.transmissionTexIdx, uv).r;

    hitInfo.hitMat.metallic = material.metallicFactor;
    hitInfo.hitMat.alpha = vec2(material.roughnessFactor);
    if (material.metallicRoughnessTexIdx != -1) {
        vec2 metallicRoughness = textureGet(material.metallicRoughnessTexIdx, uv).bg;
        hitInfo.hitMat.metallic *= metallicRoughness.x;
        hitInfo.hitMat.alpha *= metallicRoughness.y;
    }
    hitInfo.hitMat.alpha *= hitInfo.hitMat.alpha;
    hitInfo.hitMat.alpha = max(vec2(0.001), hitInfo.hitMat.alpha);

    hitInfo.hitMat.ior = material.ior;
    hitInfo.hitMat.thin = material.thicknessFactor == 0;
    hitInfo.hitMat.attenuationCoefficient = material.attenuationCoefficient;
    hitInfo.hitMat.dispersion = material.dispersion;

    float anisotropyRotation = material.anisotropyRotation;
    float anisotropyStrength = material.anisotropyStrength;
    if (material.anisotropyTexIdx != -1) {
        vec3 anisotropy = textureGet(material.anisotropyTexIdx, uv).xyz;
        anisotropyRotation += atan(anisotropy.y, anisotropy.x);
        anisotropyStrength *= anisotropy.z;
    }
    hitInfo.hitMat.alpha.x = mix(hitInfo.hitMat.alpha.x, 1.0, anisotropyStrength * anisotropyStrength);
    hitInfo.hitMat.anisotropyDirection = vec2(cos(anisotropyRotation), sin(anisotropyRotation));
    return hitInfo;
}

void main() {
    const vec3 barycentricCoords = vec3(1.0f - attribs.x - attribs.y, attribs.x, attribs.y);
    payloadIn.hitInfo = unpackTriangle(gl_PrimitiveID, barycentricCoords);
}