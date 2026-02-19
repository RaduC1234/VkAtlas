#version 460
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in vec3 fragTangent;
layout(location = 4) in vec3 fragBitangent;
layout(location = 5) flat in uint fragObjectIndex;
layout(location = 6) in vec3 fragColor;

layout(location = 0) out vec4 outColor;

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
    vec4 ambientLightColor;
    vec3 lightPosition;
    float padding1;
    vec4 lightColor;
} ubo;

layout(set = 1, binding = 0) uniform sampler2D textures[];

struct GPUObjectData {
    mat4 modelMatrix;
    mat4 normalMatrix;
    uvec4 textureIndices;
    vec4 baseColor;
};

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

    uint albedoIndex = obj.textureIndices.x;
    vec4 albedo = texture(textures[nonuniformEXT(albedoIndex)], fragTexCoord) * obj.baseColor;

    outColor = vec4(linearToSRGB(albedo.xyz), 1.0);
}
