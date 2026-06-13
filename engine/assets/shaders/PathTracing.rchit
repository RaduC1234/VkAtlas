#version 460

#extension GL_EXT_ray_tracing          : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_scalar_block_layout  : require
#extension GL_EXT_buffer_reference2    : require
#extension GL_GOOGLE_include_directive : require

#include "common/RTTypes.glsl"
#include "common/Constants.glsl"
#include "common/BRDF.glsl"
#include "common/Attenuation.glsl"
#include "common/RNG.glsl"

// ---- Descriptors ----

layout(set = 1, binding = 1) uniform accelerationStructureEXT tlas;
layout(scalar, set = 1, binding = 2) readonly buffer VertexBuffer  { Vertex     vertices[]; };
layout(scalar, set = 1, binding = 3) readonly buffer IndexBuffer   { uint       indices[];  };
layout(scalar, set = 1, binding = 4) readonly buffer ObjectBuffer  { ObjectData objects[];  };
layout(scalar, set = 1, binding = 5) readonly buffer LightBuffer   { Light      lights[];   };
layout(set = 1, binding = 6) uniform sampler2D textures[];

// ---- Push constants ----

layout(push_constant) uniform PushConstants {
    uint sampleIndex;
    uint maxBounces;
    uint frameIndex;
    uint lightCount;
} pc;

// ---- Payload ----

layout(location = 0) rayPayloadInEXT RayPayload payload;
layout(location = 1) rayPayloadEXT   bool       shadowPayload;

hitAttributeEXT vec2 barycentrics;

const float FIREFLY_CLAMP = 8.0;

// ---- Main ----

