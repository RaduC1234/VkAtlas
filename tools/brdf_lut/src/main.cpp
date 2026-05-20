#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <cstdio>
#include <cstdint>
#include <vector>
#include <chrono>

constexpr int LUT_SIZE = 512;
constexpr int NUM_SAMPLES = 1024;

float radicalInverseVdC(uint32_t bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return static_cast<float>(bits) / static_cast<float>(0x100000000ULL);
}

glm::vec2 hammersley(uint32_t i, uint32_t n) {
    return {static_cast<float>(i) / static_cast<float>(n), radicalInverseVdC(i)};
}

glm::vec3 importanceSampleGGX(glm::vec2 xi, float roughness) {
    float a = roughness * roughness;
    float phi = 2.0f * glm::pi<float>() * xi.x;
    float cosTheta = glm::sqrt((1.0f - xi.y) / (1.0f + (a * a - 1.0f) * xi.y));
    float sinTheta = glm::sqrt(glm::max(1.0f - cosTheta * cosTheta, 0.0f));

    return {
        glm::cos(phi) * sinTheta,
        glm::sin(phi) * sinTheta,
        cosTheta
    };
}

float geometrySchlickGGX(float NdotV, float roughness) {
    // IBL uses k = (roughness^2) / 2
    float k = (roughness * roughness) / 2.0f;
    return NdotV / (NdotV * (1.0f - k) + k);
}

float geometrySmith(float NdotV, float NdotL, float roughness) {
    return geometrySchlickGGX(NdotV, roughness) * geometrySchlickGGX(NdotL, roughness);
}

glm::vec2 integrateBRDF(float NdotV, float roughness) {
    NdotV = glm::max(NdotV, 0.001f);

    glm::vec3 V = {
        glm::sqrt(1.0f - NdotV * NdotV),
        0.0f,
        NdotV
    };

    float scale = 0.0f;
    float bias = 0.0f;

    for (uint32_t i = 0; i < NUM_SAMPLES; i++) {
        glm::vec2 xi = hammersley(i, NUM_SAMPLES);
        glm::vec3 H = importanceSampleGGX(xi, roughness);

        float VdotH = glm::max(glm::dot(V, H), 0.0f);
        glm::vec3 L = 2.0f * VdotH * H - V;

        float NdotL = glm::max(L.z, 0.0f);
        float NdotH = glm::max(H.z, 0.0f);

        if (NdotL > 0.0f) {
            float G = geometrySmith(NdotV, NdotL, roughness);
            float Gvis = (G * VdotH) / glm::max(NdotH * NdotV, 0.001f);
            float Fc = glm::pow(1.0f - VdotH, 5.0f);

            scale += Gvis * (1.0f - Fc);
            bias += Gvis * Fc;
        }
    }

    float invSamples = 1.0f / static_cast<float>(NUM_SAMPLES);
    return {scale * invSamples, bias * invSamples};
}

int main() {
    printf("Generating BRDF LUT (%dx%d, %d samples per texel)\n", LUT_SIZE, LUT_SIZE, NUM_SAMPLES);

    auto start = std::chrono::high_resolution_clock::now();

    std::vector<float> lut(LUT_SIZE * LUT_SIZE * 2);

    for (int y = 0; y < LUT_SIZE; y++) {
        float roughness = glm::max((static_cast<float>(y) + 0.5f) / static_cast<float>(LUT_SIZE), 0.01f);

        for (int x = 0; x < LUT_SIZE; x++) {
            float NdotV = (static_cast<float>(x) + 0.5f) / static_cast<float>(LUT_SIZE);

            glm::vec2 result = integrateBRDF(NdotV, roughness);

            int idx = (y * LUT_SIZE + x) * 2;
            lut[idx + 0] = result.x;
            lut[idx + 1] = result.y;
        }

        if ((y + 1) % 64 == 0) {
            auto now = std::chrono::high_resolution_clock::now();
            float elapsed = std::chrono::duration<float>(now - start).count();
            float progress = static_cast<float>(y + 1) / static_cast<float>(LUT_SIZE);
            float eta = elapsed / progress * (1.0f - progress);
            printf("  %3.0f%% done, ETA: %.1fs\n", progress * 100.0f, eta);
        }
    }

    auto end = std::chrono::high_resolution_clock::now();
    float elapsed = std::chrono::duration<float>(end - start).count();
    printf("Done in %.1fs\n", elapsed);

    // Replace the file writing section at the bottom, change path and format:

    const char* path = "brdf_lut.hdr";
    FILE* f = fopen(path, "wb");
    if (!f) {
        printf("ERROR: Failed to open %s for writing\n", path);
        return 1;
    }

    // Radiance HDR header
    fprintf(f, "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n");
    fprintf(f, "-Y %d +X %d\n", LUT_SIZE, LUT_SIZE);

    // Write scanlines as raw RGBE (no RLE — simplest valid HDR)
    for (int y = 0; y < LUT_SIZE; y++) {
        for (int x = 0; x < LUT_SIZE; x++) {
            int idx = (y * LUT_SIZE + x) * 2;
            float r = lut[idx + 0];
            float g = lut[idx + 1];
            float b = 0.0f;

            // Encode RGB -> RGBE
            float maxVal = glm::max(glm::max(r, g), b);
            uint8_t rgbe[4] = {0, 0, 0, 0};

            if (maxVal > 1e-32f) {
                int exp;
                float scale = frexpf(maxVal, &exp) * 256.0f / maxVal;
                rgbe[0] = static_cast<uint8_t>(r * scale);
                rgbe[1] = static_cast<uint8_t>(g * scale);
                rgbe[2] = static_cast<uint8_t>(b * scale);
                rgbe[3] = static_cast<uint8_t>(exp + 128);
            }

            fwrite(rgbe, 1, 4, f);
        }
    }

    fclose(f);

    printf("Saved: %s\n", path);
    printf("Format: %dx%d, RG encoded as HDR (B=0)\n", LUT_SIZE, LUT_SIZE);
}