#pragma once

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Cubemap.hpp"
#include "EntityBuffer.hpp"
#include "AssetHandle.hpp"
#include "Material.hpp"
#include "Mesh.hpp"
#include "Texture.hpp"
#include "accessors/IAssetAccessor.hpp"
#include "core/Log.hpp"
#include "entt/entity/entity.hpp"


namespace Atlas {
    class ExecutorService;
    class ResourceManager;

    template<typename T>
    concept FileLoadable = (std::signed_integral<T> || std::unsigned_integral<T> || std::same_as<T, std::byte>) && !std::same_as<T, bool>;

    class AssetManager {
    public:
        AssetManager(ResourceManager &resourceManager, ExecutorService &executorService);
        ~AssetManager();

        AssetManager(const AssetManager &) = delete;
        AssetManager &operator=(const AssetManager &) = delete;
        AssetManager(AssetManager &&) = delete;
        AssetManager &operator=(AssetManager &&) = delete;


        std::vector<entt::entity> importAsset(const std::string &virtualPath, entt::registry &registry, entt::entity parentEntity = entt::null);
        std::future<void> importAsync(const std::string &virtualPath, entt::registry &registry);

        template<AssetType T>
        AssetHandle<T> store(std::shared_ptr<T> asset, const std::string &virtualPath);

        template<AssetType T>
        AssetHandle<T> store(const std::string &virtualPath);

        std::filesystem::path rootPath() const;
        void overwriteRootPath(const std::filesystem::path &path);
        std::vector<std::string> importerExtensions() const;

        void update();
        void clearCaches();

        template<FileLoadable T>
        static std::vector<T> loadFileAs(const std::string &path);
        template<FileLoadable T>
        static void saveFileFrom(const std::vector<T> &data, const std::string &path);
        static std::string loadFileAsString(const std::string &path);
        static void saveFileAsString(const std::string &data, const std::string &path);

    private:
        template<typename T>
        auto &hashMap() {
            if constexpr (std::same_as<T, Texture>) return textureByHash_;
            if constexpr (std::same_as<T, Mesh>) return meshByHash_;
            if constexpr (std::same_as<T, Cubemap>) return cubemapByHash_;
        }

        template<typename T>
        auto &pathMap() {
            if constexpr (std::same_as<T, Texture>) return textureByPath_;
            if constexpr (std::same_as<T, Mesh>) return meshByPath_;
            if constexpr (std::same_as<T, Cubemap>) return cubemapByPath_;
            if constexpr (std::same_as<T, Material>) return materialByPath_;
        }

        template<GPUAssetType T>
        auto &pendingList() {
            if constexpr (std::same_as<T, Texture>) return pendingTextures_;
            if constexpr (std::same_as<T, Mesh>) return pendingMeshes_;
            if constexpr (std::same_as<T, Cubemap>) return pendingCubemaps_;
        }

        template<typename T, typename... Args>
        void registerLoader(Args &&... args) {
            auto loader = std::make_shared<T>(std::forward<Args>(args)...);
            for (auto &ext: loader->extensions()) {
                accessors_.emplace(ext, loader);
            }
        }

        template<typename T>
        using WeakState = std::weak_ptr<typename AssetHandle<T>::State>;

        std::unordered_map<uint64_t, WeakState<Texture> > textureByHash_;
        std::unordered_map<uint64_t, WeakState<Mesh> > meshByHash_;
        std::unordered_map<uint64_t, WeakState<Cubemap> > cubemapByHash_;

        std::unordered_map<std::string, WeakState<Texture> > textureByPath_;
        std::unordered_map<std::string, WeakState<Mesh> > meshByPath_;
        std::unordered_map<std::string, WeakState<Cubemap> > cubemapByPath_;
        std::unordered_map<std::string, WeakState<Material> > materialByPath_;

        std::mutex pendingMutex_;
        std::vector<WeakState<Texture> > pendingTextures_;
        std::vector<WeakState<Mesh> > pendingMeshes_;
        std::vector<WeakState<Cubemap> > pendingCubemaps_;

        ResourceManager &resourceManager_;
        ExecutorService &executor_;
        std::filesystem::path rootPath_;
        std::unordered_map<std::string, std::shared_ptr<IAccessor> > accessors_;