void main() {
    ObjectData obj = objects[gl_InstanceCustomIndexEXT];

    uint i0 = indices[obj.firstIndex + gl_PrimitiveID * 3 + 0];
    uint i1 = indices[obj.firstIndex + gl_PrimitiveID * 3 + 1];
    uint i2 = indices[obj.firstIndex + gl_PrimitiveID * 3 + 2];

    Vertex v0 = vertices[obj.firstVertex + i0];
    Vertex v1 = vertices[obj.firstVertex + i1];
    Vertex v2 = vertices[obj.firstVertex + i2];

    vec3 bary = vec3(1.0 - barycentrics.x - barycentrics.y,
                     barycentrics.x, barycentrics.y);

    vec3 localPos = v0.position * bary.x + v1.position * bary.y + v2.position * bary.z;
    vec3 worldPos = (obj.modelMatrix * vec4(localPos, 1.0)).xyz;

    vec3 worldEdge1      = (obj.modelMatrix * vec4(v1.position - v0.position, 0.0)).xyz;
    vec3 worldEdge2      = (obj.modelMatrix * vec4(v2.position - v0.position, 0.0)).xyz;
    vec3 geometricNormal = safeNormalize(cross(worldEdge1, worldEdge2), vec3(0.0, 1.0, 0.0));
    if (dot(geometricNormal, -payload.direction) < 0.0) geometricNormal = -geometricNormal;

    vec3 localNormal = v0.normal * bary.x + v1.normal * bary.y + v2.normal * bary.z;
    vec3 N           = safeNormalize((obj.normalMatrix * vec4(localNormal, 0.0)).xyz, geometricNormal);
    if (dot(N, geometricNormal) < 0.0) N = -N;

    vec2 uv          = v0.uv * bary.x + v1.uv * bary.y + v2.uv * bary.z;
    vec3 vertexColor = v0.color * bary.x + v1.color * bary.y + v2.color * bary.z;

    vec4 albedoSample = obj.textureIndices.x == 0u
        ? vec4(1.0)
        : texture(textures[nonuniformEXT(obj.textureIndices.x)], uv);
    vec3 albedo = albedoSample.rgb * obj.baseColor.rgb * vertexColor;

    float metallic  = obj.materialFactors.x;
    float roughness = obj.materialFactors.y;
    if (obj.textureIndices.z != 0u) {
        vec4 mr   = texture(textures[nonuniformEXT(obj.textureIndices.z)], uv);
        roughness *= mr.g;
        metallic  *= mr.b;
    }
    roughness = clamp(roughness, 0.04, 1.0);

    if (obj.textureIndices.y != 0u) {
        vec3 ts = texture(textures[nonuniformEXT(obj.textureIndices.y)], uv).rgb * 2.0 - 1.0;
        if (!hasInvalid(ts)) {
            vec3 tangentLocal = v0.tangent.xyz * bary.x + v1.tangent.xyz * bary.y + v2.tangent.xyz * bary.z;
            vec3 T = safeNormalize((obj.modelMatrix * vec4(tangentLocal, 0.0)).xyz, fallbackTangent(N));
            T = safeNormalize(T - dot(T, N) * N, fallbackTangent(N));

            float tangentSign = v0.tangent.w * bary.x + v1.tangent.w * bary.y + v2.tangent.w * bary.z;
            tangentSign = tangentSign < 0.0 ? -1.0 : 1.0;
            vec3 B = safeNormalize(cross(N, T) * tangentSign, fallbackTangent(N));

            N = safeNormalize(mat3(T, B, N) * ts, N);
            if (dot(N, geometricNormal) < 0.0) N = -N;
        }
    }

    vec3 V  = safeNormalize(-payload.direction, geometricNormal);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    bool clothCharlie  = (obj.flags & MATERIAL_FLAG_CLOTH_CHARLIE) != 0u;
    vec3 sheenColor    = obj.sheenColorStrength.rgb;
    float sheenStrength = obj.sheenColorStrength.a;

    // ---- NEE — direct light sampling ----
    vec3 directLight = vec3(0.0);
    for (uint i = 0u; i < pc.lightCount; i++) {
        Light light = lights[i];
        if (light.intensity <= 0.0) continue;

        vec3  L;
        float atten;
        float tMax;

        if (light.type == LIGHT_TYPE_DIRECTIONAL) {
            L     = normalize(-light.direction);
            atten = 1.0;
            tMax  = 10000.0;
        } else if (light.type == LIGHT_TYPE_RECT) {
            vec3 lightNormal = normalize(light.direction);
            vec3 rectT = normalize(light.rectRight);
            vec3 rectB = normalize(light.rectUp);

            vec2 u = vec2(randFloat(payload.seed), randFloat(payload.seed)) - 0.5;
            vec3 samplePos = light.position
                + rectT * (u.x * max(light.width, EPSILON))
                + rectB * (u.y * max(light.height, EPSILON));

            vec3  toLight = samplePos - worldPos;
            float dist    = length(toLight);
            if (dist <= 0.001) continue;

            L = toLight / dist;
            float cosLight = abs(dot(lightNormal, -L));
            if (cosLight <= 0.0) continue;

            float area = max(light.width * light.height, EPSILON);
            atten = area * cosLight / max(dist * dist, EPSILON);
            tMax  = max(dist - 0.001, 0.0);
        } else {
            vec3  toLight = light.position - worldPos;
            float dist    = length(toLight);
            if (dist <= 0.001) continue;

            L     = toLight / dist;
            atten = distanceAttenuation(dist, light.range);
            tMax  = max(dist - 0.001, 0.0);
        }

        if (light.type == LIGHT_TYPE_SPOT) {
            atten *= spotAttenuation(L, normalize(light.direction), light.innerConeAngle, light.outerConeAngle);
        }

        float NdotL = dot(N, L);
        if (NdotL <= 0.0 || dot(geometricNormal, L) <= 0.0) continue;

        shadowPayload = true;
        traceRayEXT(tlas,
                    gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsSkipClosestHitShaderEXT,
                    0xFF, 0, 0, 1,
                    offsetRayOrigin(worldPos, geometricNormal, L), 0.0, L, tMax, 1);

        if (shadowPayload) {
            continue;
        }

        float NdotV = max(dot(N, V), EPSILON);
        vec3  H     = safeNormalize(V + L, N);
        float NdotH = max(dot(N, H), 0.0);
        float HdotV = max(dot(H, V), 0.0);

        vec3 diffuse;
        vec3 specular;
        if (clothCharlie) {
            float D   = D_Charlie(NdotH, roughness);
            float Vis = V_Charlie(NdotV, NdotL);
            diffuse  = albedo * INV_PI;
            specular = sheenColor * sheenStrength * D * Vis;
        } else {
            float D   = D_GGX(NdotH, roughness);
            float Vis = V_SmithGGXCorrelated(NdotV, NdotL, roughness);
            vec3  F   = F_Schlick(HdotV, F0);
            specular  = D * Vis * F;
            vec3 kD   = (1.0 - F) * (1.0 - metallic);
            diffuse   = kD * albedo * INV_PI;
        }

        vec3 radiance = min(light.color.rgb * light.intensity * atten, FIREFLY_CLAMP);
        directLight  += (diffuse + specular) * radiance * NdotL;
    }

    payload.radiance = sanitizeColor(payload.radiance + payload.throughput * directLight);

    // ---- Indirect — sample next bounce direction ----
    float specularWeight = clothCharlie ? 0.0 : length(F_Schlick(max(dot(N, V), 0.0), F0));
    float diffuseWeight  = clothCharlie ? 1.0 : (1.0 - metallic);
    float totalWeight    = specularWeight + diffuseWeight + EPSILON;

    vec3  nextDir;
    float pdf;
    vec3  brdfWeight;

    if (randFloat(payload.seed) < specularWeight / totalWeight) {
        vec3  H     = sampleGGX(vec2(randFloat(payload.seed), randFloat(payload.seed)), roughness, N);
        nextDir     = safeNormalize(reflect(-V, H), N);
        float NdotL = max(dot(N, nextDir), 0.0);
        float NdotV = max(dot(N, V), EPSILON);
        float NdotH = max(dot(N, H), 0.0);
        float HdotV = max(dot(H, V), 0.0);

        vec3  F   = F_Schlick(HdotV, F0);
        float Vis = V_SmithGGXCorrelated(NdotV, NdotL, roughness);
        brdfWeight = F * Vis * NdotL * HdotV / max(NdotH * 0.25, EPSILON);
        pdf        = specularWeight / totalWeight;
    } else {
        nextDir     = sampleCosineHemisphere(vec2(randFloat(payload.seed), randFloat(payload.seed)), N);
        float NdotL = max(dot(N, nextDir), 0.0);
        vec3  H     = safeNormalize(V + nextDir, N);
        float HdotV = max(dot(H, V), 0.0);
        if (clothCharlie) {
            brdfWeight = albedo + sheenColor * sheenStrength;
        } else {
            vec3 F     = F_Schlick(HdotV, F0);
            vec3 kD    = (1.0 - F) * (1.0 - metallic);
            brdfWeight = kD * albedo;
        }
        pdf = diffuseWeight / totalWeight;
    }

    if (hasInvalid(nextDir) || hasInvalid(brdfWeight) || hasInvalid(pdf) || dot(nextDir, N) <= 0.0 || dot(nextDir, geometricNormal) <= 0.0 || pdf < EPSILON) {
        payload.done = true;
        return;
    }

    payload.throughput = sanitizeColor(payload.throughput * (brdfWeight / pdf));
    payload.origin     = offsetRayOrigin(worldPos, geometricNormal, nextDir);
    payload.direction  = nextDir;

    float maxThroughput = max(payload.throughput.r, max(payload.throughput.g, payload.throughput.b));
    if (hasInvalid(maxThroughput) || maxThroughput <= EPSILON) {
        payload.done = true;
        return;
    }

    float survivalProb = clamp(maxThroughput, 0.05, 1.0);
    if (randFloat(payload.seed) > survivalProb) {
        payload.done = true;
        return;
    }
    payload.throughput = sanitizeColor(payload.throughput / survivalProb);
}
