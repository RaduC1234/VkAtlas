#include "scene/LevelSerializer.hpp"

#include <algorithm>
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
            component.meshHandle = assets.store<Mesh>(meshPath);
        }
        registry.emplace<ModelComponent>(entity, std::move(component));
    }

    void LevelSerializer::loadMaterialComponent(entt::registry &registry, entt::entity entity, const Json &data, AssetManager &assets, const std::filesystem::path &projectRoot, const std::filesystem::path &assetRoot) {
        MaterialComponent component{};
        const std::string materialPath = assetVirtualPath(readString(data, "material"), projectRoot, assetRoot);
        if (!materialPath.empty()) {
            component.materialHandle = assets.store<Material>(materialPath);
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

            if (componentData(*components, "Camera")) {
                registry.emplace<CameraComponent>(entity);
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
            if (registry.all_of<CameraComponent>(entity)) {
                components["Camera"] = Json::object();
            }
            if (const auto *component = registry.try_get<ModelComponent>(entity)) {
                Json model;
                writePath(model, "mesh", component->meshHandle.path());
                components["Model"] = model;
            }
            if (const auto *component = registry.try_get<MaterialComponent>(entity)) {
                Json material;
                writePath(material, "material", component->materialHandle.path());
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
