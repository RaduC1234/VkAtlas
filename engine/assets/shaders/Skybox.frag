#version 450

layout(location = 0) in vec3 fragTexCoord;

layout(location = 0) out vec4 outColor;

struct DebugData {
    float iblMultiplier;
    float exposureMultiplier;
    uint viewMode; // 0 LIT, 1 UNLIT, 2 CLAY
};

struct CameraData {
    mat4 projection;
    mat4 view;
    mat4 viewProjection;
    vec4 frustumPlanes[6];
    vec3 position;
    float nearPlane;
    vec3 direction;
    float farPlane;
};

layout(set = 0, binding = 0) uniform GlobalUbo {
    CameraData cameraData;
    DebugData debugData;
} ubo;

const uint VIEWMODE_CLAY = 2u;

layout(set = 5, binding = 0) uniform samplerCube skyboxSampler;

void main() {
    if (ubo.debugData.viewMode == VIEWMODE_CLAY) {
        outColor = vec4(1.0);
        return;
    }

    outColor = texture(skyboxSampler, fragTexCoord);
}
