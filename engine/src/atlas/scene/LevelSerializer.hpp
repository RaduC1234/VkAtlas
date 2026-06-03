#pragma once

#include <filesystem>

#include <entt/entity/registry.hpp>

#include "json.hpp"

namespace Atlas {
    enum class LightType : uint32_t;
    class Material;
    class AssetManager;

    class LevelSerializer {
    public:
        static entt::registry load(const std::filesystem::path &levelPath, AssetManager &assets, const std::filesystem::path &projectRoot = {}, const std::filesystem::path &assetRoot = {});
        static void save(const std::filesystem::path &levelPath, const entt::registry &registry);

    private:
        using Json = nlohmann::json;

        static std::string readFile(const std::filesystem::path &path);
        static void writeFile(const std::filesystem::path &path, const std::string &contents);

        static std::string readString(const Json &data, const char *key, std::string fallback = {});
        static bool readBool(const Json &data, const char *key, bool fallback);
        static float readFloat(const Json &data, const char *key, float fallback);
        template<typename T>
        static T readVector(const Json &data, const char *key, const T &fallback);

        static bool startsWithPathPrefix(const std::string &path, const std::string &prefix);
        static std::string normalizeSlashes(std::string path);
        static std::string assetVirtualPath(const std::string &serializedPath, const std::filesystem::path &projectRoot, const std::filesystem::path &assetRoot);

        static const Json *componentData(const Json &components, const char *name);

        static std::string lightTypeToString(LightType type);
        static LightType lightTypeFromString(const std::string &value);
        static const char *shadingModelToString(uint32_t value);
        static uint32_t shadingModelFromString(const std::string &value);
        static const char *alphaModeToString(uint32_t value);
        static uint32_t alphaModeFromString(const std::string &value);
        static Json writeInlineMaterial(const Material &material);
        static void readInlineMaterial(Material &material, const Json &data, AssetManager &assets, const std::filesystem::path &projectRoot, const std::filesystem::path &assetRoot);

        static void loadModelComponent(entt::registry &registry, entt::entity entity, const Json &data, AssetManager &assets, const std::filesystem::path &projectRoot, const std::filesystem::path &assetRoot);
        static void loadMaterialComponent(entt::registry &registry, entt::entity entity, const Json &data, AssetManager &assets, const std::filesystem::path &projectRoot, const std::filesystem::path &assetRoot);
        static void loadSkyboxComponent(entt::registry &registry, entt::entity entity, const Json &data, AssetManager &assets, const std::filesystem::path &projectRoot, const std::filesystem::path &assetRoot);
        static void loadLightComponent(entt::registry &registry, entt::entity entity, const Json &data);
        static void loadPostProcessingComponent(entt::registry &registry, entt::entity entity, const Json &data);
        static void loadScriptComponent(entt::registry &registry, entt::entity entity, const Json &data);

        // Entity utilities
        static std::string entityId(entt::entity entity);
        static std::vector<entt::entity> collectEntities(const entt::registry &registry);
        static void writePath(Json &data, const char *key, const std::string &path);
    };
}
