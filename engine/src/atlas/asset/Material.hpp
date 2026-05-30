#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include "AssetHandle.hpp"

namespace Atlas {
    class AssetManager;

    enum class AlphaMode : uint32_t {
        OPAQUE = 0,
        MASK = 1,
        BLEND = 2,
    };

    enum class ShadingModel : std::uint32_t {
        STANDARD_PBR = 0,
        CLOTH_CHARLIE = 1,
        UNLIT = 2
    };

    class Material {
    public:
        std::string name{};
        ShadingModel shadingModel{ShadingModel::STANDARD_PBR};
        AlphaMode alphaMode{AlphaMode::OPAQUE};

        glm::vec4 baseColor{1.0f};
        float metallic{0.0f};
        float roughness{0.5f};
        float alphaCutoff{0.5f};

        float sheenStrength{0.0f};
        glm::vec3 sheenColor{1.0f};

        AssetHandle<Texture> baseColorTexture;
        AssetHandle<Texture> normalTexture;
        AssetHandle<Texture> metallicRoughnessTexture;
        AssetHandle<Texture> occlusionTexture;
        AssetHandle<Texture> emissiveTexture;

        glm::vec3 emissiveColor{0.0f};

        float emissiveStrength{1.0f};

        static std::shared_ptr<Material> fromFile(const std::string &path, AssetManager &assets);
        static void saveFile(const Material &material, const std::string &path);

        size_t getHash() const {
            size_t seed = 0;
            hashCombine(seed, name);
            hashCombine(seed, static_cast<uint32_t>(shadingModel));
            hashCombine(seed, static_cast<uint32_t>(alphaMode));
            hashCombine(seed, baseColor.x);
            hashCombine(seed, baseColor.y);
            hashCombine(seed, baseColor.z);
            hashCombine(seed, baseColor.w);
            hashCombine(seed, metallic);
            hashCombine(seed, roughness);
            hashCombine(seed, alphaCutoff);
            hashCombine(seed, sheenStrength);
            hashCombine(seed, sheenColor.x);
            hashCombine(seed, sheenColor.y);
            hashCombine(seed, sheenColor.z);
            hashCombine(seed, baseColorTexture.path());
            hashCombine(seed, normalTexture.path());
            hashCombine(seed, metallicRoughnessTexture.path());
            hashCombine(seed, occlusionTexture.path());
            hashCombine(seed, emissiveTexture.path());
            hashCombine(seed, baseColorTexture.identity());
            hashCombine(seed, normalTexture.identity());
            hashCombine(seed, metallicRoughnessTexture.identity());
            hashCombine(seed, occlusionTexture.identity());
            hashCombine(seed, emissiveTexture.identity());
            hashCombine(seed, emissiveColor.x);
            hashCombine(seed, emissiveColor.y);
            hashCombine(seed, emissiveColor.z);
            hashCombine(seed, emissiveStrength);
            return seed;
        }

    private:
        template<typename T>
        static void hashCombine(size_t &seed, const T &value) {
            seed ^= std::hash<T>{}(value) + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
        }
    };
}
