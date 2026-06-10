#include "scene/LevelSerializer.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "asset/AssetManager.hpp"
#include "entity/Object.hpp"

namespace Atlas {
    using Json = nlohmann::json;

    Mesh::Vertex makePrimitiveVertex(glm::vec3 position, glm::vec3 normal, glm::vec2 uv, glm::vec4 tangent) {
        Mesh::Vertex vertex{};
        vertex.position = position;
        vertex.color = glm::vec3{1.0f};
        vertex.normal = normal;
        vertex.uv = uv;
        vertex.tangent = tangent;
        return vertex;
    }

    std::shared_ptr<Mesh> makeEditorCubeMesh() {
        std::vector<Mesh::Vertex> vertices;
        std::vector<uint32_t> indices;
        vertices.reserve(24);
        indices.reserve(36);

        auto addFace = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, glm::vec3 normal, glm::vec4 tangent) {
            const uint32_t base = static_cast<uint32_t>(vertices.size());
            vertices.push_back(makePrimitiveVertex(a, normal, {0.0f, 0.0f}, tangent));
            vertices.push_back(makePrimitiveVertex(b, normal, {1.0f, 0.0f}, tangent));
            vertices.push_back(makePrimitiveVertex(c, normal, {1.0f, 1.0f}, tangent));
            vertices.push_back(makePrimitiveVertex(d, normal, {0.0f, 1.0f}, tangent));
            indices.insert(indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
        };

        constexpr float half = 0.5f;
        addFace({-half, -half, half}, {half, -half, half}, {half, half, half}, {-half, half, half}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f, 1.0f});
        addFace({half, -half, -half}, {-half, -half, -half}, {-half, half, -half}, {half, half, -half}, {0.0f, 0.0f, -1.0f}, {-1.0f, 0.0f, 0.0f, 1.0f});
        addFace({half, -half, half}, {half, -half, -half}, {half, half, -half}, {half, half, half}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f, 1.0f});
        addFace({-half, -half, -half}, {-half, -half, half}, {-half, half, half}, {-half, half, -half}, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 1.0f});
        addFace({-half, half, half}, {half, half, half}, {half, half, -half}, {-half, half, -half}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f});
        addFace({-half, -half, -half}, {half, -half, -half}, {half, -half, half}, {-half, -half, half}, {0.0f, -1.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f});

        return std::make_shared<Mesh>(vertices, indices);
    }

    std::shared_ptr<Mesh> makeEditorSphereMesh() {
        constexpr uint32_t segments = 32;
        constexpr uint32_t rings = 16;
        constexpr float radius = 0.5f;
        constexpr float pi = 3.14159265358979323846f;
        constexpr float twoPi = pi * 2.0f;

        std::vector<Mesh::Vertex> vertices;
        std::vector<uint32_t> indices;
        vertices.reserve((segments + 1) * (rings + 1));
        indices.reserve(segments * rings * 6);

        for (uint32_t ring = 0; ring <= rings; ++ring) {
            const float v = static_cast<float>(ring) / static_cast<float>(rings);
            const float theta = v * pi;
            const float sinTheta = std::sin(theta);
            const float cosTheta = std::cos(theta);
            for (uint32_t segment = 0; segment <= segments; ++segment) {
                const float u = static_cast<float>(segment) / static_cast<float>(segments);
                const float phi = u * twoPi;
                const float sinPhi = std::sin(phi);
                const float cosPhi = std::cos(phi);
                const glm::vec3 normal{sinTheta * cosPhi, cosTheta, sinTheta * sinPhi};
                vertices.push_back(makePrimitiveVertex(normal * radius, normal, {u, v}, {-sinPhi, 0.0f, cosPhi, 1.0f}));
            }
        }

        for (uint32_t ring = 0; ring < rings; ++ring) {
            for (uint32_t segment = 0; segment < segments; ++segment) {
                const uint32_t a = ring * (segments + 1) + segment;
                const uint32_t b = (ring + 1) * (segments + 1) + segment;
                const uint32_t c = b + 1;
                const uint32_t d = a + 1;
                indices.insert(indices.end(), {a, d, c, a, c, b});
            }
        }

        return std::make_shared<Mesh>(vertices, indices);
    }

    std::shared_ptr<Mesh> makeEditorSquareMesh() {
        const glm::vec3 topNormal{0.0f, 1.0f, 0.0f};
        const glm::vec3 bottomNormal{0.0f, -1.0f, 0.0f};
        const glm::vec4 tangent{1.0f, 0.0f, 0.0f, 1.0f};

        std::vector<Mesh::Vertex> vertices{
            makePrimitiveVertex({-0.5f, 0.0f, 0.5f}, topNormal, {0.0f, 0.0f}, tangent),
            makePrimitiveVertex({0.5f, 0.0f, 0.5f}, topNormal, {1.0f, 0.0f}, tangent),
            makePrimitiveVertex({0.5f, 0.0f, -0.5f}, topNormal, {1.0f, 1.0f}, tangent),
            makePrimitiveVertex({-0.5f, 0.0f, -0.5f}, topNormal, {0.0f, 1.0f}, tangent),
            makePrimitiveVertex({-0.5f, 0.0f, 0.5f}, bottomNormal, {0.0f, 0.0f}, tangent),
            makePrimitiveVertex({-0.5f, 0.0f, -0.5f}, bottomNormal, {0.0f, 1.0f}, tangent),
            makePrimitiveVertex({0.5f, 0.0f, -0.5f}, bottomNormal, {1.0f, 1.0f}, tangent),
            makePrimitiveVertex({0.5f, 0.0f, 0.5f}, bottomNormal, {1.0f, 0.0f}, tangent),
        };
        std::vector<uint32_t> indices{0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7};
        return std::make_shared<Mesh>(vertices, indices);
    }

    std::shared_ptr<Mesh> makeEditorPrimitiveMesh(const std::string &path) {
        if (path == "##editor/primitives/cube") {
            return makeEditorCubeMesh();
        }
        if (path == "##editor/primitives/sphere") {
            return makeEditorSphereMesh();
        }
        if (path == "##editor/primitives/square_twosided") {
            return makeEditorSquareMesh();
        }

        return nullptr;
    }

    AssetHandle<Mesh> loadMeshHandle(AssetManager &assets, const std::string &meshPath) {
        if (meshPath.starts_with("##editor/primitives/")) {
            if (auto existing = assets.find<Mesh>(meshPath); existing.valid()) {
                return existing;
            }
            if (auto mesh = makeEditorPrimitiveMesh(meshPath)) {
                return assets.store<Mesh>(std::move(mesh), meshPath);
            }
            return AssetHandle<Mesh>::invalid();
        }

        return assets.store<Mesh>(meshPath);
    }

    AssetHandle<Material> loadMaterialHandle(AssetManager &assets, const std::string &materialPath) {
        if (materialPath.starts_with("##editor/materials/")) {
            if (auto existing = assets.find<Material>(materialPath); existing.valid()) {
                return existing;
            }

            auto material = std::make_shared<Material>();
            material->name = std::filesystem::path(materialPath).filename().string();
            material->baseColor = glm::vec4{0.82f, 0.82f, 0.78f, 1.0f};
            return assets.store<Material>(std::move(material), materialPath);
        }

        return assets.store<Material>(materialPath);
    }

    std::string inlineMaterialNormalizeSlashes(std::string path) {
        std::ranges::replace(path, '\\', '/');
        return path;
    }

    bool inlineMaterialStartsWithPathPrefix(const std::string &path, const std::string &prefix) {
        return path == prefix || path.starts_with(prefix + "/") || path.starts_with(prefix + "\\");
    }

    std::string inlineMaterialAssetVirtualPath(
        const std::string &serializedPath,
        const std::filesystem::path &assetRoot) {
        if (serializedPath.empty() || serializedPath.starts_with("##")) {
            return serializedPath;
        }

        const std::filesystem::path path(serializedPath);
        if (path.is_absolute()) {
            if (!assetRoot.empty()) {
                const std::filesystem::path relative = path.lexically_normal().lexically_relative(assetRoot.lexically_normal());
                const std::string relativeString = relative.generic_string();
                if (!relativeString.empty() && !relativeString.starts_with("..")) {
                    return relativeString;
                }
            }

            return path.lexically_normal().generic_string();
        }

        std::string normalized = inlineMaterialNormalizeSlashes(path.generic_string());
        if (inlineMaterialStartsWithPathPrefix(normalized, "assets")) {
            normalized = normalized.size() == 6 ? std::string{} : normalized.substr(7);
        }
        return normalized;
    }

    void loadInlineTextureSlot(
        AssetManager &assets,
        const Json &textures,
        const char *key,
        AssetHandle<Texture> &handle,
        const std::filesystem::path &,
        const std::filesystem::path &assetRoot) {
        const auto it = textures.find(key);
        if (it == textures.end() || it->is_null()) {
            return;
        }

        if (!it->is_string()) {
            throw std::runtime_error(std::string("Inline material texture slot must be a string: ") + key);
        }

        const std::string path = inlineMaterialAssetVirtualPath(it->get<std::string>(), assetRoot);
        if (!path.empty()) {
            handle = assets.store<Texture>(path);
        }
    }

    void writeInlineTextureSlot(Json &textures, const char *key, const AssetHandle<Texture> &handle) {
        if (handle.hasPath()) {
            textures[key] = handle.path();
        }
    }

    std::string LevelSerializer::readFile(const std::filesystem::path &path) {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open level: " + path.string());
        }

        return {
            std::istreambuf_iterator<char>(file),
            std::istreambuf_iterator<char>()
        };
    }

    void LevelSerializer::writeFile(const std::filesystem::path &path, const std::string &contents) {
        if (path.has_parent_path()) {
            std::filesystem::create_directories(path.parent_path());
        }

        std::ofstream file(path, std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to save level: " + path.string());
        }

        file << contents;
        if (!file.good()) {
            throw std::runtime_error("Failed to write level: " + path.string());
        }
    }

    std::string LevelSerializer::readString(const Json &data, const char *key, std::string fallback) {
        const auto it = data.find(key);
        if (it == data.end() || it->is_null()) {
            return fallback;
        }

        if (!it->is_string()) {
            throw std::runtime_error(std::string("Level field must be a string: ") + key);
        }

        return it->get<std::string>();
    }

    bool LevelSerializer::readBool(const Json &data, const char *key, bool fallback) {
        const auto it = data.find(key);
        if (it == data.end() || it->is_null()) {
            return fallback;
        }

        if (!it->is_boolean()) {
            throw std::runtime_error(std::string("Level field must be a boolean: ") + key);
        }

        return it->get<bool>();
    }

    float LevelSerializer::readFloat(const Json &data, const char *key, float fallback) {
        const auto it = data.find(key);
        if (it == data.end() || it->is_null()) {
            return fallback;
        }

        if (!it->is_number()) {
            throw std::runtime_error(std::string("Level field must be a number: ") + key);
        }

        return it->get<float>();
    }

    template<typename T>
    T LevelSerializer::readVector(const Json &data, const char *key, const T &fallback) {
        const auto it = data.find(key);
        if (it == data.end() || it->is_null()) {
            return fallback;
        }

        return it->get<T>();
    }

    bool LevelSerializer::startsWithPathPrefix(const std::string &path, const std::string &prefix) {
        return path == prefix || path.starts_with(prefix + "/") || path.starts_with(prefix + "\\");
    }

    std::string LevelSerializer::normalizeSlashes(std::string path) {
        std::ranges::replace(path, '\\', '/');
        return path;
    }

    std::string LevelSerializer::assetVirtualPath(
        const std::string &serializedPath,
        const std::filesystem::path &projectRoot,
        const std::filesystem::path &assetRoot) {
        if (serializedPath.empty() || serializedPath.starts_with("##")) {
            return serializedPath;
        }

        const std::filesystem::path path(serializedPath);
        if (path.is_absolute()) {
            if (!assetRoot.empty()) {
                const std::filesystem::path relative = path.lexically_normal().lexically_relative(assetRoot.lexically_normal());
                const std::string relativeString = relative.generic_string();
                if (!relativeString.empty() && !relativeString.starts_with("..")) {
                    return relativeString;
                }
            }

            return path.lexically_normal().generic_string();
        }

        std::string normalized = normalizeSlashes(path.generic_string());
        if (startsWithPathPrefix(normalized, "assets")) {
            normalized = normalized.size() == 6 ? std::string{} : normalized.substr(7);
        }

        if (!assetRoot.empty() && !projectRoot.empty()) {
            const std::string assetRootName = normalizeSlashes(assetRoot.filename().generic_string());
            if (!assetRootName.empty() && startsWithPathPrefix(normalized, assetRootName)) {
                normalized = normalized.size() == assetRootName.size()
                                 ? std::string{}
                                 : normalized.substr(assetRootName.size() + 1);
            }
        }

        return normalized;
    }

    const Json *LevelSerializer::componentData(const Json &components, const char *name) {
        const auto it = components.find(name);
        if (it == components.end() || it->is_null()) {
            return nullptr;
        }

        if (!it->is_object()) {
            throw std::runtime_error(std::string("Component data must be an object: ") + name);
        }

        return &*it;
    }

    std::string LevelSerializer::lightTypeToString(LightType type) {
        switch (type) {
            case LightType::POINT: return "Point";
            case LightType::SPOT: return "Spot";
            case LightType::DIRECTIONAL: return "Directional";
            case LightType::RECT: return "Rectangle";
            case LightType::UNKNOWN: return "Unknown";
        }

        return "Unknown";
    }

    LightType LevelSerializer::lightTypeFromString(const std::string &value) {
        if (value == "Point") return LightType::POINT;
        if (value == "Spot") return LightType::SPOT;
        if (value == "Directional") return LightType::DIRECTIONAL;
        if (value == "Rectangle") return LightType::RECT;
        return LightType::UNKNOWN;
    }

    const char *LevelSerializer::shadingModelToString(const uint32_t value) {
        switch (static_cast<ShadingModel>(value)) {
            case ShadingModel::STANDARD_PBR: return "StandardPBR";
            case ShadingModel::CLOTH_CHARLIE: return "ClothCharlie";
            case ShadingModel::UNLIT: return "Unlit";
        }

        return "StandardPBR";
    }

    uint32_t LevelSerializer::shadingModelFromString(const std::string &value) {
        if (value == "ClothCharlie") return static_cast<uint32_t>(ShadingModel::CLOTH_CHARLIE);
        if (value == "Unlit") return static_cast<uint32_t>(ShadingModel::UNLIT);
        return static_cast<uint32_t>(ShadingModel::STANDARD_PBR);
    }

    const char *LevelSerializer::alphaModeToString(const uint32_t value) {
        switch (static_cast<AlphaMode>(value)) {
            case AlphaMode::OPAQUE: return "Opaque";
            case AlphaMode::MASK: return "Mask";
            case AlphaMode::BLEND: return "Blend";
        }

        return "Opaque";
    }

    uint32_t LevelSerializer::alphaModeFromString(const std::string &value) {
        if (value == "Mask") return static_cast<uint32_t>(AlphaMode::MASK);
        if (value == "Blend") return static_cast<uint32_t>(AlphaMode::BLEND);
        return static_cast<uint32_t>(AlphaMode::OPAQUE);
    }

    Json LevelSerializer::writeInlineMaterial(const Material &material) {
        Json data;
        data["name"] = material.name;
        data["shadingModel"] = shadingModelToString(static_cast<uint32_t>(material.shadingModel));
        data["alphaMode"] = alphaModeToString(static_cast<uint32_t>(material.alphaMode));
        data["baseColor"] = material.baseColor;
        data["metallic"] = material.metallic;
        data["roughness"] = material.roughness;
        data["alphaCutoff"] = material.alphaCutoff;
        data["sheenStrength"] = material.sheenStrength;
        data["sheenColor"] = material.sheenColor;
        data["emissiveColor"] = material.emissiveColor;
        data["emissiveStrength"] = material.emissiveStrength;

        Json textures = Json::object();
        writeInlineTextureSlot(textures, "baseColor", material.baseColorTexture);
        writeInlineTextureSlot(textures, "normal", material.normalTexture);
        writeInlineTextureSlot(textures, "metallicRoughness", material.metallicRoughnessTexture);
        writeInlineTextureSlot(textures, "occlusion", material.occlusionTexture);
        writeInlineTextureSlot(textures, "emissive", material.emissiveTexture);
        if (!textures.empty()) {
            data["textures"] = textures;
        }

        return data;
    }

    void LevelSerializer::readInlineMaterial(
        Material &material,
        const Json &data,
        AssetManager &assets,
        const std::filesystem::path &projectRoot,
        const std::filesystem::path &assetRoot) {
        material.name = readString(data, "name", material.name);
        material.shadingModel = static_cast<ShadingModel>(
            shadingModelFromString(readString(data, "shadingModel", shadingModelToString(static_cast<uint32_t>(material.shadingModel)))));
        material.alphaMode = static_cast<AlphaMode>(
            alphaModeFromString(readString(data, "alphaMode", alphaModeToString(static_cast<uint32_t>(material.alphaMode)))));
        material.baseColor = readVector(data, "baseColor", material.baseColor);
        material.metallic = readFloat(data, "metallic", material.metallic);
        material.roughness = readFloat(data, "roughness", material.roughness);
        material.alphaCutoff = readFloat(data, "alphaCutoff", material.alphaCutoff);
        material.sheenStrength = readFloat(data, "sheenStrength", material.sheenStrength);
        material.sheenColor = readVector(data, "sheenColor", material.sheenColor);
        material.emissiveColor = readVector(data, "emissiveColor", material.emissiveColor);
        material.emissiveStrength = readFloat(data, "emissiveStrength", material.emissiveStrength);

        if (const auto textures = data.find("textures"); textures != data.end()) {
            if (!textures->is_object()) {
                throw std::runtime_error("Inline material textures field must be an object");
            }

            loadInlineTextureSlot(assets, *textures, "baseColor", material.baseColorTexture, projectRoot, assetRoot);
            loadInlineTextureSlot(assets, *textures, "normal", material.normalTexture, projectRoot, assetRoot);
            loadInlineTextureSlot(assets, *textures, "metallicRoughness", material.metallicRoughnessTexture, projectRoot, assetRoot);
            loadInlineTextureSlot(assets, *textures, "occlusion", material.occlusionTexture, projectRoot, assetRoot);
            loadInlineTextureSlot(assets, *textures, "emissive", material.emissiveTexture, projectRoot, assetRoot);
        }
    }

    void LevelSerializer::loadModelComponent(
        entt::registry &registry,
        entt::entity entity,
        const Json &data,
        AssetManager &assets,
        const std::filesystem::path &projectRoot,
        const std::filesystem::path &assetRoot) {
        ModelComponent component{};
        const std::string meshPath = assetVirtualPath(readString(data, "mesh"), projectRoot, assetRoot);
        if (!meshPath.empty()) {
            component.meshHandle = loadMeshHandle(assets, meshPath);
        }
        registry.emplace<ModelComponent>(entity, std::move(component));
    }

    void LevelSerializer::loadMaterialComponent(entt::registry &registry, entt::entity entity, const Json &data, AssetManager &assets, const std::filesystem::path &projectRoot, const std::filesystem::path &assetRoot) {
        MaterialComponent component{};
        const std::string materialPath = assetVirtualPath(readString(data, "material"), projectRoot, assetRoot);
        if (!materialPath.empty()) {
            if (const auto inlineMaterial = data.find("inlineMaterial");
                inlineMaterial != data.end() && inlineMaterial->is_object() && materialPath.starts_with("##editor/materials/")) {
                auto material = std::make_shared<Material>();
                readInlineMaterial(*material, *inlineMaterial, assets, projectRoot, assetRoot);
                component.materialHandle = assets.store<Material>(std::move(material), materialPath);
            } else {
                component.materialHandle = loadMaterialHandle(assets, materialPath);
            }
        }
        registry.emplace<MaterialComponent>(entity, std::move(component));
    }

    void LevelSerializer::loadSkyboxComponent(entt::registry &registry, entt::entity entity, const Json &data, AssetManager &assets, const std::filesystem::path &projectRoot, const std::filesystem::path &assetRoot) {
        SkyboxComponent component{};
        const std::string skyboxPath = assetVirtualPath(readString(data, "skybox"), projectRoot, assetRoot);
        if (!skyboxPath.empty()) {
            component.skyboxHandle = assets.store<Cubemap>(skyboxPath);
        }

        const std::string irradiancePath = assetVirtualPath(readString(data, "irradiance"), projectRoot, assetRoot);
        if (!irradiancePath.empty()) {
            component.irradianceHandle = assets.store<Cubemap>(irradiancePath);
        }

        const std::string prefilterPath = assetVirtualPath(readString(data, "prefilter"), projectRoot, assetRoot);
        if (!prefilterPath.empty()) {
            component.prefilterHandle = assets.store<Cubemap>(prefilterPath);
        }
        registry.emplace<SkyboxComponent>(entity, std::move(component));
    }

    void LevelSerializer::loadLightComponent(
        entt::registry &registry,
        entt::entity entity,
        const Json &data) {
        LightComponent component{};
        component.type = lightTypeFromString(readString(data, "type"));
        component.color = readVector(data, "color", component.color);
        component.intensity = readFloat(data, "intensity", component.intensity);
        component.range = readFloat(data, "range", component.range);
        component.direction = readVector(data, "direction", component.direction);
        component.innerConeAngle = readFloat(data, "innerConeAngle", component.innerConeAngle);
        component.outerConeAngle = readFloat(data, "outerConeAngle", component.outerConeAngle);
        component.width = readFloat(data, "width", component.width);
        component.height = readFloat(data, "height", component.height);
        component.rectRight = readVector(data, "rectRight", component.rectRight);
        component.rectUp = readVector(data, "rectUp", component.rectUp);
        registry.emplace<LightComponent>(entity, std::move(component));
    }

    void LevelSerializer::loadPostProcessingComponent(
        entt::registry &registry,
        entt::entity entity,
        const Json &data) {
        PostProcessingVolumeComponent component{};
        component.exposure = readFloat(data, "exposure", component.exposure);
        component.contrast = readFloat(data, "contrast", component.contrast);
        component.saturation = readFloat(data, "saturation", component.saturation);
        component.colorTint = readVector(data, "colorTint", component.colorTint);
        component.bloomEnabled = readBool(data, "bloomEnabled", component.bloomEnabled);
        component.vignetteEnabled = readBool(data, "vignetteEnabled", component.vignetteEnabled);
        component.bloomStrength = readFloat(data, "bloomStrength", component.bloomStrength);
        component.vignetteStrength = readFloat(data, "vignetteStrength", component.vignetteStrength);
        registry.emplace<PostProcessingVolumeComponent>(entity, std::move(component));
    }

    void LevelSerializer::loadScriptComponent(entt::registry &registry, entt::entity entity, const Json &data) {
        ScriptComponent component{};
        const auto scripts = data.find("scripts");
        if (scripts != data.end() && scripts->is_array()) {
            for (const auto &scriptJson: *scripts) {
                ScriptBinding binding;
                binding.type = readString(scriptJson, "type");
                binding.enabled = readBool(scriptJson, "enabled", true);
                binding.properties = scriptJson.value("properties", Json::object());
                component.scripts.push_back(binding);
            }
        }
        registry.emplace<ScriptComponent>(entity, std::move(component));
    }

    std::string LevelSerializer::entityId(entt::entity entity) {
        return "entity_" + std::to_string(static_cast<std::uint32_t>(entity));
    }

    std::vector<entt::entity> LevelSerializer::collectEntities(const entt::registry &registry) {
        std::vector<entt::entity> entities;
        std::unordered_set<entt::entity> seen;
        for (const auto &&[id, storage]: registry.storage()) {
            for (const entt::entity entity: storage) {
                if (seen.insert(entity).second) {
                    entities.push_back(entity);
                }
            }
        }
        return entities;
    }

    void LevelSerializer::writePath(Json &data, const char *key, const std::string &path) {
        if (!path.empty()) {
            data[key] = path;
        }
    }

    entt::registry LevelSerializer::load(const std::filesystem::path &levelPath, AssetManager &assets, const std::filesystem::path &projectRoot, const std::filesystem::path &assetRoot) {
        const std::string contents = readFile(levelPath);

        Json data;
        try {
            data = Json::parse(contents);
        } catch (const Json::exception &e) {
            throw std::runtime_error("Failed to parse level JSON: " + std::string(e.what()));
        }

        if (!data.is_object()) {
            throw std::runtime_error("Level root must be a JSON object: " + levelPath.string());
        }

        const auto entitiesIt = data.find("entities");
        if (entitiesIt == data.end()) {
            throw std::runtime_error("Level is missing entities array: " + levelPath.string());
        }
        if (!entitiesIt->is_array()) {
            throw std::runtime_error("Level entities field must be an array: " + levelPath.string());
        }

        entt::registry registry;
        std::vector<std::pair<entt::entity, Json> > entityData;
        std::unordered_map<std::string, entt::entity> entityById;
        entityData.reserve(entitiesIt->size());

        size_t fallbackId = 0;
        for (const Json &entityJson: *entitiesIt) {
            if (!entityJson.is_object()) {
                throw std::runtime_error("Level entity entry must be an object: " + levelPath.string());
            }

            const entt::entity entity = registry.create();
            std::string id = readString(entityJson, "id");
            if (id.empty()) {
                id = "entity_" + std::to_string(fallbackId);
            }
            ++fallbackId;

            entityById.emplace(id, entity);
            entityData.emplace_back(entity, entityJson);
        }

        for (const auto &[entity, entityJson]: entityData) {
            SceneNodeComponent node{};
            node.name = readString(entityJson, "name", "Entity");
            node.visible = readBool(entityJson, "visible", node.visible);
            registry.emplace<SceneNodeComponent>(entity, std::move(node));

            const auto components = entityJson.find("components");
            if (components == entityJson.end() || components->is_null()) {
                continue;
            }

            if (!components->is_object()) {
                throw std::runtime_error("Level entity components field must be an object: " + levelPath.string());
            }

            if (const auto *component = componentData(*components, "Transform")) {
                registry.emplace<TransformComponent>(entity, component->get<TransformComponent>());
            }

            if (const auto *component = componentData(*components, "Camera")) {
                CameraComponent camera{};
                if (const auto projection = component->find("projection"); projection != component->end() && !projection->is_null()) {
                    camera.projection = projection->get<Camera::Projection>();
                }
                if (const auto renderMode = component->find("renderMode"); renderMode != component->end() && !renderMode->is_null()) {
                    camera.renderMode = renderMode->get<ViewMode>();
                }
                camera.perspectiveFovY = readFloat(*component, "perspectiveFovY", camera.perspectiveFovY);
                camera.orthographicHalfHeight = readFloat(*component, "orthographicHalfHeight", camera.orthographicHalfHeight);
                camera.nearPlane = readFloat(*component, "nearPlane", camera.nearPlane);
                camera.farPlane = readFloat(*component, "farPlane", camera.farPlane);
                registry.emplace<CameraComponent>(entity, std::move(camera));
            }

            if (const auto *component = componentData(*components, "Model")) {
                loadModelComponent(registry, entity, *component, assets, projectRoot, assetRoot);
            }

            if (const auto *component = componentData(*components, "Material")) {
                loadMaterialComponent(registry, entity, *component, assets, projectRoot, assetRoot);
            }

            if (const auto *component = componentData(*components, "Skybox")) {
                loadSkyboxComponent(registry, entity, *component, assets, projectRoot, assetRoot);
            }

            if (const auto *component = componentData(*components, "Light")) {
                loadLightComponent(registry, entity, *component);
            }

            if (const auto *component = componentData(*components, "PostProcessingVolume")) {
                loadPostProcessingComponent(registry, entity, *component);
            }

            if (const auto *component = componentData(*components, "Script")) {
                loadScriptComponent(registry, entity, *component);
            }
        }

        for (const auto &[entity, entityJson]: entityData) {
            const std::string parentId = readString(entityJson, "parent");
            if (parentId.empty()) {
                continue;
            }

            const auto parentIt = entityById.find(parentId);
            if (parentIt == entityById.end()) {
                throw std::runtime_error("Level entity references unknown parent: " + parentId);
            }

            SceneNodeComponent &node = registry.get<SceneNodeComponent>(entity);
            node.parent = parentIt->second;
            registry.get<SceneNodeComponent>(parentIt->second).children.push_back(entity);
        }

        return registry;
    }

    void LevelSerializer::save(const std::filesystem::path &levelPath, const entt::registry &registry) {
        const std::vector<entt::entity> entities = collectEntities(registry);

        Json data;
        data["version"] = 1;
        data["name"] = levelPath.stem().string();
        data["entities"] = Json::array();

        for (const entt::entity entity: entities) {
            if (registry.all_of<TransientComponent>(entity)) {
                continue;
            }

            const SceneNodeComponent *node = registry.try_get<SceneNodeComponent>(entity);
            if (node && node->deleted) {
                continue;
            }

            Json entityJson;
            entityJson["id"] = entityId(entity);

            if (node && !node->name.empty()) {
                entityJson["name"] = node->name;
            }
            if (node && node->parent != entt::null &&
                registry.valid(node->parent) &&
                (!registry.all_of<SceneNodeComponent>(node->parent) || !registry.get<SceneNodeComponent>(node->parent).deleted)) {
                entityJson["parent"] = entityId(node->parent);
            }
            if (node && !node->visible) {
                entityJson["visible"] = false;
            }

            Json components = Json::object();
            if (const auto *component = registry.try_get<TransformComponent>(entity)) {
                components["Transform"] = *component;
            }
            if (const auto *component = registry.try_get<CameraComponent>(entity)) {
                Json camera;
                camera["projection"] = component->projection;
                camera["renderMode"] = component->renderMode;
                camera["perspectiveFovY"] = component->perspectiveFovY;
                camera["orthographicHalfHeight"] = component->orthographicHalfHeight;
                camera["nearPlane"] = component->nearPlane;
                camera["farPlane"] = component->farPlane;
                components["Camera"] = camera;
            }
            if (const auto *component = registry.try_get<ModelComponent>(entity)) {
                Json model;
                writePath(model, "mesh", component->meshHandle.path());
                components["Model"] = model;
            }
            if (const auto *component = registry.try_get<MaterialComponent>(entity)) {
                Json material;
                writePath(material, "material", component->materialHandle.path());
                if (component->materialHandle.path().starts_with("##editor/materials/")) {
                    if (const Material *materialAsset = component->materialHandle.get()) {
                        material["inlineMaterial"] = writeInlineMaterial(*materialAsset);
                    }
                }
                components["Material"] = material;
            }
            if (const auto *component = registry.try_get<SkyboxComponent>(entity)) {
                Json skybox;
                writePath(skybox, "skybox", component->skyboxHandle.path());
                writePath(skybox, "irradiance", component->irradianceHandle.path());
                writePath(skybox, "prefilter", component->prefilterHandle.path());
                components["Skybox"] = skybox;
            }
            if (const auto *component = registry.try_get<LightComponent>(entity)) {
                Json light;
                light["type"] = lightTypeToString(component->type);
                light["color"] = component->color;
                light["intensity"] = component->intensity;
                light["range"] = component->range;
                light["direction"] = component->direction;
                light["innerConeAngle"] = component->innerConeAngle;
                light["outerConeAngle"] = component->outerConeAngle;
                light["width"] = component->width;
                light["height"] = component->height;
                light["rectRight"] = component->rectRight;
                light["rectUp"] = component->rectUp;
                components["Light"] = light;
            }
            if (const auto *component = registry.try_get<PostProcessingVolumeComponent>(entity)) {
                Json volume;
                volume["exposure"] = component->exposure;
                volume["contrast"] = component->contrast;
                volume["saturation"] = component->saturation;
                volume["colorTint"] = component->colorTint;
                volume["bloomEnabled"] = component->bloomEnabled;
                volume["vignetteEnabled"] = component->vignetteEnabled;
                volume["bloomStrength"] = component->bloomStrength;
                volume["vignetteStrength"] = component->vignetteStrength;
                components["PostProcessingVolume"] = volume;
            }
            if (const auto *component = registry.try_get<ScriptComponent>(entity)) {
                Json script;
                script["scripts"] = Json::array();
                for (const ScriptBinding &binding: component->scripts) {
                    Json bindingJson;
                    bindingJson["type"] = binding.type;
                    bindingJson["enabled"] = binding.enabled;
                    bindingJson["properties"] = binding.properties;
                    script["scripts"].push_back(bindingJson);
                }
                components["Script"] = script;
            }

            entityJson["components"] = components;
            data["entities"].push_back(entityJson);
        }

        std::ostringstream out;
        out << std::setw(2) << data << '\n';
        writeFile(levelPath, out.str());
    }
}
