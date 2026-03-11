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

struct Light {
    uint type;
    float intensity;
    float range;
    float innerConeAngle;
    vec3 color;
    float outerConeAngle;
    vec3 position;
    float padding;
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

layout(std430, set = 3, binding = 0) readonly buffer LightBuffer {
    Light lights[];
} lightData;

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

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    return GeometrySchlickGGX(max(dot(N, V), 0.0), roughness) * GeometrySchlickGGX(max(dot(N, L), 0.0), roughness);
}

vec3 FresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 calcLightContribution(
vec3 N, vec3 V, vec3 L,
vec3 radiance,
vec3 albedo, float metallic, float roughness)
{
    vec3 H = normalize(V + L);

    vec3  F0 = mix(vec3(0.04), albedo, metallic);
    vec3  F  = FresnelSchlick(max(dot(H, V), 0.0), F0);
    float D  = DistributionGGX(N, H, roughness);
    float G  = GeometrySmith(N, V, L, roughness);

    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);

    vec3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 1e-4);
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
    vec3 diffuse = kD * albedo / PI;

    return (diffuse + specular) * radiance * NdotL;
}

const int STUDIO_LIGHT_COUNT = 6;
const vec3 studioLightDirs[6] = vec3[6](
normalize(vec3( 0.0,  1.0,  0.0)),
normalize(vec3( 0.5,  0.3,  1.0)),
normalize(vec3(-0.5,  0.3,  1.0)),
normalize(vec3( 0.0,  0.3, -1.0)),
normalize(vec3( 1.0, -0.2,  0.0)),
normalize(vec3( 0.0, -1.0,  0.0))
);
const vec3 studioLightColors[6] = vec3[6](
vec3(0.80, 0.80, 0.80) * 0.8,
vec3(0.90, 0.90, 1.00) * 0.6,
vec3(1.00, 0.95, 0.90) * 0.5,
vec3(0.65, 0.70, 0.75) * 0.5,
vec3(0.70, 0.72, 0.75) * 0.4,
vec3(0.50, 0.50, 0.55) * 0.3
);

void main() {
    GPUObjectData obj = objectData.objects[fragObjectIndex];

    // Albedo
    uint albedoIndex = obj.textureIndices.x;
    vec4 albedoSample = texture(textures[nonuniformEXT(albedoIndex)], fragTexCoord) * obj.baseColor;
    vec3 albedo = albedoSample.rgb;

    // Geometric normal for hemisphere ambient
    vec3 N_geom = normalize(fragNormal);

    // Normal mapping with interpolated tangent + handedness
    uint normalIndex = obj.textureIndices.y;
    vec3 N;
    if (normalIndex != 0u) {
        vec3 normalSample = texture(textures[nonuniformEXT(normalIndex)], fragTexCoord).rgb;
        normalSample = normalSample * 2.0 - 1.0;

        vec3 T = normalize(fragTangent.xyz);
        T = normalize(T - dot(T, N_geom) * N_geom);        // Gram-Schmidt
        vec3 B = cross(N_geom, T) * fragTangent.w;          // handedness
        mat3 TBN = mat3(T, B, N_geom);

        N = normalize(TBN * normalSample);
    } else {
        N = N_geom;
    }

    // Metallic / Roughness
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

    // Hemisphere ambient uses geometric normal
    float hemisphereBlend = N_geom.y * 0.5 + 0.5;
    vec3 ambient = mix(vec3(0.35), vec3(0.55), hemisphereBlend) * albedo;

    // BRDF uses normal-mapped N
    vec3 Lo = vec3(0.0);
    for (int i = 0; i < STUDIO_LIGHT_COUNT; i++) {
        Lo += calcLightContribution(N, V, studioLightDirs[i], studioLightColors[i], albedo, metallic, roughness);
    }

    vec3 color = ambient + Lo;

    // Filmic ACES
    color = color * (2.51 * color + 0.03) / (color * (2.43 * color + 0.59) + 0.14);
    color = clamp(color, 0.0, 1.0);

    outColor = vec4(linearToSRGB(color), albedoSample.a);
}