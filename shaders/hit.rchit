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
#include "light.glsl"
#include "texture.glsl"
#include "constants.glsl"
#include "random.glsl"

layout(binding = 0, set = 0) uniform accelerationStructureEXT topLevelAS;
layout(binding = 4, set = 0, scalar) uniform PathTracingProperties{
    uint sampleCount, maxRayDepth;
} pathTracing;

layout(location = 0) rayPayloadInEXT PathTracingPayload payloadIn;
layout(location = 1) rayPayloadEXT ShadowPayload shadowRayPayload;
layout(location = 2) rayPayloadEXT EmissivePayload emissiveRayPayload;
hitAttributeEXT vec2 attribs;

struct HitInfo {
    vec3 pos, normal, baseColour;
    vec3 emissiveColour;
    float roughnessFactor, metallicFactor;
    float transmissionFactor;
};

HitInfo unpackTriangle(uint idx, vec3 weights, out Material material) {
    GeometryInfo geometryInfo = geometryInfos[gl_InstanceCustomIndexEXT];
    material = materials[geometryInfo.materialIdx];
    Indices indexBuffer = Indices(geometryInfo.indexBufferAddress);
    Vertices vertexBuffer = Vertices(geometryInfo.vertexBufferAddress);
    
    HitInfo hitInfo;
    hitInfo.pos = vec3(0.0);
    hitInfo.normal = vec3(0.0);
    vec3 tangent = vec3(0.0);
    vec3 bitangent = vec3(0.0);
    vec2 uv0 = vec2(0.0);
    vec2 uv1 = vec2(0.0);
    for (int i = 0; i < 3; i++) {
        uint index = indexBuffer.indices[3 * idx + i];
        Vertex vertex = vertexBuffer.vertices[index];

        hitInfo.pos += vertex.pos * weights[i];
        hitInfo.normal += vertex.normal * weights[i];
        if (material.normalTexIdx != -1) {
            tangent += vertex.tangent.xyz * weights[i];
            bitangent += cross(hitInfo.normal, tangent) * vertex.tangent.w * weights[i];
        }
        uv0 += vertex.uv0 * weights[i];
        uv1 += vertex.uv1 * weights[i];
    }

    hitInfo.pos = vec3(gl_ObjectToWorldEXT * vec4(hitInfo.pos, 1.0));
    mat3 rotation = mat3(gl_WorldToObjectEXT);
    hitInfo.normal = rotation * hitInfo.normal;
    if (material.normalTexIdx != -1) {
        tangent = rotation * tangent;
        bitangent = rotation * bitangent;
    }

    hitInfo.baseColour = material.baseColourFactor.rgb;
    if (material.baseColourTexIdx != -1)
        hitInfo.baseColour *= textureGet(material.baseColourTexIdx, uv0).rgb;
    
    hitInfo.emissiveColour = material.emissiveFactor * material.emissiveStrength;
    if (material.emissiveTexIdx != -1)
        hitInfo.emissiveColour *= textureGet(material.emissiveTexIdx, uv0).rgb;

    hitInfo.transmissionFactor = material.transmissionFactor;
    if (material.transmissionTexIdx != -1)
        hitInfo.transmissionFactor *= textureGet(material.transmissionTexIdx, uv0).r;

    hitInfo.normal = normalize(hitInfo.normal);
    if (material.normalTexIdx != -1) {
        tangent = normalize(tangent);
        bitangent = normalize(bitangent);
        hitInfo.normal = mat3(tangent, bitangent, hitInfo.normal) * normalize(textureGet(material.normalTexIdx, uv0).rgb * 2.0 - 1.0);
    }
    hitInfo.normal = sign(dot(payloadIn.rayOrigin - hitInfo.pos, hitInfo.normal)) * hitInfo.normal;
    
    hitInfo.roughnessFactor = material.roughnessFactor;
    hitInfo.metallicFactor = material.metallicFactor;
    if (material.metallicRoughnessTexIdx != -1) {
        vec2 metallicRoughness = textureGet(material.metallicRoughnessTexIdx, uv0).bg;
        hitInfo.metallicFactor = metallicRoughness.x;
        hitInfo.roughnessFactor = metallicRoughness.y;
    }

    return hitInfo;
}

