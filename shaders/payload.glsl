struct PathTracingPayload {
    vec3 rayOrigin;
    vec3 hitPos, hitNormal;
    vec3 baseColour, emittedLight, directLight;
    float transmissionFactor, ior;
    bool thin;
    float attenuationDistance;
    vec3 attenuationColour;
};