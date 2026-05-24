#include "AssetManager.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <utility>

#include "core/Log.hpp"
#include "accessors/GLTFAccessor.hpp"
#include "accessors/OBJAccessor.hpp"
#include "renderer/ResourceManager.hpp"
#include "renderer/resources/GPUCubemap.hpp"

namespace Atlas {
    AssetManager::AssetManager(ResourceManager &resourceManager, ExecutorService &executorService) : resourceManager_(resourceManager), executor_(executorService) {
        registerLoader<GLTFAccessor>(*this, executorService);
        registerLoader<OBJAccessor>(*this, executorService);
    }

    AssetManager::~AssetManager() {
        clearCaches();
    }

    std::vector<entt::entity> AssetManager::importAsset(const std::string &virtualPath, entt::registry &registry, entt::entity parentEntity) {
        (void) parentEntity;

        std::string ext = std::filesystem::path(virtualPath).extension().string();
        std::ranges::transform(ext, ext.begin(), ::tolower);

        auto it = accessors_.find(ext);
        if (it == accessors_.end()) {
            AT_ERROR("No loader registered for extension: {}", ext);
            return {};
        }

        EntityBuffer buffer;
        it->second->importAsset(virtualPath, buffer);
        return buffer.flush(registry);
    }

    std::future<void> AssetManager::importAsync(const std::string &virtualPath, entt::registry &registry) {
        std::string ext = std::filesystem::path(virtualPath).extension().string();
        std::ranges::transform(ext, ext.begin(), ::tolower);

        auto it = accessors_.find(ext);
        if (it == accessors_.end()) {
            AT_ERROR("No loader registered for extension: {}", ext);
            return {};
        }

        auto accessor = it->second;
        return executor_.submit([this, accessor = std::move(accessor), virtualPath, registry = &registry]() {
            EntityBuffer buffer;
            accessor->importAsset(virtualPath, buffer);

            std::lock_guard lock(pendingFlushMutex_);
            pendingFlushes_.emplace_back(std::move(buffer), registry);
        });
    }

    std::filesystem::path AssetManager::rootPath() const {
        return rootPath_;
    }

    void AssetManager::overwriteRootPath(const std::filesystem::path &path) {
        rootPath_ = path;
    }

    std::vector<std::string> AssetManager::importerExtensions() const {
        std::vector<std::string> extensions;
        extensions.reserve(accessors_.size());

        for (const auto &[extension, accessor]: accessors_) {
            (void)accessor;
            extensions.push_back(extension);
        }

        std::ranges::sort(extensions);
        return extensions;
    }

    void AssetManager::update() {
        constexpr size_t maxGpuCreatesPerFrame = 1;

        std::vector<std::pair<EntityBuffer, entt::registry*>> pendingFlushes;
        {
            std::lock_guard lock(pendingFlushMutex_);
            pendingFlushes.swap(pendingFlushes_);
        }

        for (auto &[buffer, registry]: pendingFlushes) {
            if (registry) {
                buffer.flush(*registry);
            }
        }

        std::vector<WeakState<Texture> > textures;
        std::vector<WeakState<Mesh> > meshes;
        std::vector<WeakState<Cubemap> > cubemaps;

        {
            std::lock_guard lock(pendingMutex_);
            size_t remaining = maxGpuCreatesPerFrame;

            auto takePending = [&remaining](auto &source, auto &destination) {
                while (remaining > 0 && !source.empty()) {
                    destination.push_back(std::move(source.back()));
                    source.pop_back();
                    --remaining;
                }
            };

            takePending(pendingMeshes_, meshes);
            takePending(pendingTextures_, textures);
            takePending(pendingCubemaps_, cubemaps);
        }

        for (auto &weak: meshes) {
            auto state = weak.lock();
            if (!state || !state->asset) continue;
            auto gpu = resourceManager_.add(state->asset);
            state->gpu = gpu;
        }

        for (auto &weak: textures) {
            auto state = weak.lock();
            if (!state || !state->asset) continue; // all handles dropped before upload — skip
            auto gpu = resourceManager_.add(state->asset);
            state->gpu = gpu;
            for (const auto &slot: state->bindlessSlots) {
                static_cast<GPUTexture &>(*state->gpu).registerBindlessSlot(slot.device, slot.set, slot.binding, slot.arrayElement);
            }
        }

        for (auto &weak: cubemaps) {
            auto state = weak.lock();
            if (!state || !state->asset) continue;
            auto gpu = resourceManager_.add(state->asset);
            state->gpu = gpu;
            for (const auto &slot: state->bindlessSlots) {
                static_cast<GPUCubemap &>(*state->gpu).registerBindlessSlot(slot.device, slot.set, slot.binding, slot.arrayElement);
            }
        }

        resourceManager_.update();

        {
            std::lock_guard lock(pendingMutex_);

            // Prune expired weak_ptrs from dedup caches. These maps are also
            // touched by async import jobs through store(), so keep pruning
            // under the same lock as insertion/deduplication.
            std::erase_if(textureByHash_, [](const auto &kv) { return kv.second.expired(); });
            std::erase_if(meshByHash_, [](const auto &kv) { return kv.second.expired(); });
            std::erase_if(cubemapByHash_, [](const auto &kv) { return kv.second.expired(); });
            std::erase_if(textureByPath_, [](const auto &kv) { return kv.second.expired(); });
            std::erase_if(meshByPath_, [](const auto &kv) { return kv.second.expired(); });
            std::erase_if(cubemapByPath_, [](const auto &kv) { return kv.second.expired(); });
        }
    }

