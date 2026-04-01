#version 460

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

struct GPUObjectData {
    mat4 modelMatrix;
    mat4 normalMatrix;
    uvec4 textureIndices;
    vec4 baseColor;
};

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec2 inTexCoord;
layout(location = 4) in vec4 inTangent;

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) out vec4 fragTangent;
layout(location = 4) flat out uint fragObjectIndex;
layout(location = 5) out vec3 fragColor;

layout(set = 0, binding = 0) uniform GlobalUbo {
    CameraData cameraData;
    vec4 ambientLightColor;
    vec3 lightPosition;
    float padding1;
    vec4 lightColor;
} ubo;

layout(std430, set = 3, binding = 0) readonly buffer ObjectDataBuffer {
    GPUObjectData objects[];
} objectData;

void main() {
    uint objectIndex = gl_BaseInstance;
    fragObjectIndex = objectIndex;

    GPUObjectData obj = objectData.objects[objectIndex];

    vec4 worldPosition = obj.modelMatrix * vec4(inPosition, 1.0);
    fragWorldPos = worldPosition.xyz;

    gl_Position = ubo.cameraData.viewProjection * worldPosition;

    fragNormal  = normalize(mat3(obj.normalMatrix) * inNormal);
    fragTangent = vec4(normalize(mat3(obj.modelMatrix) * inTangent.xyz), inTangent.w);

    fragTexCoord = inTexCoord;
    fragColor    = inColor;
}