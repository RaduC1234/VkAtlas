
// Requires: Constants.glsl (EPSILON)

float distanceAttenuation(float dist, float range) {
    if (range <= 0.0) return 1.0 / max(dist * dist, EPSILON);
    float ratio  = dist / range;
    float ratio4 = ratio * ratio * ratio * ratio;
    float window = max(1.0 - ratio4, 0.0);
    return (window * window) / max(dist * dist, EPSILON);
}

float spotAttenuation(vec3 L, vec3 dir, float innerAngle, float outerAngle) {
    float cosOuter = cos(outerAngle);
    float cosInner = cos(innerAngle);
    float cosAngle = dot(dir, -L);
    return clamp((cosAngle - cosOuter) / max(cosInner - cosOuter, EPSILON), 0.0, 1.0);
}
