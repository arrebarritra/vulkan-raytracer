vec3 reinhard(vec3 v) {
    return v / (1.0f + v);
}

float luminance(vec3 v) {
    return dot(v, vec3(0.2126f, 0.7152f, 0.0722f));
}

vec3 reinhardJodie(vec3 v) {
    float l = luminance(v);
    vec3 tv = reinhard(v);
    return mix(v / (1.0f + l), tv, tv);
}