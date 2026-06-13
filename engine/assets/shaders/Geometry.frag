#version 460
#extension GL_EXT_nonuniform_qualifier : require

struct DebugData {
    float iblMultiplier;
    float exposureMultiplier;
    uint viewMode; // 0 LIT, 1 UNLIT, 2 CLAY
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
    vec4 materialFactors;
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
    vec3  right;
    float _pad;
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
layout(set = 1, binding = 2) uniform sampler2D ltcMatLUT;
layout(set = 1, binding = 3) uniform sampler2D ltcAmpLUT;
layout(set = 1, binding = 4) uniform sampler2D brdfLUT;

layout(set = 2, binding = 0) uniform sampler2D textures[];

layout(std430, set = 3, binding = 0) readonly buffer ObjectDataBuffer {
    GPUObjectData objects[];
} objectData;

layout(std430, set = 4, binding = 0) readonly buffer LightBuffer {
    uint  count;            // total lights written by CPU, directional first
    uint  directionalCount; // evaluated globally, never clustered
    uint  _pad0;
    uint  _pad1;
    Light lights[];
} lightData;

// Per-froxel light lists built by LightCluster.comp; constants are mirrored
// in LightClusterStage.hpp and LightCluster.comp.
const uvec3 CLUSTER_GRID = uvec3(16, 9, 24);
const uint MAX_LIGHTS_PER_CLUSTER = 63u;

struct Cluster {
    uint count;
    uint indices[MAX_LIGHTS_PER_CLUSTER];
};

layout(std430, set = 4, binding = 1) readonly buffer ClusterBuffer {
    Cluster clusters[];
} clusterData;

// Directional shadow map; layout mirrored by ShadowStage::ShadowData.
layout(std430, set = 4, binding = 2) readonly buffer ShadowDataBuffer {
    mat4 lightViewProj;
    uint lightIndex;
    uint enabled;
    float texelSize;
    float _pad;
} shadowData;

layout(set = 4, binding = 3) uniform sampler2DShadow shadowMap;

layout(push_constant) uniform ClusterPush {
    vec2 tileScale; // CLUSTER_GRID.xy / framebuffer size
} push;

const float PI             = 3.14159265359;
const float INV_PI         = 0.31830988618;
const float EPSILON        = 1e-5;
const float MAX_REFLECTION_LOD = 5.0;

// Light types (Light.type)
const uint LIGHT_TYPE_POINT       = 1u;
const uint LIGHT_TYPE_SPOT        = 2u;
const uint LIGHT_TYPE_DIRECTIONAL = 3u;
const uint LIGHT_TYPE_RECT        = 4u;

// Debug view modes (debugData.viewMode)
const uint VIEWMODE_LIT   = 0u;
const uint VIEWMODE_UNLIT = 1u;
const uint VIEWMODE_CLAY  = 2u;


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


// ---- Punctual light evaluation (point / spot / directional) ----

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
        atten         = distanceAttenuation(dist, light.range);
        if (light.type == LIGHT_TYPE_SPOT)
        atten *= spotAttenuation(L, normalize(light.direction),
        light.innerConeAngle, light.outerConeAngle);
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


// ---- Rect area lights (Heitz et al., linearly transformed cosines) ----

vec3 ltcIntegrateEdge(vec3 v1, vec3 v2) {
    float x = dot(v1, v2);
    float y = abs(x);
    float a = 0.8543985 + (0.4965155 + 0.0145206 * y) * y;
    float b = 3.4175940 + (4.1616724 + y) * y;
    float v = a / b;
    float thetaSinTheta = x > 0.0 ? v : 0.5 * inversesqrt(max(1.0 - x * x, 1e-7)) - v;
    return cross(v1, v2) * thetaSinTheta;
}

float ltcEvaluate(vec3 N, vec3 V, vec3 P, mat3 Minv, vec3 p0, vec3 p1, vec3 p2, vec3 p3) {
    // transform the polygon into the cosine space defined by Minv around (T1, T2, N)
    vec3 T1 = normalize(V - N * dot(V, N));
    vec3 T2 = cross(N, T1);
    Minv = Minv * transpose(mat3(T1, T2, N));

    vec3 L0 = normalize(Minv * (p0 - P));
    vec3 L1 = normalize(Minv * (p1 - P));
    vec3 L2 = normalize(Minv * (p2 - P));
    vec3 L3 = normalize(Minv * (p3 - P));

    vec3 vsum = ltcIntegrateEdge(L0, L1)
              + ltcIntegrateEdge(L1, L2)
              + ltcIntegrateEdge(L2, L3)
              + ltcIntegrateEdge(L3, L0);

    // abs() keeps the integral winding-independent; one-sidedness is handled
    // by the caller through the light plane test.
    return abs(vsum.z);
}

vec3 evaluateRectLight(Light light, vec3 N, vec3 V, vec3 P, vec3 albedo, float metallic, float roughness, vec3 F0)
{
    if (light.intensity <= 0.0 || light.width <= 0.0 || light.height <= 0.0) return vec3(0.0);

    // the rect emits along its normal; points behind it receive nothing
    vec3 n = normalize(light.direction);
    if (dot(P - light.position, n) <= 0.0) return vec3(0.0);

    vec3 ex = light.right * (0.5 * light.width);
    vec3 ey = cross(n, light.right) * (0.5 * light.height);
    vec3 p0 = light.position - ex - ey;
    vec3 p1 = light.position + ex - ey;
    vec3 p2 = light.position + ex + ey;
    vec3 p3 = light.position - ex + ey;

    // LUT layout from scripts/generate_ltc.py: u = roughness, v = 1 - theta / (pi/2)
    float NdotV = clamp(dot(N, V), EPSILON, 1.0);
    vec2 uv = vec2(roughness, 1.0 - acos(NdotV) * (2.0 * INV_PI));
    vec4 t1 = texture(ltcMatLUT, uv);
    vec2 t2 = texture(ltcAmpLUT, uv).rg;

    mat3 Minv = mat3(
        vec3(t1.x, 0.0, t1.y),
        vec3(0.0, 1.0, 0.0),
        vec3(t1.z, 0.0, t1.w));

    float specI = ltcEvaluate(N, V, P, Minv, p0, p1, p2, p3);
    vec3 specular = (F0 * t2.x + (1.0 - F0) * t2.y) * specI;

    float diffI = ltcEvaluate(N, V, P, mat3(1.0), p0, p1, p2, p3);
    vec3 diffuse = (1.0 - metallic) * albedo * diffI;

    return light.color * light.intensity * (diffuse + specular);
}


// ---- Shadows ----

float shadowFactor(vec3 worldPos) {
    if (shadowData.enabled == 0u) return 1.0;

    vec4 lightSpace = shadowData.lightViewProj * vec4(worldPos, 1.0);
    vec3 ndc = lightSpace.xyz / lightSpace.w;
    if (ndc.z <= 0.0 || ndc.z >= 1.0) return 1.0;

    // Out-of-map samples land on the white border and stay lit.
    vec2 uv = ndc.xy * 0.5 + 0.5;

    float sum = 0.0;
    for (int x = -1; x <= 1; x++)
    for (int y = -1; y <= 1; y++)
    sum += texture(shadowMap, vec3(uv + vec2(x, y) * shadowData.texelSize, ndc.z));
    return sum / 9.0;
}


// ---- Clustered light loop ----

uint clusterIndexFor(vec3 worldPos) {
    float viewZ = (ubo.cameraData.view * vec4(worldPos, 1.0)).z;
    float near = ubo.cameraData.nearPlane;
    float far = ubo.cameraData.farPlane;

    float slicef = log(max(viewZ, near) / near) / log(far / near) * float(CLUSTER_GRID.z);
    uint slice = min(uint(max(slicef, 0.0)), CLUSTER_GRID.z - 1u);
    uvec2 tile = min(uvec2(gl_FragCoord.xy * push.tileScale), CLUSTER_GRID.xy - 1u);

    return tile.x + CLUSTER_GRID.x * (tile.y + CLUSTER_GRID.y * slice);
}

vec3 evaluateLights(vec3 N, vec3 V, vec3 worldPos, vec3 albedo, float metallic, float roughness, vec3 F0)
{
    vec3 Lo = vec3(0.0);

    for (uint i = 0u; i < lightData.directionalCount; i++) {
        vec3 contribution = evaluateLight(lightData.lights[i], N, V, worldPos, albedo, metallic, roughness, F0);
        if (shadowData.enabled != 0u && i == shadowData.lightIndex) {
            contribution *= shadowFactor(worldPos);
        }
        Lo += contribution;
    }

    uint cluster = clusterIndexFor(worldPos);
    uint clusterLightCount = min(clusterData.clusters[cluster].count, MAX_LIGHTS_PER_CLUSTER);
    for (uint i = 0u; i < clusterLightCount; i++) {
        Light light = lightData.lights[clusterData.clusters[cluster].indices[i]];
        Lo += light.type == LIGHT_TYPE_RECT
            ? evaluateRectLight(light, N, V, worldPos, albedo, metallic, roughness, F0)
            : evaluateLight(light, N, V, worldPos, albedo, metallic, roughness, F0);
    }

    return Lo;
}


// ---- IBL ----

vec3 evaluateIBL(vec3 N, vec3 V, vec3 albedo, float metallic, float roughness, vec3 F0, float ao)
{
    float NdotV = max(dot(N, V), 0.0);

    // diffuse
    vec3 irradiance  = texture(irradianceMap, N).rgb;
    //irradiance       = irradiance / (irradiance + vec3(1.0));
    vec3 kS_ibl      = F_SchlickRoughness(NdotV, F0, roughness);
    vec3 kD_ibl      = (1.0 - kS_ibl) * (1.0 - metallic);
    vec3 diffuse_ibl = kD_ibl * irradiance * albedo * ao * ubo.debugData.iblMultiplier;

    vec3  R              = reflect(-V, N);
    float lod            = roughness * MAX_REFLECTION_LOD;
    vec3  prefilteredColor = textureLod(prefilterMap, R, lod).rgb;
    vec2 brdf = texture(brdfLUT, vec2(NdotV, roughness)).rg;
    if (brdf.x > 0.999 && brdf.y > 0.999) {
        brdf = vec2(1.0, 0.0);
    }
    vec3 specular_ibl = prefilteredColor * (F0 * brdf.x + brdf.y) * ubo.debugData.iblMultiplier;

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
    vec4 albedoSample = obj.textureIndices.x == 0u
        ? vec4(1.0)
        : texture(textures[nonuniformEXT(obj.textureIndices.x)], fragTexCoord);
    vec3 albedo       = albedoSample.rgb * obj.baseColor.rgb * fragColor;
    float alpha       = albedoSample.a * obj.baseColor.a;

    // Debug: unlit mode outputs base color only (no lighting/IBL)
    if (ubo.debugData.viewMode == VIEWMODE_UNLIT) {
        outColor = vec4(albedo.rgb, 1.0);
        return;
    }

    vec3 V  = normalize(ubo.cameraData.position - fragWorldPos);

    // normal
    vec3 baseNormal = normalize(fragNormal);
    vec3 N = perturbNormal(baseNormal, fragTangent,
    fragTexCoord, obj.textureIndices.y);
    N = faceforward(N, -V, N);

    // metallic-roughness
    float metallic  = obj.materialFactors.x;
    float roughness = obj.materialFactors.y;
    if (obj.textureIndices.z != 0u) {
        vec4 mr   = texture(textures[nonuniformEXT(obj.textureIndices.z)], fragTexCoord);
        roughness *= mr.g;
        metallic  *= mr.b;
    }
    roughness = clamp(roughness, 0.04, 1.0);

    // AO
    float ao = 1.0;
    if (obj.textureIndices.w != 0u)
    ao = texture(textures[nonuniformEXT(obj.textureIndices.w)], fragTexCoord).r;

    // Debug: clay mode overrides material to a neutral, readable look
    if (ubo.debugData.viewMode == VIEWMODE_CLAY) {
        const vec3  clayAlbedo    = vec3(0.8);
        const float clayMetallic  = 0.0;
        const float clayRoughness = 0.7;

        vec3 clayF0 = vec3(0.04);

        vec3 ambientClay = evaluateIBL(N, V, clayAlbedo, clayMetallic, clayRoughness, clayF0, ao);
        vec3 LoClay = evaluateLights(N, V, fragWorldPos, clayAlbedo, clayMetallic, clayRoughness, clayF0);

        outColor = vec4(ambientClay + LoClay, alpha);
        return;
    }

    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    vec3 ambient = evaluateIBL(N, V, albedo, metallic, roughness, F0, ao);
    vec3 Lo = evaluateLights(N, V, fragWorldPos, albedo, metallic, roughness, F0);

    outColor = vec4(ambient + Lo, alpha);
}
