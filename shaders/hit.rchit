#version 460
#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_debug_printf : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int32 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require


struct Vertex {
    vec3 pos, normal, tangent, bitangent;
    vec2 uv0, uv1;
};

struct Material {
    vec4 baseColourFactor;
    float roughnessFactor, metallicFactor;
    int baseColourTexIdx, roughnessMetallicTexIdx, normalTexIdx;
    bool doubleSided;
};

struct GeometryInfo {
    uint64_t vertexBufferAddress, indexBufferAddress;
    uint materialIdx;
};

struct PointLight {
    vec3 position, colour;
    float attenuationConstant, attenuationLinear, attenuationQuadratic;
};

struct DirectionalLight {
    vec3 direction, colour;
};

struct HitInfo {
    vec3 pos, normal, tangent, bitangent;
    vec2 uv0, uv1;
    vec3 baseColour;
    float roughnessFactor, metallicFactor;
};

layout(buffer_reference, scalar) buffer Vertices { Vertex vertices[]; };
layout(buffer_reference, scalar) buffer Indices { uint32_t indices[]; };

layout(binding = 0, set = 0) uniform accelerationStructureEXT topLevelAS;
layout(binding = 3, set = 0, scalar) readonly buffer GeometryInfos { GeometryInfo geometryInfos[]; };
layout(binding = 4, set = 0, scalar) readonly buffer Materials { Material materials[]; };
layout(binding = 5, set = 0) uniform sampler2D textures[];

layout(binding = 6, set = 0, scalar) readonly buffer PointLights { 
    uint numPointLights;
    PointLight pointLights[]; 
};
layout(binding = 7, set = 0, scalar) readonly buffer DirectionalLights { 
    uint numDirectionalLights;
    DirectionalLight directionalLights[]; 
};

layout(location = 0) rayPayloadInEXT vec3 hitValue;
layout(location = 1) rayPayloadEXT bool shadow;
hitAttributeEXT vec2 attribs;

HitInfo unpackTriangle(uint idx, vec3 weights, out GeometryInfo geometryInfo, out Material material) {
    geometryInfo = geometryInfos[gl_InstanceCustomIndexEXT];
    material = materials[geometryInfo.materialIdx];
    Indices indexBuffer = Indices(geometryInfo.indexBufferAddress);
    Vertices vertexBuffer = Vertices(geometryInfo.vertexBufferAddress);
    
    HitInfo hitInfo;
    hitInfo.pos = vec3(0.0);
    hitInfo.normal = vec3(0.0);
    hitInfo.tangent = vec3(0.0);
    hitInfo.bitangent = vec3(0.0);
    hitInfo.uv0 = vec2(0.0);
    hitInfo.uv1 = vec2(0.0);
    for (int i = 0; i < 3; i++) {
        uint index = indexBuffer.indices[3 * idx + i];
        Vertex vertex = vertexBuffer.vertices[index];

        hitInfo.pos += vertex.pos * weights[i];
        hitInfo.normal += vertex.normal * weights[i];
        hitInfo.tangent += vertex.tangent * weights[i];
        hitInfo.bitangent += vertex.bitangent * weights[i];
        hitInfo.uv0 += vertex.uv0 * weights[i];
        hitInfo.uv1 += vertex.uv1 * weights[i];
    }

    hitInfo.baseColour = material.baseColourFactor.xyz;
    if (material.baseColourTexIdx != -1)
        hitInfo.baseColour = texture(textures[nonuniformEXT(material.baseColourTexIdx)], hitInfo.uv0).rgb;

    hitInfo.normal = normalize(hitInfo.normal);
    if (material.normalTexIdx != -1) {
        hitInfo.tangent = normalize(hitInfo.tangent);
        hitInfo.bitangent = normalize(hitInfo.bitangent);
        hitInfo.normal = mat3(hitInfo.tangent, hitInfo.bitangent, hitInfo.normal) * normalize(texture(textures[nonuniformEXT(material.normalTexIdx)], hitInfo.uv1).rgb * 2.0 - 1.0);
    }
    
    if (material.roughnessMetallicTexIdx != -1) {
        vec2 roughnessMetallic = texture(textures[nonuniformEXT(material.roughnessMetallicTexIdx)], hitInfo.uv0).rg;
        hitInfo.roughnessFactor = roughnessMetallic.x;
        hitInfo.metallicFactor = roughnessMetallic.y;
    } else {
        hitInfo.roughnessFactor = material.roughnessFactor;
        hitInfo.metallicFactor = material.metallicFactor;
    }

    return hitInfo;
}

void main() {
  const vec3 barycentricCoords = vec3(1.0f - attribs.x - attribs.y, attribs.x, attribs.y);
  GeometryInfo geometryInfo;
  Material mat;
  HitInfo hitInfo = unpackTriangle(gl_PrimitiveID, barycentricCoords, geometryInfo, mat);

  // Cast shadow rays
  vec3 shadowRayOrigin = hitInfo.pos + 0.0001 * hitInfo.normal;
  vec3 lightFactor = vec3(0.3);
  for (int i = 0; i < numPointLights; i++) {
    PointLight pointLight = pointLights[i];
    vec3 lightRay = pointLight.position - shadowRayOrigin;
    float lightDist = length(lightRay);
    vec3 lightDir = lightRay / lightDist;
    
    shadow = true;
    traceRayEXT(topLevelAS, gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT | gl_RayFlagsSkipClosestHitShaderEXT, 0xFF, 0, 0, 1, shadowRayOrigin, 0.0, lightDir, lightDist, 1);
    if (!shadow) {
        float k_d = max(dot(hitInfo.normal, lightDir), 0.0);
        lightFactor += k_d * vec3(1.0);
    }
  }
  for (int i = 0; i < numDirectionalLights; i++) {
    DirectionalLight directionalLight = directionalLights[i];
    
    shadow = true;
    traceRayEXT(topLevelAS, gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT | gl_RayFlagsSkipClosestHitShaderEXT, 0xFF, 0, 0, 1, shadowRayOrigin, 0.0, -directionalLight.direction, 10000.0, 1);
    if (!shadow) {
        float k_d = max(dot(hitInfo.normal, -directionalLight.direction), 0.0);
        lightFactor += k_d * vec3(1.0);
    }
  }
  hitValue = lightFactor * hitInfo.baseColour;
}