#ifndef HIT_GLSL
#define HIT_GLSL

struct HitMaterial {
	vec3 baseColour, emissiveColour;
	float metallic;
	vec2 alpha, anisotropyDirection;
	float transmissionFactor;
	float ior;
	bool thin;
	vec3 attenuationCoefficient;
	float dispersion;
};

struct HitInfo {
    vec3 pos, normal, tangent, bitangent;
    float t;
	bool frontFace;
	HitMaterial hitMat;
};

#endif