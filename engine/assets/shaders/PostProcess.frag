#version 460

struct DebugData {
    float irradianceMultiplier;
    float exposureMultiplier;
    uint viewMode;
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

layout(location = 0) in  vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform GlobalUbo {
    CameraData cameraData;
    DebugData  debugData;
} ubo;

layout(set = 1, binding = 0) uniform sampler2D hdrInput;
layout(set = 1, binding = 1) uniform usampler2D layers;
layout(set = 1, binding = 2) uniform sampler2D bloomInput;  // bloom_blurred, half res — GPU bilinear upscales

layout(push_constant) uniform PC {
    float bloomStrength;
    float vignetteStrength;
    uint  flags;            // bit 0 = vignette enabled, bit 1 = bloom enabled
} pc;

const uint VIEWMODE_CLAY         = 2u;
const uint VIEWMODE_UNLIT        = 1u;
const uint VIEWMODE_PATH_TRACING = 3u;

const uint FLAG_VIGNETTE = 1u << 0u;
const uint FLAG_BLOOM    = 1u << 1u;

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

void main() {
    vec3 hdr = texture(hdrInput, inUV).rgb;

    if (ubo.debugData.viewMode == VIEWMODE_CLAY ||
    ubo.debugData.viewMode == VIEWMODE_UNLIT) {
        outColor = vec4(hdr, 1.0);
        return;
    }

    // Bloom composite — add before tonemapping so it affects the curve correctly
    if ((pc.flags & FLAG_BLOOM) != 0u) {
        vec3 bloom = texture(bloomInput, inUV).rgb;
        hdr += bloom * pc.bloomStrength;
    }

    // Exposure
    vec3 color = max(hdr, vec3(0.0)) * ubo.debugData.exposureMultiplier;

    // Tonemap
    color = ACESFitted(color);

    // Vignette — after tonemap, subtle darkening at edges
    if ((pc.flags & FLAG_VIGNETTE) != 0u) {
        vec2  uv       = inUV - 0.5;
        float vignette = 1.0 - dot(uv, uv) * pc.vignetteStrength;
        color *= clamp(vignette, 0.0, 1.0);
    }

    outColor = vec4(color, 1.0);
}
