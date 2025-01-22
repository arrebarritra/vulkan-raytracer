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

vec3 sampleAnalyticLight(inout uint seed, vec3 origin, vec3 normal) {
    vec3 lightDir;
    uint numAnalyticLights = numPointLights + numDirectionalLights;
    int lightIdx = rnd(seed, 0, int(numAnalyticLights - 1));
    if (lightIdx < numPointLights) {
        PointLight light = pointLights[lightIdx];
        vec3 lightRay = light.position - origin;
        float lightDist = length(lightRay);
        lightDir = lightRay / lightDist;

        shadowRayPayload.shadowRayMiss = false;
        traceRayEXT(topLevelAS, gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsSkipClosestHitShaderEXT, 0xFF, 0, 0, 1, origin, 0.0, lightDir, lightDist, 1);
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
        traceRayEXT(topLevelAS, gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsSkipClosestHitShaderEXT, 0xFF, 0, 0, 1, origin, 0.0, lightDir, 10000.0, 1);
        if (shadowRayPayload.shadowRayMiss) {
            float NdotL = dot(normal, -light.direction);
            return NdotL * light.colour;
        }
    }
    return vec3(0.0);
}

vec3 sampleEmissiveTriangle(inout uint seed, vec3 origin, vec3 normal, out float pdf) {
    vec3 lightDir;
    uint triangleIdx;
    float pTriangle;
    // Binary search for triangle index
    {
        float p = rnd(seed);
        uint a = 0;
        uint b = numEmissiveTriangles - 1;
        while (true) {
            uint mid = (a + b) / 2;
            float pLeft = mid == 0 ? 0.0 : emissiveTriangles[mid - 1].pHeuristic;
            float pRight = emissiveTriangles[mid].pHeuristic;
            if (p < pLeft) {
                b = mid;
            } else if (p > pRight) {
                a = mid;
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
        while (true) {
            uint mid = (a + b) / 2;
            uint idxLeft = emissiveSurfaces[mid].baseEmissiveTriangleIdx;
            uint idxRight = mid == numEmissiveSurfaces - 1 ? numEmissiveTriangles : emissiveSurfaces[mid + 1].baseEmissiveTriangleIdx;
            if (triangleIdx < idxLeft) {
                b = mid;
            } else if (triangleIdx > idxRight) {
                a = mid;
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
    v[0] = vec3(es.transform * vec4(vertexBuffer.vertices[indexBuffer.indices[3 * primitiveIdx]].pos, 1.0));
    v[1] = vec3(es.transform * vec4(vertexBuffer.vertices[indexBuffer.indices[3 * primitiveIdx + 1]].pos, 1.0));
    v[2] = vec3(es.transform * vec4(vertexBuffer.vertices[indexBuffer.indices[3 * primitiveIdx + 2]].pos, 1.0));
    pdf = pTriangle / (length(cross(v[1] - v[0], v[2] - v[0])) / 2.0);

    vec2 uv = rndSquare(seed);
    // Invert points on parallelogram outside triangle
    if (uv.x + uv.y > 1.0) {
        uv.x = 1 - uv.x;
        uv.y = 1 - uv.y;
    }
    vec3 samplePoint = v[0] * uv.x + v[1] * uv.y + v[2] * (1 - uv.x - uv.y);

    vec3 lightRay = samplePoint - origin;
    float lightDist = length(lightRay);
    lightDir = lightRay / lightDist;

    emissiveRayPayload.instanceGeometryIdx = es.geometryIdx;
    emissiveRayPayload.instancePrimitiveIdx = primitiveIdx;
    traceRayEXT(topLevelAS, gl_RayFlagsTerminateOnFirstHitEXT, 0xFF, 1, 0, 2, origin, 0.0, lightDir, lightDist + BIAS, 2);
    if (emissiveRayPayload.instanceHit) {
        float NdotL = dot(normal, lightDir);
        float attenuation = min(1.0, 1.0 / lightDist * lightDist);
        return emissiveRayPayload.emittedLight * NdotL * attenuation;
    }
    return vec3(0.0);
}

#endif