void main() {
    uint seed = tea(gl_PrimitiveID * gl_LaunchIDEXT.y * gl_LaunchIDEXT.x + gl_LaunchIDEXT.y * gl_LaunchIDEXT.x + gl_LaunchIDEXT.x, pathTracing.sampleCount);

    const vec3 barycentricCoords = vec3(1.0f - attribs.x - attribs.y, attribs.x, attribs.y);
    Material mat;
    HitInfo hitInfo = unpackTriangle(gl_PrimitiveID, barycentricCoords, mat);

    payloadIn.directLight = vec3(0.0);
    if (hitInfo.emissiveColour == vec3(0.0)) {
        // Calculate direct light contribution from point and directional lights, and emissive surfaces
        vec3 shadowRayOrigin = hitInfo.pos + BIAS * hitInfo.normal;

        uint numLights = numPointLights + numDirectionalLights + numEmissiveSurfaces;
        int lightIdx = rnd(seed, 0, int(numLights - 1));
        if (lightIdx < numPointLights) {
            PointLight light = pointLights[lightIdx];
            vec3 lightRay = light.position - shadowRayOrigin;
            float lightDist = length(lightRay);
            vec3 lightDir = lightRay / lightDist;
    
            shadowRayPayload.shadowRayMiss = false;
            traceRayEXT(topLevelAS, gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsSkipClosestHitShaderEXT, 0xFF, 0, 0, 1, shadowRayOrigin, 0.0, lightDir, lightDist, 1);
            if (shadowRayPayload.shadowRayMiss) {
                float k_d = dot(hitInfo.normal, lightDir);
                float attenuation = light.range == 0.0 ? 1.0 : max(1.0 - pow(lightDist / light.range, 4), 0.0);
                attenuation /= lightDist * lightDist;
                attenuation = min(attenuation, 1.0);
                payloadIn.directLight += k_d * light.colour;
            }
        } else if (lightIdx < numPointLights + numDirectionalLights) {
            DirectionalLight light = directionalLights[lightIdx - numPointLights];
    
            shadowRayPayload.shadowRayMiss = false;
            traceRayEXT(topLevelAS, gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsSkipClosestHitShaderEXT, 0xFF, 0, 0, 1, shadowRayOrigin, 0.0, -light.direction, 10000.0, 1);
            if (shadowRayPayload.shadowRayMiss) {
                float k_d = dot(hitInfo.normal, -light.direction);
                payloadIn.directLight += k_d * light.colour;
            }
        } else {
            EmissiveSurface es = emissiveSurfaces[lightIdx - numDirectionalLights - numPointLights];
            vec3 worldPoint = vec3(es.transform * vec4(es.minCoord + rndCube(seed) * (es.maxCoord - es.minCoord), 1.0));
            vec3 lightRay = worldPoint - shadowRayOrigin;
            float lightDist = length(lightRay);
            vec3 lightDir = lightRay / lightDist;
            
            emissiveRayPayload.instanceIdx = es.geometryIdx;
            traceRayEXT(topLevelAS, gl_RayFlagsTerminateOnFirstHitEXT, 0xFF, 1, 0, 2, shadowRayOrigin, 0.0, lightDir, lightDist + BIAS, 2);
        
            if (emissiveRayPayload.instanceHit) {
                float k_d = dot(hitInfo.normal, lightDir);
                float attenuation = min(1.0, 1.0 / lightDist * lightDist);
                payloadIn.directLight += emissiveRayPayload.emittedLight * k_d * attenuation;

            }
        }
        payloadIn.directLight /= numLights;
    }
    
    payloadIn.hitPos = hitInfo.pos;
    payloadIn.hitNormal = hitInfo.normal;
    payloadIn.emittedLight = hitInfo.emissiveColour;
    payloadIn.baseColour = hitInfo.baseColour.rgb;
    payloadIn.transmissionFactor = hitInfo.transmissionFactor;
    payloadIn.ior = mat.ior;
    payloadIn.thin = mat.thicknessFactor == 0;
    if (payloadIn.thin) {
        payloadIn.attenuationDistance = mat.attenuationDistance;
        payloadIn.attenuationColour = mat.attenuationColour;
    }
}