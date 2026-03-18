#version 460
#extension GL_EXT_nonuniform_qualifier : require

struct CameraData {
    mat4 projection[2];
    mat4 view[2];
    mat4 viewProjection[2];
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

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in vec4 fragTangent;
layout(location = 4) flat in uint fragObjectIndex;
layout(location = 5) in vec3 fragColor;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform GlobalUbo {
    CameraData cameraData;
    vec4 ambientLightColor;
    vec3 lightPosition;
    float padding1;
    vec4 lightColor;
} ubo;

layout(set = 1, binding = 0) uniform sampler2D textures[];

layout(std430, set = 2, binding = 0) readonly buffer ObjectDataBuffer {
    GPUObjectData objects[];
} objectData;

vec3 linearToSRGB(vec3 color) {
    vec3 a = 1.055 * pow(color, vec3(1.0 / 2.4)) - 0.055;
    vec3 b = color * 12.92;
    return mix(a, b, lessThanEqual(color, vec3(0.0031308)));
}

void main() {
    GPUObjectData obj = objectData.objects[fragObjectIndex];

    // Albedo
    uint albedoIndex = obj.textureIndices.x;
    vec4 albedoSample = texture(textures[nonuniformEXT(albedoIndex)], fragTexCoord) * obj.baseColor;

    vec3 color = clamp(albedoSample.rgb, 0.0, 1.0);
    outColor = vec4(linearToSRGB(color), albedoSample.a);
}