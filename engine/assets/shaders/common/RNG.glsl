
// Requires: Constants.glsl (EPSILON)

uint pcgHash(uint seed) {
    uint state = seed * 747796405u + 2891336453u;
    uint word  = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float randFloat(inout uint seed) {
    seed = pcgHash(seed);
    return float(seed) / float(0xFFFFFFFFu);
}

vec2 randVec2(inout uint seed) {
    return vec2(randFloat(seed), randFloat(seed));
}

bool hasInvalid(float v) { return isnan(v) || isinf(v); }
bool hasInvalid(vec3 v)  { return any(isnan(v)) || any(isinf(v)); }

vec3 sanitizeColor(vec3 color) {
    if (hasInvalid(color)) return vec3(0.0);
    return clamp(color, vec3(0.0), vec3(20.0));
}

vec3 sanitizeColor(vec3 color, vec3 fallback) {
    if (hasInvalid(color)) return fallback;
    return clamp(color, vec3(0.0), vec3(20.0));
}

vec3 safeNormalize(vec3 v, vec3 fallback) {
    float len2 = dot(v, v);
    if (hasInvalid(v) || hasInvalid(len2) || len2 <= EPSILON * EPSILON) return fallback;
    return v * inversesqrt(len2);
}

vec3 fallbackTangent(vec3 N) {
    vec3 axis = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
    return safeNormalize(cross(axis, N), vec3(1.0, 0.0, 0.0));
}

vec3 offsetRayOrigin(vec3 position, vec3 geometricNormal, vec3 direction) {
    float side = dot(direction, geometricNormal) >= 0.0 ? 1.0 : -1.0;
    return position + geometricNormal * (0.001 * side);
}
