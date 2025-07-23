#version 450

layout(location = 0) in vec3 fragColor;

layout(push_constant) uniform Push {
    mat4 transform;
    vec3 color;
} push;

layout(location = 0) out vec4 outColor;

vec3 linearToSRGB(vec3 color) {
    vec3 a = 1.055 * pow(color, vec3(1.0 / 2.4)) - 0.055;
    vec3 b = color * 12.92;
    return mix(a, b, lessThanEqual(color, vec3(0.0031308)));
}

void main() {
    vec3 srgbColor = linearToSRGB(fragColor);
    outColor = vec4(srgbColor, 1.0);
}

