
// Requires: Constants.glsl (PI, INV_PI, EPSILON)

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

float D_Charlie(float NdotH, float roughness) {
    float alpha    = clamp(roughness, 0.04, 1.0);
    float invAlpha = 1.0 / alpha;
    float sinTheta = sqrt(max(1.0 - NdotH * NdotH, 0.0));
    return (2.0 + invAlpha) * pow(sinTheta, invAlpha) / (2.0 * PI);
}

float V_Charlie(float NdotV, float NdotL) {
    return 1.0 / max(4.0 * (NdotL + NdotV - NdotL * NdotV), EPSILON);
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

vec3 sampleCosineHemisphere(vec2 u, vec3 N) {
    float r   = sqrt(u.x);
    float phi = 2.0 * PI * u.y;
    vec3  T, B;
    if (abs(N.x) > 0.9)
        T = normalize(cross(N, vec3(0, 1, 0)));
    else
        T = normalize(cross(N, vec3(1, 0, 0)));
    B = cross(N, T);
    return normalize(r * cos(phi) * T + r * sin(phi) * B + sqrt(1.0 - u.x) * N);
}

vec3 sampleGGX(vec2 u, float roughness, vec3 N) {
    float a    = roughness * roughness;
    float phi  = 2.0 * PI * u.x;
    float cosT = sqrt((1.0 - u.y) / (1.0 + (a * a - 1.0) * u.y));
    float sinT = sqrt(1.0 - cosT * cosT);
    vec3  H_local = vec3(sinT * cos(phi), sinT * sin(phi), cosT);
    vec3  T, B;
    if (abs(N.x) > 0.9)
        T = normalize(cross(N, vec3(0, 1, 0)));
    else
        T = normalize(cross(N, vec3(1, 0, 0)));
    B = cross(N, T);
    return normalize(H_local.x * T + H_local.y * B + H_local.z * N);
}
