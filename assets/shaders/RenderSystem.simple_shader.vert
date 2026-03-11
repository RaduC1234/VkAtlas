#version 450

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

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 color;
layout(location = 2) in vec3 normal;
layout(location = 3) in vec2 uv;
layout(location = 4) in vec4 tangent;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec3 fragPosWorld;
layout(location = 2) out vec3 fragNormalWorld;
layout(location = 3) out vec2 fragUV;

layout(set = 0, binding = 0) uniform GlobalUbo {
    CameraData cameraData;
    vec4 ambientLightColor;// w is intensity
    vec3 lightPosition;
    float padding1;
    vec4 lightColor;
} ubo;

layout(push_constant) uniform Push {
    mat4 modelMatrix;
    mat4 normalMatrix;
    vec4 baseColor;
    uvec4 texturesIndexes;// alberto,  normal, metallicRoughness
} push;

void main() {
    vec4 positionWorld = push.modelMatrix * vec4(position, 1.0);
    gl_Position = ubo.cameraData.viewProjection * positionWorld;

    fragNormalWorld = normalize(mat3(push.normalMatrix) * normal);
    fragPosWorld = positionWorld.xyz;
    fragColor = color;
    fragUV = uv;
}