        std::mutex pendingFlushMutex_;
        std::vector<std::pair<EntityBuffer, entt::registry*>> pendingFlushes_;

        static std::filesystem::path resolveFilePath(const std::string &path);
    };

    template<AssetType T>
    AssetHandle<T> AssetManager::store(std::shared_ptr<T> asset, const std::string &virtualPath) {
        std::lock_guard lock(pendingMutex_);

        // 1. Dedup by path — if this exact path is already tracked and still alive, return it
        auto &byPath = pathMap<T>();
        if (auto it = byPath.find(virtualPath); it != byPath.end()) {
            if (auto state = it->second.lock()) {
                AT_TRACE("Asset dedup hit (path: {})", virtualPath);
                return AssetHandle<T>(state);
            }
        }

        if constexpr (GPUAssetType<T>) {
            const uint64_t hash = asset->getHash();

            // 2. Dedup by hash — same content under a different path
            auto &byHash = hashMap<T>();
            if (auto it = byHash.find(hash); it != byHash.end()) {
                if (auto state = it->second.lock()) {
                    AT_TRACE("Asset dedup hit (hash: {}), aliasing path: {}", hash, virtualPath);
                    byPath[virtualPath] = state; // register the new path alias
                    return AssetHandle<T>(state);
                }
            }

            // 3. New GPU asset
            auto state = std::make_shared<typename AssetHandle<T>::State>();
            state->asset = std::move(asset);
            state->gpu = nullptr; // filled by update()

            byHash[hash] = state;
            byPath[virtualPath] = state;
            pendingList<T>().push_back(state);

            AT_TRACE("Stored asset (path: {}, hash: {})", virtualPath, hash);
            return AssetHandle<T>(state);
        } else {
            // CPU assets are mutable, so path identity is the stable key.
            auto state = std::make_shared<typename AssetHandle<T>::State>();
            state->asset = std::move(asset);
            state->gpu = nullptr;
            byPath[virtualPath] = state;

            AT_TRACE("Stored CPU asset (path: {})", virtualPath);
            return AssetHandle<T>(state);
        }
    }

    template<AssetType T>
    AssetHandle<T> AssetManager::store(const std::string &virtualPath) {
        if constexpr (std::is_same_v<T, Texture>) {
            return store(Texture::fromFile(virtualPath), virtualPath);
        } else if constexpr (std::is_same_v<T, Cubemap>) {
            return store(Cubemap::fromFile(virtualPath), virtualPath);
        } else {
            static_assert(std::same_as<T, Texture> || std::same_as<T, Cubemap>, "AssetManager::store(path) only supports file-loadable asset types");
        }
    }

    template<FileLoadable T>
    std::vector<T> AssetManager::loadFileAs(const std::string &path) {
        const std::filesystem::path filePath = resolveFilePath(path);

        std::ifstream file(filePath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            AT_ERROR("Failed to open file: {}", filePath.string());
            return {};
        }

        const std::streamsize size = file.tellg();
        if (size < 0) {
            AT_ERROR("Failed to determine file size: {}", filePath.string());
            return {};
        }

        file.seekg(0, std::ios::beg);

        std::vector<T> buffer(static_cast<size_t>(size));
        if (size > 0 && !file.read(reinterpret_cast<char *>(buffer.data()), size)) {
            AT_ERROR("Failed to read file: {}", filePath.string());
            return {};
        }

        return buffer;
    }

    template<FileLoadable T>
    void AssetManager::saveFileFrom(const std::vector<T> &data, const std::string &path) {
        try {
            const std::filesystem::path filePath = resolveFilePath(path);

            if (filePath.has_parent_path()) {
                std::filesystem::create_directories(filePath.parent_path());
            }

            std::ofstream out(filePath, std::ios::binary);
            if (!out.is_open()) {
                AT_ERROR("Failed to open file for writing: {}", filePath.string());
                return;
            }

            if (!data.empty()) {
                out.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
                if (!out) {
                    AT_ERROR("Failed to write data to file: {}", filePath.string());
                    return;
                }
            }

            AT_TRACE("Saved {} bytes to {}", data.size(), filePath.string());
        } catch (const std::exception &e) {
            AT_ERROR("Exception while saving file: {}", e.what());
        }
    }
}
