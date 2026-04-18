#version 460
#extension GL_EXT_nonuniform_qualifier : require

struct DebugData {
    float iblMultiplier;
    float exposureMultiplier;
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

struct GPUObjectData {
    mat4 modelMatrix;
    mat4 normalMatrix;
    uvec4 textureIndices;
    vec4 baseColor;
};

struct Light {
    uint  type;
    float intensity;
    float range;
    float innerConeAngle;
    vec3  color;
    float outerConeAngle;
    vec3  position;
    float width;
    vec3  direction;
    float height;
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
    DebugData debugData;
} ubo;

layout(set = 1, binding = 0) uniform samplerCube irradianceMap;
layout(set = 1, binding = 1) uniform samplerCube prefilterMap;
layout(set = 1, binding = 2) uniform sampler2D x1;
layout(set = 1, binding = 3) uniform sampler2D x2;
layout(set = 1, binding = 4) uniform sampler2D brdfLUT;

layout(set = 2, binding = 0) uniform sampler2D textures[];

layout(std430, set = 3, binding = 0) readonly buffer ObjectDataBuffer {
    GPUObjectData objects[];
} objectData;

layout(std430, set = 4, binding = 0) readonly buffer LightBuffer {
    Light lights[];
} lightData;

const float PI             = 3.14159265359;
const float INV_PI         = 0.31830988618;
const float EPSILON        = 1e-5;
const uint  MAX_LIGHTS     = 5u;
const float MAX_REFLECTION_LOD = 5.0;


// ---- BRDF ----

float D_GGX(float NdotH, float roughness) {
    float a  = roughness * roughness;
    float a2 = a * a;
    float d  = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

float V_SmithGGXCorrelated(float NdotV, float NdotL, float roughness) {
    float a  = roughness * roughness;
    float a2 = a * a;
    float v  = NdotL * sqrt(NdotV * NdotV * (1.0 - a2) + a2);
    float l  = NdotV * sqrt(NdotL * NdotL * (1.0 - a2) + a2);
    return 0.5 / max(v + l, EPSILON);
}

vec3 F_Schlick(float cosTheta, vec3 F0) {
    float f  = 1.0 - cosTheta;
    float f2 = f * f;
    return F0 + (1.0 - F0) * (f2 * f2 * f);
}

vec3 F_SchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    float f  = 1.0 - cosTheta;
    float f2 = f * f;
    vec3  r  = max(vec3(1.0 - roughness), F0);
    return F0 + (r - F0) * (f2 * f2 * f);
}


// ---- Attenuation ----

float distanceAttenuation(float dist, float range) {
    if (range <= 0.0)
    return 1.0 / max(dist * dist, EPSILON);
    float ratio  = dist / range;
    float ratio2 = ratio * ratio;
    float ratio4 = ratio2 * ratio2;
    float window = max(1.0 - ratio4, 0.0);
    return (window * window) / max(dist * dist, EPSILON);
}

float spotAttenuation(vec3 L, vec3 dir, float innerAngle, float outerAngle) {
    float cosOuter = cos(outerAngle);
    float cosInner = cos(innerAngle);
    float cosAngle = dot(dir, -L);
    return clamp((cosAngle - cosOuter) / max(cosInner - cosOuter, EPSILON), 0.0, 1.0);
}


// ---- Direct light evaluation ----

vec3 evaluateLight(Light light, vec3 N, vec3 V, vec3 worldPos, vec3 albedo, float metallic, float roughness, vec3 F0)
{
    if (light.intensity <= 0.0) return vec3(0.0);

    vec3  L;
    float atten;

    if (light.type == 1u) {
        L     = normalize(-light.direction);
        atten = 1.0;
    } else {
        vec3  toLight = light.position - worldPos;
        float dist    = length(toLight);
        L             = toLight / max(dist, EPSILON);
        atten         = distanceAttenuation(dist, light.range);
        if (light.type == 2u)
        atten *= spotAttenuation(L, normalize(light.direction),
        light.innerConeAngle, light.outerConeAngle);
    }

    float NdotL = dot(N, L);
    if (NdotL <= 0.0) return vec3(0.0);

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

    float intensityScale = (light.type == 0u) ? (1.0 / (4.0 * PI))
    : (light.type == 2u) ? (1.0 / PI)
    : 1.0;

    vec3 radiance = light.color * light.intensity * intensityScale * atten;
    return (diffuse + specular) * radiance * NdotL;
}


// ---- IBL — diffuse + prefiltered specular, no BRDF LUT ----

vec3 evaluateIBL(vec3 N, vec3 V, vec3 albedo, float metallic, float roughness, vec3 F0, float ao)
{
    float NdotV = max(dot(N, V), 0.0);

    // diffuse
    vec3 irradiance  = texture(irradianceMap, N).rgb;
    //irradiance       = irradiance / (irradiance + vec3(1.0));
    vec3 kS_ibl      = F_SchlickRoughness(NdotV, F0, roughness);
    vec3 kD_ibl      = (1.0 - kS_ibl) * (1.0 - metallic);
    vec3 diffuse_ibl = kD_ibl * irradiance * albedo * ao * ubo.debugData.iblMultiplier * 0.01;

    // specular — prefilter sample only, BRDF LUT correction done in post process
    vec3  R              = reflect(-V, N);
    float lod            = roughness * MAX_REFLECTION_LOD;
    vec3  prefilteredColor = textureLod(prefilterMap, R, lod).rgb;
    vec2 brdf = texture(brdfLUT, vec2(NdotV, roughness)).rg;
    vec3 specular_ibl = prefilteredColor * (F0 * brdf.x + brdf.y) * ubo.debugData.iblMultiplier * 0.01;

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

    // albedo
    vec4 albedoSample = texture(textures[nonuniformEXT(obj.textureIndices.x)], fragTexCoord);
    vec3 albedo       = pow(albedoSample.rgb, vec3(2.2)) * obj.baseColor.rgb;
    float alpha       = albedoSample.a * obj.baseColor.a;

    // normal
    vec3 N = perturbNormal(normalize(fragNormal), fragTangent,
    fragTexCoord, obj.textureIndices.y);

    // metallic-roughness
    float metallic  = 0.0;
    float roughness = 0.5;
    if (obj.textureIndices.z != 0u) {
        vec4 mr   = texture(textures[nonuniformEXT(obj.textureIndices.z)], fragTexCoord);
        roughness = mr.g;
        metallic  = mr.b;
    }
    roughness = clamp(roughness, 0.04, 1.0);

    // AO
    float ao = 1.0;
    if (obj.textureIndices.w != 0u)
    ao = texture(textures[nonuniformEXT(obj.textureIndices.w)], fragTexCoord).r;

    vec3 V  = normalize(ubo.cameraData.position - fragWorldPos);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    vec3 ambient = evaluateIBL(N, V, albedo, metallic, roughness, F0, ao);

    vec3 Lo = vec3(0.0);
    for (uint i = 0u; i < MAX_LIGHTS; i++)
    Lo += evaluateLight(lightData.lights[i], N, V, fragWorldPos,
    albedo, metallic, roughness, F0);

    outColor = vec4(ambient + Lo, alpha);
}