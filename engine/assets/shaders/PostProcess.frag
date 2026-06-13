#version 460
#extension GL_GOOGLE_include_directive : require

#include "common/Types.glsl"
#include "common/Constants.glsl"
#include "common/ToneMapping.glsl"

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform GlobalUbo {
    CameraData cameraData;
    DebugData  debugData;
} ubo;

layout(set = 1, binding = 0) uniform sampler2D  hdrInput;
layout(set = 1, binding = 1) uniform usampler2D layers;
layout(set = 1, binding = 2) uniform sampler2D  bloomInput;

layout(push_constant) uniform PC {
    vec4  colorTint;
    float exposure;
    float contrast;
    float saturation;
    float bloomStrength;
    float vignetteStrength;
    uint  flags;
} pc;

const uint FLAG_VIGNETTE = 1u << 0u;
const uint FLAG_BLOOM    = 1u << 1u;
const uint FLAG_ACES     = 1u << 2u;

void main() {
    vec3 hdr = texture(hdrInput, inUV).rgb;

    if (ubo.debugData.viewMode == VIEWMODE_CLAY ||
        ubo.debugData.viewMode == VIEWMODE_UNLIT) {
        outColor = vec4(hdr, 1.0);
        return;
    }

    if ((pc.flags & FLAG_BLOOM) != 0u)
        hdr += texture(bloomInput, inUV).rgb * pc.bloomStrength;

    vec3 color = max(hdr, vec3(0.0)) * pc.exposure;

    if ((pc.flags & FLAG_ACES) != 0u)
        color = ACESFitted(color);

    color *= pc.colorTint.rgb;
    color  = mix(vec3(0.5), color, pc.contrast);
    color  = mix(vec3(luminance(color)), color, pc.saturation);

    if ((pc.flags & FLAG_VIGNETTE) != 0u) {
        vec2  uv       = inUV - 0.5;
        float vignette = 1.0 - dot(uv, uv) * pc.vignetteStrength;
        color *= clamp(vignette, 0.0, 1.0);
    }

    outColor = vec4(color, 1.0);
}
