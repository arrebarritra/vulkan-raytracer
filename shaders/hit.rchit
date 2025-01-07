#version 460
#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_debug_printf : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int32 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require

struct Vertex {
    vec3 pos, normal;
    vec4 tangent;
    vec2 uv0, uv1;
};

struct Material {
    vec4 baseColourFactor;
    vec3 emissiveFactor;
    float roughnessFactor, metallicFactor;
    int baseColourTexIdx, roughnessMetallicTexIdx, normalTexIdx, emissiveTexIdx;
    bool doubleSided;
};

struct GeometryInfo {
    uint64_t vertexBufferAddress, indexBufferAddress;
    uint materialIdx;
};

struct PointLight {
    vec3 position, colour;
    float intensity, range;
};

struct DirectionalLight {
    vec3 direction, colour;
    float intensity;
};

struct HitInfo {
    vec3 pos, normal, tangent, bitangent;
    vec2 uv0, uv1;
    vec3 baseColour, emissiveColour;
    float roughnessFactor, metallicFactor;
};

layout(buffer_reference, scalar) buffer Vertices { Vertex vertices[]; };
layout(buffer_reference, scalar) buffer Indices { uint32_t indices[]; };

layout(binding = 0, set = 0) uniform accelerationStructureEXT topLevelAS;
layout(binding = 2, set = 0, scalar) uniform UniformData {
    uint sampleCount;
	mat4 viewInverse;
	mat4 projInverse;
} cam;
layout(binding = 3, set = 0, scalar) readonly buffer GeometryInfos { GeometryInfo geometryInfos[]; };
layout(binding = 4, set = 0, scalar) readonly buffer Materials { Material materials[]; };

layout(binding = 5, set = 0, scalar) readonly buffer PointLights { 
    uint numPointLights;
    PointLight pointLights[]; 
};
layout(binding = 6, set = 0, scalar) readonly buffer DirectionalLights { 
    uint numDirectionalLights;
    DirectionalLight directionalLights[]; 
};

layout(binding = 7, set = 0) uniform sampler2D textures[];

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
        hitInfo.tangent += vertex.tangent.xyz * weights[i];
        hitInfo.bitangent += cross(hitInfo.normal, hitInfo.tangent) * vertex.tangent.w * weights[i];
        hitInfo.uv0 += vertex.uv0 * weights[i];
        hitInfo.uv1 += vertex.uv1 * weights[i];
    }

    hitInfo.baseColour = material.baseColourFactor.xyz;
    if (material.baseColourTexIdx != -1)
        hitInfo.baseColour = texture(textures[nonuniformEXT(material.baseColourTexIdx)], hitInfo.uv0).rgb;
    hitInfo.emissiveColour = material.emissiveFactor;
    if (material.emissiveTexIdx != -1)
        hitInfo.emissiveColour = texture(textures[nonuniformEXT(material.emissiveTexIdx)], hitInfo.uv0).rgb;

    hitInfo.normal = normalize(hitInfo.normal);
    if (material.normalTexIdx != -1) {
        hitInfo.tangent = normalize(hitInfo.tangent);
        hitInfo.bitangent = normalize(hitInfo.bitangent);
        hitInfo.normal = mat3(hitInfo.tangent, hitInfo.bitangent, hitInfo.normal) * normalize(texture(textures[nonuniformEXT(material.normalTexIdx)], hitInfo.uv0).rgb * 2.0 - 1.0);
    }
	vec3 origin = vec3(cam.viewInverse * vec4(0,0,0,1));
    hitInfo.normal = sign(dot(origin - hitInfo.pos, hitInfo.normal)) * hitInfo.normal;
    
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
    float shadowBias = 0.01;
    vec3 shadowRayOrigin = hitInfo.pos + shadowBias * hitInfo.normal;
    vec3 lightFactor = vec3(0.3);

    hitValue = vec3(0.0);
    if (hitInfo.emissiveColour != vec3(0.0))
        hitValue += hitInfo.emissiveColour;
  
    for (int i = 0; i < numPointLights; i++) {
        PointLight light = pointLights[i];
        vec3 lightRay = light.position - shadowRayOrigin;
        float lightDist = length(lightRay);
        vec3 lightDir = lightRay / lightDist;
    
        shadow = true;
        traceRayEXT(topLevelAS, gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT | gl_RayFlagsSkipClosestHitShaderEXT, 0xFF, 0, 0, 1, shadowRayOrigin, 0.0, lightDir, lightDist, 1);
        if (!shadow) {
            float k_d = max(dot(hitInfo.normal, lightDir), 0.0);
            float attenuation = light.range == 0.0 ? 1.0 : max(1.0 - pow(lightDist / light.range, 4), 0.0);
            attenuation /= lightDist * lightDist;
            attenuation = min(attenuation, 1.0);
            lightFactor += k_d * light.colour;
        }
    }

    for (int i = 0; i < numDirectionalLights; i++) {
        DirectionalLight light = directionalLights[i];
    
        shadow = true;
        traceRayEXT(topLevelAS, gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT | gl_RayFlagsSkipClosestHitShaderEXT, 0xFF, 0, 0, 1, shadowRayOrigin, 0.0, -light.direction, 10000.0, 1);
        if (!shadow) {
            float k_d = max(dot(hitInfo.normal, -light.direction), 0.0);
            lightFactor += k_d * light.colour;
        }
    }

    // Add directional light if there are none
    if (numPointLights + numDirectionalLights == 0) {
        DirectionalLight light;
        light.colour = vec3(1.0, 0.8, 0.7);
        light.direction = normalize(vec3(0.1, -1.0, 0.0));

        shadow = true;
        traceRayEXT(topLevelAS, gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT | gl_RayFlagsSkipClosestHitShaderEXT, 0xFF, 0, 0, 1, shadowRayOrigin, 0.0, light.direction, 10000.0, 1);
        if (!shadow) {
            float k_d = max(dot(hitInfo.normal, light.direction), 0.0);
            lightFactor += k_d * light.colour;
        }
    }
    
    hitValue += lightFactor * hitInfo.baseColour;
}