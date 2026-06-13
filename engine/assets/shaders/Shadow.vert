#version 460

// Depth-only pass from the shadow light's point of view; see ShadowStage.

struct GPUObjectData {
    mat4 modelMatrix;
    mat4 normalMatrix;
    uvec4 textureIndices;
    vec4 baseColor;
    vec4 materialFactors;
};

layout(location = 0) in vec3 inPosition;

layout(push_constant) uniform ShadowPush {
    mat4 lightViewProj;
} push;

layout(std430, set = 0, binding = 0) readonly buffer ObjectDataBuffer {
    GPUObjectData objects[];
} objectData;

void main() {
    gl_Position = push.lightViewProj * objectData.objects[gl_BaseInstance].modelMatrix * vec4(inPosition, 1.0);
}
