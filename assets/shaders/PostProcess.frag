#version 460

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0) uniform sampler2D hdrInput;
layout(set = 1, binding = 1) uniform sampler2D BRDF_LUT;
layout(set = 1, binding = 2) uniform usampler2D stencil;

// ---- Tone mapping ----

vec3 ACESFitted(vec3 color) {
    const mat3 input_mat = mat3(
    0.59719, 0.07600, 0.02840,
    0.35458, 0.90834, 0.13383,
    0.04823, 0.01566, 0.83777
    );
    const mat3 output_mat = mat3(
    1.60475, -0.10208, -0.00327,
    -0.53108,  1.10813, -0.07276,
    -0.07367, -0.00605,  1.07602
    );
    color = input_mat * color;
    vec3 a = color * (color + 0.0245786) - 0.000090537;
    vec3 b = color * (0.983729 * color + 0.4329510) + 0.238081;
    return clamp(output_mat * (a / b), 0.0, 1.0);
}

vec3 linearToSRGB(vec3 c) {
    return mix(
    1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055,
    c * 12.92,
    lessThanEqual(c, vec3(0.0031308))
    );
}


void main() {
    vec3 hdr = texture(hdrInput, inUV).rgb;
    uint layer = texture(stencil, inUV).r;

    if(layer == 0u) {
        outColor = vec4(hdr, 1.0);
        return;
    }

    //hdr *= /*ubo.exposure;*/ 0.3;
    hdr = ACESFitted(hdr);
    hdr = linearToSRGB(hdr);

    outColor = vec4(hdr, 1.0);
}