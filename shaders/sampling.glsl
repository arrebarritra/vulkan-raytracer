#ifndef SAMPLING_GLSL
#define SAMPLING_GLSL

float balanceHeuristic(float p1, float n1, float p2, float n2) {
    return n1 * p1 / (n1 * p1 + n2 * p2);
}

float balanceHeuristic(float p1, float p2) {
    return p1 / (p1 + p2);
}

#endif