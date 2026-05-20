#include "AssetManager.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>

#include "core/Log.hpp"
#include "accessors/GLTFAccessor.hpp"
#include "accessors/OBJAccessor.hpp"
#include "renderer/ResourceManager.hpp"
#include "renderer/resources/GPUCubemap.hpp"

namespace Atlas {
    AssetManager::AssetManager(ResourceManager &resourceManager, ExecutorService &executorService) : resourceManager_(resourceManager) {
        registerLoader<GLTFAccessor>(*this, executorService);
        registerLoader<OBJAccessor>(*this, executorService);
    }

    AssetManager::~AssetManager() {
    }

    std::vector<entt::entity> AssetManager::importAsset(const std::string &virtualPath, entt::registry &registry, entt::entity parentEntity) {
        std::string ext = std::filesystem::path(virtualPath).extension().string();
        std::ranges::transform(ext, ext.begin(), ::tolower);

        auto it = accessors_.find(ext);
        if (it == accessors_.end()) {
            AT_ERROR("No loader registered for extension: {}", ext);
            return {};
        }

        return it->second->importAsset(virtualPath, registry, parentEntity);
    }

    std::filesystem::path AssetManager::rootPath() const {
        return rootPath_;
    }

    void AssetManager::overwriteRootPath(const std::filesystem::path &path) {
        rootPath_ = path;
    }

    void AssetManager::update() {
        constexpr size_t maxGpuCreatesPerFrame = 1;

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

        // Prune expired weak_ptrs from dedup caches
        std::erase_if(textureByHash_, [](const auto &kv) { return kv.second.expired(); });
        std::erase_if(meshByHash_, [](const auto &kv) { return kv.second.expired(); });
        std::erase_if(cubemapByHash_, [](const auto &kv) { return kv.second.expired(); });
        std::erase_if(textureByPath_, [](const auto &kv) { return kv.second.expired(); });
        std::erase_if(meshByPath_, [](const auto &kv) { return kv.second.expired(); });
        std::erase_if(cubemapByPath_, [](const auto &kv) { return kv.second.expired(); });
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
