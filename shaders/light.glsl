#ifndef LIGHTS_GLSL
#define LIGHTS_GLSL

struct PointLight {
    vec3 position, colour;
    float intensity, range;
};

struct DirectionalLight {
    vec3 direction, colour;
    float intensity;
};

struct EmissiveSurface {
    uint geometryIdx, baseEmissiveTriangleIdx;
    mat4 transform;
};

struct EmissiveTriangle {
    float pHeuristic;
};

layout(binding = 7, set = 0, scalar) readonly buffer PointLights {
    uint numPointLights;
    PointLight pointLights[];
};
layout(binding = 8, set = 0, scalar) readonly buffer DirectionalLights {
    uint numDirectionalLights;
    DirectionalLight directionalLights[];
};
layout(binding = 9, set = 0, scalar) readonly buffer EmissiveSurfaces {
    uint numEmissiveSurfaces;
    EmissiveSurface emissiveSurfaces[];
};
layout(binding = 10, set = 0, scalar) readonly buffer EmissiveTriangles{
    uint numEmissiveTriangles;
    EmissiveTriangle emissiveTriangles[];
};

layout(location = 1) rayPayloadEXT ShadowPayload shadowRayPayload;
layout(location = 2) rayPayloadEXT EmissivePayload emissiveRayPayload;

vec3 sampleAnalyticLight(inout uint seed, vec3 origin, vec3 normal, out vec3 lightDir) {
    uint numAnalyticLights = numPointLights + numDirectionalLights;
    int lightIdx = rnd(seed, 0, int(numAnalyticLights - 1));
    if (lightIdx < numPointLights) {
        PointLight light = pointLights[lightIdx];
        vec3 lightRay = light.position - origin;
        float lightDist = length(lightRay);
        lightDir = lightRay / lightDist;

        shadowRayPayload.shadowRayMiss = false;
        traceRayEXT(topLevelAS, gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsSkipClosestHitShaderEXT, 0xFF, 0, 0, 1, origin, BIAS, lightDir, lightDist, 1);
        if (shadowRayPayload.shadowRayMiss) {
            float NdotL = dot(normal, lightDir);
            float attenuation = light.range == 0.0 ? 1.0 : max(1.0 - pow(lightDist / light.range, 4), 0.0);
            attenuation /= lightDist * lightDist;
            attenuation = min(attenuation, 1.0);
            return NdotL * light.colour;
        }
    } else {
        DirectionalLight light = directionalLights[lightIdx - numPointLights];

        shadowRayPayload.shadowRayMiss = false;
        lightDir = -light.direction;
        traceRayEXT(topLevelAS, gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsSkipClosestHitShaderEXT, 0xFF, 0, 0, 1, origin, BIAS, lightDir, 10000.0, 1);
        if (shadowRayPayload.shadowRayMiss) {
            float NdotL = dot(normal, -light.direction);
            return NdotL * light.colour;
        }
    }
    return vec3(0.0);
}

vec3 sampleEmissiveTriangle(inout uint seed, vec3 origin, vec3 normal, out vec3 lightDir, out float pdf) {
    uint triangleIdx;
    float pTriangle;
    // Binary search for triangle index
    {
        float p = rnd(seed);
        uint a = 0;
        uint b = numEmissiveTriangles - 1;
        int i = 0;
        while (true) {
            uint mid = (a + b) / 2;
            float pLeft = mid == 0 ? 0.0 : emissiveTriangles[mid - 1].pHeuristic;
            float pRight = emissiveTriangles[mid].pHeuristic;
            if (p < pLeft) {
                b = mid - 1;
            } else if (p > pRight) {
                a = mid + 1;
            } else {
                triangleIdx = mid;
                pTriangle = pRight - pLeft;
                break;
            }
        }
    }

    // Binary search for corresponding emissive surface
    uint surfaceIdx;
    {
        uint a = 0;
        uint b = numEmissiveSurfaces - 1;
        int i = 0;
        while (true) {
            uint mid = (a + b) / 2;
            uint idxLeft = emissiveSurfaces[mid].baseEmissiveTriangleIdx;
            uint idxRight = mid == numEmissiveSurfaces - 1 ? numEmissiveTriangles : emissiveSurfaces[mid + 1].baseEmissiveTriangleIdx;
            if (triangleIdx < idxLeft) {
                b = mid - 1;
            } else if (triangleIdx > idxRight) {
                a = mid + 1;
            } else {
                surfaceIdx = mid;
                break;
            }
        }
    }

    EmissiveSurface es = emissiveSurfaces[surfaceIdx];
    GeometryInfo geometryInfo = geometryInfos[es.geometryIdx];
    Material emissiveMat = materials[geometryInfo.materialIdx];
    Indices indexBuffer = Indices(geometryInfo.indexBufferAddress);
    Vertices vertexBuffer = Vertices(geometryInfo.vertexBufferAddress);

    uint primitiveIdx = triangleIdx - es.baseEmissiveTriangleIdx;
    vec3 v[3];
    for (int i = 0; i < 3; i++) {
        uint index = indexBuffer.indices[3 * primitiveIdx + i];
        Vertex vertex = vertexBuffer.vertices[index];
        v[i] = vec3(es.transform * vec4(vertex.pos, 1.0));
    }
    pdf = pTriangle;

    vec2 uv = rndSquare(seed);
    // Invert points on parallelogram outside triangle
    if (uv.x + uv.y > 1.0) {
        uv.x = 1 - uv.x;
        uv.y = 1 - uv.y;
    }
    vec3 samplePoint = v[0] * uv.x + v[1] * uv.y + v[2] * (1 - uv.x - uv.y);

    vec3 rayOrigin = origin + BIAS * normal;
    vec3 lightRay = samplePoint - rayOrigin;
    float lightDist = length(lightRay);
    float lightDistSq = lightDist * lightDist;
    lightDir = lightRay / lightDist;

    emissiveRayPayload.instanceGeometryIdx = es.geometryIdx;
    emissiveRayPayload.instancePrimitiveIdx = primitiveIdx;
    traceRayEXT(topLevelAS, gl_RayFlagsNoneEXT, 0xFF, 1, 0, 2, rayOrigin, 0, lightDir, lightDist + EPS, 2);
    if (emissiveRayPayload.instanceHit) {
        float NdotL = dot(normal, lightDir);
        float attenuation = min(1.0, 1.0 / lightDistSq);
        return emissiveRayPayload.emittedLight * NdotL * attenuation;
    }
    return vec3(0.0);
}

#endif