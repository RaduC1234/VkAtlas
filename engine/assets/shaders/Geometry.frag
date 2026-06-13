#version 460
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_GOOGLE_include_directive : require

#include "common/Types.glsl"
#include "common/RasterTypes.glsl"
#include "common/Constants.glsl"
#include "common/BRDF.glsl"
#include "common/Attenuation.glsl"

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in vec4 fragTangent;
layout(location = 4) flat in uint fragObjectIndex;
layout(location = 5) in vec3 fragColor;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform GlobalUbo {
    CameraData cameraData;
    DebugData  debugData;
} ubo;

layout(set = 1, binding = 0) uniform samplerCube irradianceMap;
layout(set = 1, binding = 1) uniform samplerCube prefilterMap;
layout(set = 1, binding = 2) uniform sampler2D   ltcMatLUT;
layout(set = 1, binding = 3) uniform sampler2D   ltcAmpLUT;
layout(set = 1, binding = 4) uniform sampler2D   brdfLUT;

layout(set = 2, binding = 0) uniform sampler2D textures[];

layout(std430, set = 3, binding = 0) readonly buffer ObjectDataBuffer {
    GPUObjectData objects[];
} objectData;

layout(std430, set = 4, binding = 0) readonly buffer LightBuffer {
    uint  count;
    uint  _pad[3];
    Light lights[];
} lightData;


// ---- Direct light evaluation ----

vec3 evaluateLight(Light light, vec3 N, vec3 V, vec3 worldPos, vec3 albedo, float metallic, float roughness, vec3 F0)
{
    if (light.intensity <= 0.0) return vec3(0.0);

    vec3  L;
    float atten;

    if (light.type == LIGHT_TYPE_DIRECTIONAL) {
        L     = normalize(-light.direction);
        atten = 1.0;
    } else {
        vec3  toLight = light.position - worldPos;
        float dist    = length(toLight);
        L             = toLight / max(dist, EPSILON);
        if (light.type == LIGHT_TYPE_RECT) {
            vec3  lightNormal = normalize(light.direction);
            float rectArea    = max(light.width * light.height, EPSILON);
            float cosLight    = max(dot(lightNormal, -L), 0.0);
            atten             = rectArea * cosLight / max(dist * dist, EPSILON);
        } else {
            atten = distanceAttenuation(dist, light.range);
            if (light.type == LIGHT_TYPE_SPOT)
                atten *= spotAttenuation(L, normalize(light.direction),
                                        light.innerConeAngle, light.outerConeAngle);
        }
    }

    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), EPSILON);
    vec3  H     = normalize(V + L);
    float NdotH = max(dot(N, H), 0.0);
    float HdotV = max(dot(H, V), 0.0);

    float D   = D_GGX(NdotH, roughness);
    float Vis = V_SmithGGXCorrelated(NdotV, NdotL, roughness);
    vec3  F   = F_Schlick(HdotV, F0);

    vec3 specular = D * Vis * F;
    vec3 kD       = (1.0 - F) * (1.0 - metallic);
    vec3 diffuse  = kD * albedo * INV_PI;

    vec3 radiance = light.color * light.intensity * atten;
    return (diffuse + specular) * radiance * NdotL;
}


// ---- IBL ----

vec3 evaluateIBL(vec3 N, vec3 V, vec3 albedo, float metallic, float roughness, vec3 F0, float ao)
{
    float NdotV = max(dot(N, V), 0.0);

    vec3 irradiance  = texture(irradianceMap, N).rgb;
    vec3 kS_ibl      = F_SchlickRoughness(NdotV, F0, roughness);
    vec3 kD_ibl      = (1.0 - kS_ibl) * (1.0 - metallic);
    vec3 diffuse_ibl = kD_ibl * irradiance * albedo * ao * ubo.debugData.iblMultiplier;

    vec3  R               = reflect(-V, N);
    float lod             = roughness * MAX_REFLECTION_LOD;
    vec3  prefilteredColor = textureLod(prefilterMap, R, lod).rgb;
    vec2  brdf            = texture(brdfLUT, vec2(NdotV, roughness)).rg;
    if (brdf.x > 0.999 && brdf.y > 0.999) brdf = vec2(1.0, 0.0);
    vec3  specular_ibl    = prefilteredColor * (F0 * brdf.x + brdf.y) * ubo.debugData.iblMultiplier;

    return diffuse_ibl + specular_ibl;
}


// ---- Normal mapping ----

vec3 perturbNormal(vec3 N, vec4 tangent, vec2 uv, uint texIdx) {
    if (texIdx == 0u) return N;
    vec3 ts = texture(textures[nonuniformEXT(texIdx)], uv).rgb * 2.0 - 1.0;
    vec3 T  = normalize(tangent.xyz);
    T       = normalize(T - dot(T, N) * N);
    vec3 B  = cross(N, T) * tangent.w;
    return normalize(mat3(T, B, N) * ts);
}


// ---- Main ----

void main() {
    GPUObjectData obj = objectData.objects[fragObjectIndex];

    vec4 albedoSample = obj.textureIndices.x == 0u
        ? vec4(1.0)
        : texture(textures[nonuniformEXT(obj.textureIndices.x)], fragTexCoord);
    vec3  albedo = albedoSample.rgb * obj.baseColor.rgb * fragColor;
    float alpha  = albedoSample.a * obj.baseColor.a;

    if (ubo.debugData.viewMode == VIEWMODE_UNLIT) {
        outColor = vec4(albedo.rgb, 1.0);
        return;
    }

    vec3 V = normalize(ubo.cameraData.position - fragWorldPos);

    vec3 baseNormal = normalize(fragNormal);
    vec3 N = perturbNormal(baseNormal, fragTangent, fragTexCoord, obj.textureIndices.y);
    N = faceforward(N, -V, N);

    float metallic  = obj.materialFactors.x;
    float roughness = obj.materialFactors.y;
    if (obj.textureIndices.z != 0u) {
        vec4 mr   = texture(textures[nonuniformEXT(obj.textureIndices.z)], fragTexCoord);
        roughness *= mr.g;
        metallic  *= mr.b;
    }
    roughness = clamp(roughness, 0.04, 1.0);

    float ao = 1.0;
    if (obj.textureIndices.w != 0u)
        ao = texture(textures[nonuniformEXT(obj.textureIndices.w)], fragTexCoord).r;

    if (ubo.debugData.viewMode == VIEWMODE_CLAY) {
        const vec3  clayAlbedo    = vec3(0.8);
        const float clayMetallic  = 0.0;
        const float clayRoughness = 0.7;
        vec3 clayF0 = vec3(0.04);

        vec3 LoClay = vec3(0.0);
        uint lightCount = min(lightData.count, MAX_LIGHT_COUNT);
        for (uint i = 0u; i < lightCount; i++) {
            LoClay += evaluateLight(lightData.lights[i], N, V, fragWorldPos, clayAlbedo, clayMetallic, clayRoughness, clayF0);
        }

        outColor = vec4(evaluateIBL(N, V, clayAlbedo, clayMetallic, clayRoughness, clayF0, ao) + LoClay, alpha);
        return;
    }

    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    vec3 Lo = vec3(0.0);
    uint lightCount = min(lightData.count, MAX_LIGHT_COUNT);
    for (uint i = 0u; i < lightCount; i++) {
        Lo += evaluateLight(lightData.lights[i], N, V, fragWorldPos, albedo, metallic, roughness, F0);
    }

    outColor = vec4(evaluateIBL(N, V, albedo, metallic, roughness, F0, ao) + Lo, alpha);
}
