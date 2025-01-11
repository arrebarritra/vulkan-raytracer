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

layout(binding = 0, set = 0) uniform accelerationStructureEXT topLevelAS;
layout(binding = 9, set = 0) uniform sampler2D textures[];

layout(location = 0) rayPayloadInEXT PathTracingPayload payload;
layout(location = 1) rayPayloadEXT bool shadow;
hitAttributeEXT vec2 attribs;

struct HitInfo {
    vec3 pos, normal, baseColour;
    vec3 emissiveColour;
    float roughnessFactor, metallicFactor;
    float transmissionFactor;
};

HitInfo unpackTriangle(uint idx, vec3 weights, out GeometryInfo geometryInfo, out Material material) {
    geometryInfo = geometryInfos[gl_InstanceCustomIndexEXT];
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
        hitInfo.baseColour *= texture(textures[nonuniformEXT(material.baseColourTexIdx)], uv0).rgb;
    
    hitInfo.emissiveColour = material.emissiveFactor;
    if (material.emissiveTexIdx != -1)
        hitInfo.emissiveColour *= texture(textures[nonuniformEXT(material.emissiveTexIdx)], uv0).rgb;
    hitInfo.emissiveColour *= material.emissiveStrength;

    hitInfo.transmissionFactor = material.transmissionFactor;
    if (material.transmissionTexIdx != -1)
        hitInfo.transmissionFactor *= texture(textures[nonuniformEXT(material.transmissionTexIdx)], uv0).r;

    hitInfo.normal = normalize(hitInfo.normal);
    if (material.normalTexIdx != -1) {
        tangent = normalize(tangent);
        bitangent = normalize(bitangent);
        hitInfo.normal = mat3(tangent, bitangent, hitInfo.normal) * normalize(texture(textures[nonuniformEXT(material.normalTexIdx)], uv0).rgb * 2.0 - 1.0);
    }
    hitInfo.normal = sign(dot(payload.rayOrigin - hitInfo.pos, hitInfo.normal)) * hitInfo.normal;
    
    hitInfo.roughnessFactor = material.roughnessFactor;
    hitInfo.metallicFactor = material.metallicFactor;
    if (material.metallicRoughnessTexIdx != -1) {
        vec2 metallicRoughness = texture(textures[nonuniformEXT(material.metallicRoughnessTexIdx)], uv0).bg;
        hitInfo.metallicFactor = metallicRoughness.x;
        hitInfo.roughnessFactor = metallicRoughness.y;
    }

    return hitInfo;
}

void main() {
    const vec3 barycentricCoords = vec3(1.0f - attribs.x - attribs.y, attribs.x, attribs.y);
    GeometryInfo geometryInfo;
    Material mat;
    HitInfo hitInfo = unpackTriangle(gl_PrimitiveID, barycentricCoords, geometryInfo, mat);

    // Cast shadow rays
//    float shadowBias = 0.01;
//    vec3 shadowRayOrigin = hitInfo.pos + shadowBias * hitInfo.normal;
//    vec3 lightFactor = vec3(0.3);
  
//    for (int i = 0; i < numPointLights; i++) {
//        PointLight light = pointLights[i];
//        vec3 lightRay = light.position - shadowRayOrigin;
//        float lightDist = length(lightRay);
//        vec3 lightDir = lightRay / lightDist;
//    
//        shadow = true;
//        traceRayEXT(topLevelAS, gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT | gl_RayFlagsSkipClosestHitShaderEXT, 0xFF, 0, 0, 1, shadowRayOrigin, 0.0, lightDir, lightDist, 1);
//        if (!shadow) {
//            float k_d = max(dot(hitInfo.normal, lightDir), 0.0);
//            float attenuation = light.range == 0.0 ? 1.0 : max(1.0 - pow(lightDist / light.range, 4), 0.0);
//            attenuation /= lightDist * lightDist;
//            attenuation = min(attenuation, 1.0);
//            lightFactor += k_d * light.colour;
//        }
//    }
//
//    for (int i = 0; i < numDirectionalLights; i++) {
//        DirectionalLight light = directionalLights[i];
//    
//        shadow = true;
//        traceRayEXT(topLevelAS, gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT | gl_RayFlagsSkipClosestHitShaderEXT, 0xFF, 0, 0, 1, shadowRayOrigin, 0.0, -light.direction, 10000.0, 1);
//        if (!shadow) {
//            float k_d = max(dot(hitInfo.normal, -light.direction), 0.0);
//            lightFactor += k_d * light.colour;
//        }
//    }
//    // Add directional light if there are no lights
//    if (numPointLights + numDirectionalLights == 0) {
//        DirectionalLight light;
//        light.colour = vec3(1.0, 0.8, 0.7);
//        light.direction = normalize(vec3(0.1, -1.0, 0.0));
//
//        shadow = true;
//        traceRayEXT(topLevelAS, gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT | gl_RayFlagsSkipClosestHitShaderEXT, 0xFF, 0, 0, 1, shadowRayOrigin, 0.0, -light.direction, 10000.0, 1);
//        if (!shadow) {
//            float k_d = max(dot(hitInfo.normal, light.direction), 0.0);
//            lightFactor += k_d * light.colour;
//        }
//    }
    
    payload.hitPos = hitInfo.pos;
    payload.hitNormal = hitInfo.normal;
    payload.emittedLight = hitInfo.emissiveColour;
    payload.baseColour = hitInfo.baseColour.rgb;
    payload.directLight = vec3(0.0);
    payload.transmissionFactor = hitInfo.transmissionFactor;
    payload.ior = mat.ior;
    payload.thin = mat.thicknessFactor == 0;
    if (payload.thin) {
        payload.attenuationDistance = mat.attenuationDistance;
        payload.attenuationColour = mat.attenuationColour;
    }
}