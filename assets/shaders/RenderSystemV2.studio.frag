#version 460
#extension GL_EXT_nonuniform_qualifier : require

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

const float PI = 3.14159265359;

vec3 linearToSRGB(vec3 color) {
    vec3 a = 1.055 * pow(color, vec3(1.0 / 2.4)) - 0.055;
    vec3 b = color * 12.92;
    return mix(a, b, lessThanEqual(color, vec3(0.0031308)));
}

float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a  = roughness * roughness;
    float a2 = a * a;
    float NdotH  = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom  = (NdotH2 * (a2 - 1.0) + 1.0);
    return a2 / (PI * denom * denom);
}

float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

vec3 FresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 FresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Completely artifact-free light contribution.
// NO max(NdotL, 0) anywhere. Everything uses smooth remaps.
vec3 calcLightContribution(
vec3 N, vec3 V, vec3 L,
vec3 radiance,
vec3 albedo, float metallic, float roughness)
{
    float NdotL_raw = dot(N, L);

    // Smooth falloff: maps [-1,1] to [0,1] with C1 continuity everywhere
    float fade = smoothstep(-1.0, 1.0, NdotL_raw);

    // Early out — if fade is near zero, skip expensive BRDF math
    if (fade < 0.001) return vec3(0.0);

    vec3 H = normalize(V + L);
    float NdotV = max(dot(N, V), 0.001);
    float HdotV = max(dot(H, V), 0.0);

    vec3  F0 = mix(vec3(0.04), albedo, metallic);
    vec3  F  = FresnelSchlick(HdotV, F0);

    // For D and G, use the smooth fade value remapped back to a "safe" NdotL
    // This avoids any discontinuity in the specular lobe
    float safeNdotL = max(fade * fade, 0.001); // fade^2 gives a nice soft ramp

    float D = DistributionGGX(N, H, roughness);

    // Simplified geometry term using the smooth value
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    float G_V = NdotV / (NdotV * (1.0 - k) + k);
    float G_L = safeNdotL / (safeNdotL * (1.0 - k) + k);
    float G = G_V * G_L;

    vec3 specular = (D * G * F) / max(4.0 * NdotV * safeNdotL, 0.001);

    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
    vec3 diffuse = kD * albedo / PI;

    // Single smooth fade applied to everything
    return (diffuse + specular) * radiance * fade;
}

// ---------- Studio lighting ----------

const int STUDIO_LIGHT_COUNT = 6;

const vec3 studioLightDirs[6] = vec3[6](
normalize(vec3( 0.5,  0.7,  0.5)),
normalize(vec3(-0.5,  0.5,  0.7)),
normalize(vec3(-0.7,  0.3, -0.5)),
normalize(vec3( 0.7,  0.3, -0.5)),
normalize(vec3( 0.1,  1.0,  0.1)),
normalize(vec3( 0.0, -0.3,  1.0))
);

const vec3 studioLightColors[6] = vec3[6](
vec3(1.00, 0.95, 0.88) * 2.2,   // key - warm, strong
vec3(0.85, 0.88, 0.95) * 1.3,   // fill - cool
vec3(0.90, 0.85, 0.80) * 0.9,   // back left
vec3(0.85, 0.88, 0.90) * 0.9,   // back right
vec3(0.95, 0.95, 0.95) * 1.0,   // top
vec3(0.85, 0.75, 0.60) * 0.6    // ground bounce
);

void main() {
    GPUObjectData obj = objectData.objects[fragObjectIndex];

    // ---- Albedo ----
    uint albedoIndex = obj.textureIndices.x;
    vec4 albedoSample = texture(textures[nonuniformEXT(albedoIndex)], fragTexCoord) * obj.baseColor;
    vec3 albedo = albedoSample.rgb;

    // ---- Normal mapping ----
    vec3 N_geom = normalize(fragNormal);

    uint normalIndex = obj.textureIndices.y;
    vec3 N;
    if (normalIndex != 0u) {
        vec3 normalSample = texture(textures[nonuniformEXT(normalIndex)], fragTexCoord).rgb;
        normalSample = normalSample * 2.0 - 1.0;

        vec3 T = normalize(fragTangent.xyz);
        T = normalize(T - dot(T, N_geom) * N_geom);
        vec3 B = cross(N_geom, T) * fragTangent.w;
        mat3 TBN = mat3(T, B, N_geom);

        N = normalize(TBN * normalSample);
    } else {
        N = N_geom;
    }

    // ---- Metallic / Roughness ----
    uint mrIndex = obj.textureIndices.z;
    float metallic  = 0.0;
    float roughness = 0.5;
    if (mrIndex != 0u) {
        vec4 mrSample = texture(textures[nonuniformEXT(mrIndex)], fragTexCoord);
        roughness = mrSample.g;
        metallic  = mrSample.b;
    }
    roughness = clamp(roughness, 0.04, 1.0);

    vec3 V = normalize(ubo.cameraData.position - fragWorldPos);
    vec3 R = reflect(-V, N);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    // ---- Ambient (strong, artifact-free) ----
    float NdotV = max(dot(N, V), 0.0);
    vec3 kS_amb = FresnelSchlickRoughness(NdotV, F0, roughness);
    vec3 kD_amb = (1.0 - kS_amb) * (1.0 - metallic);

    // Hemisphere blend using geometric normal (smooth across mesh)
    float upBlend = N_geom.y * 0.5 + 0.5;
    vec3 ambientTint = mix(
    vec3(0.55, 0.45, 0.35),  // warm floor bounce
    vec3(0.60, 0.60, 0.62),  // cool ceiling
    upBlend
    );

    vec3 ambient = kD_amb * albedo * ambientTint * 1.2;

    // Specular ambient for shiny surfaces
    float reflUpBlend = R.y * 0.5 + 0.5;
    vec3 envReflection = mix(vec3(0.4, 0.33, 0.25), vec3(0.5, 0.5, 0.52), reflUpBlend);
    float specStr = mix(0.5, 0.03, roughness * roughness);
    ambient += kS_amb * envReflection * specStr;

    // ---- Direct lighting ----
    vec3 Lo = vec3(0.0);
    for (int i = 0; i < STUDIO_LIGHT_COUNT; i++) {
        Lo += calcLightContribution(
        N, V, studioLightDirs[i],
        studioLightColors[i],
        albedo, metallic, roughness
        );
    }

    vec3 color = ambient + Lo;

    // ---- ACES tone mapping ----
    float exposure = 1.3;
    vec3 x = color * exposure;
    color = (x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14);
    color = clamp(color, 0.0, 1.0);

    outColor = vec4(linearToSRGB(color), albedoSample.a);
}