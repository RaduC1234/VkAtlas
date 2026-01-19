#version 450
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragPosWorld;
layout(location = 2) in vec3 fragNormalWorld;
layout(location = 3) in vec2 fragUV;

layout(push_constant) uniform Push {
    mat4 modelMatrix;
    mat4 normalMatrix;
    vec4 baseColor;
    ivec4 texturesIndexes;// alberto,  normal, metallicRoughness
} push;

layout(set = 0, binding = 0) uniform GlobalUbo {
    mat4 projectionMatrix;
    mat4 viewMatrix;
    vec4 ambientLightColor;// w is intensity
    vec3 lightPosition;
    float padding1;
    vec4 lightColor;
} ubo;

// Bindless texture array
layout(set = 1, binding = 0) uniform sampler2D textures[];

layout(location = 0) out vec4 outColor;

vec3 linearToSRGB(vec3 color) {
    vec3 a = 1.055 * pow(color, vec3(1.0 / 2.4)) - 0.055;
    vec3 b = color * 12.92;
    return mix(a, b, lessThanEqual(color, vec3(0.0031308)));
}

void main() {

    vec3 directionToLight = ubo.lightPosition - fragPosWorld;
    float attenution = 1.0 / dot(directionToLight, directionToLight);

    // Compute diffuse lighting
    vec3 lightColor = ubo.lightColor.rgb * ubo.lightColor.rgb * attenution;
    vec3 ambientLight = ubo.ambientLightColor.rgb * ubo.ambientLightColor.w;
    vec3 diffuseLight = lightColor * max(dot(normalize(fragNormalWorld), normalize(directionToLight)), 0.0);

    vec3 texColor = texture(textures[nonuniformEXT(push.texturesIndexes.x)], fragUV).rgb;

    // Combine texture with vertex color and lighting
    vec3 finalColor = texColor * fragColor * push.baseColor.rgb;
    vec3 srgbColor = linearToSRGB((diffuseLight + ambientLight) * finalColor);
    outColor = vec4(srgbColor, 1.0);
}