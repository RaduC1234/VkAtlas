#version 460
#extension GL_EXT_ray_tracing         : require
#extension GL_GOOGLE_include_directive : require

#include "common/Types.glsl"
#include "common/RTTypes.glsl"

layout(set = 0, binding = 0) uniform GlobalUbo {
    CameraData cameraData;
    DebugData  debugData;
} ubo;

layout(set = 1, binding = 9) uniform samplerCube envMap;

layout(location = 0) rayPayloadInEXT RayPayload payload;

void main() {
    vec3 env = texture(envMap, payload.direction).rgb;
    env *= ubo.debugData.iblMultiplier;

    if (any(isnan(env)) || any(isinf(env))) env = vec3(0.0);

    payload.radiance += payload.throughput * env;
    payload.done = true;
}
