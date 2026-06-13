#version 450
#extension GL_GOOGLE_include_directive : require

#include "common/Types.glsl"
#include "common/Constants.glsl"

layout(location = 0) in  vec3 fragTexCoord;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform GlobalUbo {
    CameraData cameraData;
    DebugData  debugData;
} ubo;

layout(set = 5, binding = 0) uniform samplerCube skyboxSampler;

void main() {
    if (ubo.debugData.viewMode == VIEWMODE_CLAY) {
        outColor = vec4(1.0);
        return;
    }
    outColor = texture(skyboxSampler, fragTexCoord);
}