    void AssetManager::clearCaches() {
        {
            std::lock_guard lock(pendingFlushMutex_);
            pendingFlushes_.clear();
        }

        std::lock_guard lock(pendingMutex_);

        pendingTextures_.clear();
        pendingMeshes_.clear();
        pendingCubemaps_.clear();

        textureByHash_.clear();
        meshByHash_.clear();
        cubemapByHash_.clear();

        textureByPath_.clear();
        meshByPath_.clear();
        cubemapByPath_.clear();
    }

    std::string AssetManager::loadFileAsString(const std::string &path) {
        const auto data = loadFileAs<uint8_t>(path);
        return {data.begin(), data.end()};
    }

    void AssetManager::saveFileAsString(const std::string &data, const std::string &path) {
        std::vector<std::byte> bytes(data.size());
        std::memcpy(bytes.data(), data.data(), data.size());
        saveFileFrom(bytes, path);
    }

    std::filesystem::path AssetManager::resolveFilePath(const std::string &path) {
        std::filesystem::path filePath(path);
        if (filePath.is_absolute()) {
            return filePath;
        }

        if (path.starts_with("##")) {
            const size_t separator = path.find_first_of("/\\", 2);
            const std::string mount = separator == std::string::npos
                                          ? path.substr(2)
                                          : path.substr(2, separator - 2);
            const std::filesystem::path relativePath = separator == std::string::npos
                                                           ? std::filesystem::path{}
                                                           : std::filesystem::path(path.substr(separator + 1));

            std::filesystem::path assetDirectory;
            if (mount == "engine") {
                assetDirectory = "engine";
            } else if (mount == "editor") {
                assetDirectory = "editor";
            } else {
                AT_ERROR("Unknown asset namespace: {}", mount);
                return std::filesystem::current_path() / filePath;
            }

            for (auto directory = std::filesystem::current_path(); !directory.empty(); directory = directory.parent_path()) {
                const auto candidateRoot = directory / assetDirectory / "assets";
                if (std::filesystem::exists(candidateRoot)) {
                    return candidateRoot / relativePath;
                }

                if (directory == directory.root_path()) {
                    break;
                }
            }

            return std::filesystem::current_path() / assetDirectory / "assets" / relativePath;
        }

        for (auto directory = std::filesystem::current_path(); !directory.empty(); directory = directory.parent_path()) {
            const auto candidate = directory / "assets" / filePath;
            if (std::filesystem::exists(candidate)) {
                return candidate;
            }

            if (directory == directory.root_path()) {
                break;
            }
        }

        return std::filesystem::current_path() / "assets" / filePath;
    }
} // namespace Atlas
