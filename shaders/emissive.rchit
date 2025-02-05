#version 460
#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_debug_printf : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int32 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require

#include "geometry.glsl"
#include "material.glsl"
#include "texture.glsl"
#include "payload.glsl"

layout(location = 2) rayPayloadInEXT EmissivePayload payload;
hitAttributeEXT vec2 attribs;

struct HitInfo {
    vec3 normal, emissiveColour;
};

HitInfo unpackTriangle(uint idx, vec3 weights) {
    GeometryInfo geometryInfo = geometryInfos[gl_InstanceCustomIndexEXT];
    Material material = materials[geometryInfo.materialIdx];
    Indices indexBuffer = Indices(geometryInfo.indexBufferAddress);
    Vertices vertexBuffer = Vertices(geometryInfo.vertexBufferAddress);
    
    HitInfo hitInfo;
    hitInfo.normal = vec3(0.0);
    vec2 uv = vec2(0.0);
    for (int i = 0; i < 3; i++) {
        uint index = indexBuffer.indices[3 * idx + i];
        Vertex vertex = vertexBuffer.vertices[index];
        hitInfo.normal += vertex.normal * weights[i];
        uv += vertex.uv * weights[i];
    }
    hitInfo.normal = sign(dot(-gl_WorldRayDirectionEXT, hitInfo.normal)) * normalize(hitInfo.normal);

    hitInfo.emissiveColour = material.emissiveFactor;
    if (material.emissiveTexIdx != -1)
        hitInfo.emissiveColour *= textureGet(material.emissiveTexIdx, uv).rgb;

    return hitInfo;
}

void main() {
    if (gl_InstanceCustomIndexEXT == payload.instanceGeometryIdx && gl_PrimitiveID == payload.instancePrimitiveIdx) {
        const vec3 barycentricCoords = vec3(1.0f - attribs.x - attribs.y, attribs.x, attribs.y);
        HitInfo hitInfo = unpackTriangle(gl_PrimitiveID, barycentricCoords);

        if (hitInfo.emissiveColour == vec3(0.0)) return;
        payload.instanceHit = true;
        payload.emittedLight = hitInfo.emissiveColour;
        payload.normal = hitInfo.normal;

    }
}