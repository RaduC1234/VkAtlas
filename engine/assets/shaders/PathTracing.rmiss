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

struct DebugData {
    float iblMultiplier;
    float exposureMultiplier;
    uint  viewMode;
};

struct CameraData {
    mat4  projection;
    mat4  view;
    mat4  viewProjection;
    vec4  frustumPlanes[6];
    vec3  position;
    float nearPlane;
    vec3  direction;
    float farPlane;
};

layout(set = 0, binding = 0) uniform GlobalUbo {
    CameraData cameraData;
    DebugData  debugData;
} ubo;

layout(set = 1, binding = 9) uniform samplerCube envMap;

layout(location = 0) rayPayloadInEXT RayPayload payload;

void main() {
    vec3 env = texture(envMap, payload.direction).rgb;
    env *= ubo.debugData.iblMultiplier;

    // Guard against NaN/Inf from malformed HDR data
    if (any(isnan(env)) || any(isinf(env))) {
        env = vec3(0.0);
    }

    payload.radiance += payload.throughput * env;
    payload.done = true;
}
