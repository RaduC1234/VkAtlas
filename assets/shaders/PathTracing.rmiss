#version 460
#extension GL_EXT_ray_tracing : require

struct RayPayload {
    vec3  radiance;
    vec3  throughput;
    vec3  origin;
    vec3  direction;
    bool  done;
    uint  seed;
};

layout(location = 0) rayPayloadInEXT RayPayload payload;

// Simple sky gradient — replace with cubemap sample later
vec3 skyColor(vec3 dir) {
    float t       = clamp(dir.y * 0.5 + 0.5, 0.0, 1.0);
    vec3  horizon = vec3(0.8, 0.85, 0.9);
    vec3  zenith  = vec3(0.2, 0.4, 0.8);
    vec3  ground  = vec3(0.15, 0.12, 0.10);
    if (dir.y >= 0.0)
    return mix(horizon, zenith, t * t);
    else
    return mix(horizon, ground, -dir.y);
}

void main() {
    payload.radiance += payload.throughput * skyColor(payload.direction);
    payload.done      = true;
}