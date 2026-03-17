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
    uint  type;
    float intensity;
    float range;
    float innerConeAngle;
    vec3  color;
    float outerConeAngle;
    vec3  position;
    float _pad0;
    vec3  direction;
    float _pad1;
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
    Light lights[5];
} lightData;

const float PI      = 3.14159265359;
const float INV_PI  = 0.31830988618;
const float EPSILON = 1e-5;
const uint  MAX_LIGHTS = 5u;


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

vec3 F_Schlick(float HdotV, vec3 F0) {
    float f  = 1.0 - HdotV;
    float f2 = f * f;
    return F0 + (1.0 - F0) * (f2 * f2 * f);
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


// ---- Light evaluation ----

vec3 evaluateLight(
Light light,
vec3 N, vec3 V, vec3 worldPos,
vec3 albedo, float metallic, float roughness, vec3 F0)
{
    if (light.intensity <= 0.0) return vec3(0.0);

    vec3  toLight = light.position - worldPos;
    float dist    = length(toLight);
    vec3  L       = toLight / max(dist, EPSILON);

    float atten = distanceAttenuation(dist, light.range);

    if (light.type == 2u) // spot
    atten *= spotAttenuation(L, normalize(light.direction), light.innerConeAngle, light.outerConeAngle);

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

    vec3 radiance = light.color * light.intensity * atten;
    return (diffuse + specular) * radiance * NdotL;
}


// ---- Normal mapping ----

vec3 perturbNormal(vec3 N, vec4 tangent, vec2 uv, uint texIdx) {
    if (texIdx == 0u) return N;

    vec3 ts = texture(textures[nonuniformEXT(texIdx)], uv).rgb * 2.0 - 1.0;

    vec3 T = normalize(tangent.xyz);
    T      = normalize(T - dot(T, N) * N);
    vec3 B = cross(N, T) * tangent.w;

    return normalize(mat3(T, B, N) * ts);
}


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


// ---- Main ----

void main() {
    GPUObjectData obj = objectData.objects[fragObjectIndex];

    // Albedo
    vec4 albedoSample = texture(textures[nonuniformEXT(obj.textureIndices.x)], fragTexCoord);
    vec3 albedo = pow(albedoSample.rgb, vec3(2.2)) * obj.baseColor.rgb;
    float alpha = albedoSample.a * obj.baseColor.a;

    // Normal
    vec3 N = perturbNormal(
    normalize(fragNormal),
    fragTangent,
    fragTexCoord,
    obj.textureIndices.y
    );

    // Metallic-roughness
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

    // Setup
    vec3 V  = normalize(ubo.cameraData.position - fragWorldPos);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    // Ambient
    vec3 ambient = ubo.ambientLightColor.rgb * ubo.ambientLightColor.a
    * albedo * (1.0 - metallic) * ao;

    // Direct lighting
    vec3 Lo = vec3(0.0);
    for (uint i = 0u; i < MAX_LIGHTS; i++) {
        Lo += evaluateLight(
        lightData.lights[i],
        N, V, fragWorldPos,
        albedo, metallic, roughness, F0
        );
    }

    // Final
    vec3 color = ambient + Lo;
    color = ACESFitted(color);
    color = linearToSRGB(color);

    outColor = vec4(color, alpha);
}