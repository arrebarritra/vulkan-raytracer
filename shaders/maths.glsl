#ifndef MATHS_GLSL
#define MATHS_GLSL

#define PI 3.1415926535897932384626433832795
#define TWOPI 6.2831853071795864769252867665590
#define HALFPI 1.5707963267948966192313216916398
#define PIINV 0.31830988618379067153776752674503


// Building an Orthonormal Basis, Revisited
// Tom Duff et. al: https://graphics.pixar.com/library/OrthonormalB/paper.pdf
void branchlessONB(const vec3 n, out vec3 tangent, out vec3 bitangent) {
    float sign = n.z >= 0.0 ? 1.0 : -1.0;
    const float a = -1.0f / (sign + n.z);
    const float b = n.x * n.y * a;
    tangent = vec3(1.0f + sign * n.x * n.x * a, sign * b, -sign * n.x);
    bitangent = vec3(b, sign + n.y * n.y * a, -n.y);
}

#endif