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

vec3 hableTonemap(vec3 x)
{
    float A = 0.15;
    float B = 0.50;
    float C = 0.10;
    float D = 0.20;
    float E = 0.02;
    float F = 0.30;

    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}