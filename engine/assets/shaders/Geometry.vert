#version 460
#extension GL_GOOGLE_include_directive : require

#include "common/Types.glsl"
#include "common/RasterTypes.glsl"

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
    DebugData  debugData;
} ubo;

layout(std430, set = 3, binding = 0) readonly buffer ObjectDataBuffer {
    GPUObjectData objects[];
} objectData;

void main() {
    uint objectIndex = gl_BaseInstance;
    fragObjectIndex  = objectIndex;

    GPUObjectData obj = objectData.objects[objectIndex];

    vec4 worldPosition = obj.modelMatrix * vec4(inPosition, 1.0);
    fragWorldPos = worldPosition.xyz;

    gl_Position = ubo.cameraData.viewProjection * worldPosition;

    fragNormal  = normalize(mat3(obj.normalMatrix) * inNormal);
    fragTangent = vec4(normalize(mat3(obj.modelMatrix) * inTangent.xyz), inTangent.w);

    fragTexCoord = inTexCoord;
    fragColor    = inColor;
}
