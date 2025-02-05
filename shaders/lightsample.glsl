#ifndef LIGHT_SAMPLE_GLSL
#define LIGHT_SAMPLE_GLSL

#include "light.glsl"

layout(location = 1) rayPayloadEXT ShadowPayload shadowRayPayload;
layout(location = 2) rayPayloadEXT EmissivePayload emissiveRayPayload;
layout(location = 3) rayPayloadEXT EmissivePDFPayload emissivePDFPayload;

vec3 sampleAnalyticLight(inout uint seed, vec3 origin, vec3 normal, out vec3 lightDir, out float pdf) {
    uint numAnalyticLights = numPointLights + numDirectionalLights;
    float pFactor = 1.0 / (float(numPointLights > 0) + float(numDirectionalLights > 0));
    if (numPointLights > 0 && (rnd(seed) < 0.5 || numDirectionalLights == 0)) {
        int lightIdx = rnd(seed, 0, int(numPointLights - 1));
        PointLight light = pointLights[lightIdx];
        vec3 lightRay = light.position - origin;
        float lightDist = length(lightRay);
        lightDir = lightRay / lightDist;

        vec3 rayOrigin = origin + (dot(normal, lightDir) >= 0.0 ? 1.0 : -1.0) * BIAS * normal;
        shadowRayPayload.seed = payloadIn.seed;
        shadowRayPayload.shadowRayMiss = false;
        traceRayEXT(topLevelAS, gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsSkipClosestHitShaderEXT, 0xFF, 1, 0, 1, rayOrigin, 0, lightDir, lightDist, 1);
        payloadIn.seed = shadowRayPayload.seed;
        pdf = pFactor / numPointLights;
        if (shadowRayPayload.shadowRayMiss) {
            float attenuation = light.range == 0.0 ? 1.0 : max(1.0 - pow(lightDist / light.range, 4), 0.0);
            attenuation /= lightDist * lightDist;
            attenuation = min(attenuation, 1.0);
            return light.colour * light.intensity * attenuation;
        }
    } else {
        int lightIdx = rnd(seed, int(numPointLights), int(numPointLights + numDirectionalLights - 1));
        DirectionalLight light = directionalLights[lightIdx - numPointLights];

        shadowRayPayload.seed = payloadIn.seed;
        shadowRayPayload.shadowRayMiss = false;
        lightDir = -light.direction;
        vec3 rayOrigin = origin + (dot(normal, lightDir) >= 0.0 ? 1.0 : -1.0) * BIAS * normal;
        traceRayEXT(topLevelAS, gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsSkipClosestHitShaderEXT, 0xFF, 1, 0, 1, rayOrigin, 0, lightDir, INF, 1);
        payloadIn.seed = shadowRayPayload.seed;
        pdf = pFactor / numDirectionalLights;
        if (shadowRayPayload.shadowRayMiss) {
            return light.colour * light.intensity;
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
    vec3 rayOrigin = origin + (dot(normal, lightDir) >= 0.0 ? 1.0 : -1.0) * BIAS * normal;

    emissiveRayPayload.seed = payloadIn.seed;
    emissiveRayPayload.instanceGeometryIdx = es.geometryIdx;
    emissiveRayPayload.instancePrimitiveIdx = primitiveIdx;
    emissiveRayPayload.instanceHit = false;
    traceRayEXT(topLevelAS, gl_RayFlagsNoneEXT, 0xFF, 2, 0, 2, rayOrigin, 0, lightDir, lightDist + EPS, 2);

    payloadIn.seed = emissiveRayPayload.seed;
    if (emissiveRayPayload.instanceHit) {
        emissivePDFPayload.pdf = 0;
        traceRayEXT(topLevelAS, gl_RayFlagsSkipClosestHitShaderEXT | gl_RayFlagsNoOpaqueEXT, 1u << 1, 3, 0, 2, origin, 0, lightDir, INF, 3);
        pdf = emissivePDFPayload.pdf;
        return emissiveRayPayload.emittedLight;
    }
    return vec3(0.0);
}

#endif