#include "Material.hpp"

#include <iomanip>
#include <sstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "AssetManager.hpp"
#include "core/Core.hpp"

namespace Atlas {
    namespace MaterialSerialization {
        using Json = nlohmann::json;

        const char *shadingModelToString(ShadingModel model) {
            switch (model) {
                case ShadingModel::STANDARD_PBR: return "StandardPBR";
                case ShadingModel::CLOTH_CHARLIE: return "ClothCharlie";
                case ShadingModel::UNLIT: return "Unlit";
            }

            return "StandardPBR";
        }

        ShadingModel shadingModelFromString(const std::string &value) {
            if (value == "ClothCharlie") return ShadingModel::CLOTH_CHARLIE;
            if (value == "Unlit") return ShadingModel::UNLIT;
            return ShadingModel::STANDARD_PBR;
        }

        const char *alphaModeToString(AlphaMode mode) {
            switch (mode) {
                case AlphaMode::OPAQUE: return "Opaque";
                case AlphaMode::MASK: return "Mask";
                case AlphaMode::BLEND: return "Blend";
            }

            return "Opaque";
        }

        AlphaMode alphaModeFromString(const std::string &value) {
            if (value == "Mask") return AlphaMode::MASK;
            if (value == "Blend") return AlphaMode::BLEND;
            return AlphaMode::OPAQUE;
        }

        std::string readString(const Json &data, const char *key, std::string fallback = {}) {
            const auto it = data.find(key);
            if (it == data.end()) {
                return fallback;
            }

            if (!it->is_string()) {
                throw std::runtime_error(std::string("Material field must be a string: ") + key);
            }

            return it->get<std::string>();
        }

        float readFloat(const Json &data, const char *key, float fallback) {
            const auto it = data.find(key);
            if (it == data.end()) {
                return fallback;
            }

            if (!it->is_number()) {
                throw std::runtime_error(std::string("Material field must be a number: ") + key);
            }

            return it->get<float>();
        }

        template<typename Vec>
        Vec readVector(const Json &data, const char *key, const Vec &fallback) {
            const auto it = data.find(key);
            if (it == data.end()) {
                return fallback;
            }

            return it->get<Vec>();
        }

        void loadTextureSlot(
            AssetManager &assets,
            const Json &textures,
            const char *key,
            AssetHandle<Texture> &handle) {
            const auto it = textures.find(key);
            if (it == textures.end() || it->is_null()) {
                return;
            }

            if (!it->is_string()) {
                throw std::runtime_error(std::string("Material texture slot must be a string: ") + key);
            }

            const std::string path = it->get<std::string>();
            if (!path.empty()) {
                handle = assets.store<Texture>(path);
            }
        }

        void writeTextureSlot(Json &textures, const char *key, const AssetHandle<Texture> &handle) {
            if (handle.hasPath()) {
                textures[key] = handle.path();
            }
        }
    }

    std::shared_ptr<Material> Material::fromFile(const std::string &path, AssetManager &assets) {
        const std::string source = AssetManager::loadFileAsString(path);
        MaterialSerialization::Json data;

        try {
            data = MaterialSerialization::Json::parse(source);
        } catch (const MaterialSerialization::Json::exception &error) {
            throw std::runtime_error("Failed to parse material JSON: " + path + " (" + error.what() + ")");
        }

        if (!data.is_object()) {
            throw std::runtime_error("Material root must be a JSON object: " + path);
        }

        auto material = std::make_shared<Material>();
        material->name = MaterialSerialization::readString(data, "name", material->name);
        material->shadingModel = MaterialSerialization::shadingModelFromString(
            MaterialSerialization::readString(data, "shadingModel", MaterialSerialization::shadingModelToString(material->shadingModel)));
        material->alphaMode = MaterialSerialization::alphaModeFromString(
            MaterialSerialization::readString(data, "alphaMode", MaterialSerialization::alphaModeToString(material->alphaMode)));
        material->baseColor = MaterialSerialization::readVector(data, "baseColor", material->baseColor);
        material->metallic = MaterialSerialization::readFloat(data, "metallic", material->metallic);
        material->roughness = MaterialSerialization::readFloat(data, "roughness", material->roughness);
        material->alphaCutoff = MaterialSerialization::readFloat(data, "alphaCutoff", material->alphaCutoff);
        material->sheenStrength = MaterialSerialization::readFloat(data, "sheenStrength", material->sheenStrength);
        material->sheenColor = MaterialSerialization::readVector(data, "sheenColor", material->sheenColor);
        material->emissiveColor = MaterialSerialization::readVector(data, "emissiveColor", material->emissiveColor);
        material->emissiveStrength = MaterialSerialization::readFloat(data, "emissiveStrength", material->emissiveStrength);

        if (const auto textures = data.find("textures"); textures != data.end()) {
            if (!textures->is_object()) {
                throw std::runtime_error("Material textures field must be an object: " + path);
            }

            MaterialSerialization::loadTextureSlot(assets, *textures, "baseColor", material->baseColorTexture);
            MaterialSerialization::loadTextureSlot(assets, *textures, "normal", material->normalTexture);
            MaterialSerialization::loadTextureSlot(assets, *textures, "metallicRoughness", material->metallicRoughnessTexture);
            MaterialSerialization::loadTextureSlot(assets, *textures, "occlusion", material->occlusionTexture);
            MaterialSerialization::loadTextureSlot(assets, *textures, "emissive", material->emissiveTexture);
        }

        return material;
    }

    void Material::saveFile(const Material &material, const std::string &path) {
        MaterialSerialization::Json data;
        data["version"] = 1;
        data["name"] = material.name;
        data["shadingModel"] = MaterialSerialization::shadingModelToString(material.shadingModel);
        data["alphaMode"] = MaterialSerialization::alphaModeToString(material.alphaMode);
        data["baseColor"] = material.baseColor;
        data["metallic"] = material.metallic;
        data["roughness"] = material.roughness;
        data["alphaCutoff"] = material.alphaCutoff;
        data["sheenStrength"] = material.sheenStrength;
        data["sheenColor"] = material.sheenColor;
        data["emissiveColor"] = material.emissiveColor;
        data["emissiveStrength"] = material.emissiveStrength;

        MaterialSerialization::Json textures = MaterialSerialization::Json::object();
        MaterialSerialization::writeTextureSlot(textures, "baseColor", material.baseColorTexture);
        MaterialSerialization::writeTextureSlot(textures, "normal", material.normalTexture);
        MaterialSerialization::writeTextureSlot(textures, "metallicRoughness", material.metallicRoughnessTexture);
        MaterialSerialization::writeTextureSlot(textures, "occlusion", material.occlusionTexture);
        MaterialSerialization::writeTextureSlot(textures, "emissive", material.emissiveTexture);
        if (!textures.empty()) {
            data["textures"] = textures;
        }

        std::ostringstream out;
        out << std::setw(2) << data << '\n';
        AssetManager::saveFileAsString(out.str(), path);
    }
} // Atlas
