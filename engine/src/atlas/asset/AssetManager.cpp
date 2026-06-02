#include "AssetManager.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

#include <nlohmann/json.hpp>

#include "core/Log.hpp"
#include "renderer/ResourceManager.hpp"
#include "renderer/resources/GPUCubemap.hpp"

namespace Atlas {
    // ── paths.json mount overrides ────────────────────────────────────────────
    // Format (place paths.json next to the executable / working directory):
    //   {
    //     "engine": "E:/Atlas/engine/assets",
    //     "editor": "E:/Atlas/editor/assets"
    //   }
    // Each key is a ##mount name; the value is the full path to that mount's
    // assets root.  Missing keys fall back to the normal directory-walk search.

    namespace {
        struct MountOverrides {
            std::unordered_map<std::string, std::filesystem::path> table;
            bool loaded = false;
        };

        MountOverrides &mountOverrides() {
            static MountOverrides overrides;
            return overrides;
        }

        void ensureMountOverridesLoaded() {
            auto &mo = mountOverrides();
            if (mo.loaded) return;
            mo.loaded = true;

            // Search from the working directory upward for paths.json
            for (auto dir = std::filesystem::current_path(); !dir.empty(); dir = dir.parent_path()) {
                const auto candidate = dir / "paths.json";
                if (std::filesystem::exists(candidate)) {
                    try {
                        std::ifstream file(candidate);
                        const auto json = nlohmann::json::parse(file);
                        for (const auto &[key, value]: json.items()) {
                            if (value.is_string()) {
                                std::filesystem::path p(value.get<std::string>());
                                // Relative paths are resolved against the paths.json location,
                                // so the file works on any drive letter / mount point.
                                if (p.is_relative()) {
                                    p = (dir / p).lexically_normal();
                                }
                                mo.table[key] = p;
                                AT_INFO("AssetManager: mount '##{}' -> '{}'", key, p.string());
                            }
                        }
                    } catch (const std::exception &e) {
                        AT_ERROR("AssetManager: failed to parse paths.json: {}", e.what());
                    }
                    return;
                }
                if (dir == dir.root_path()) break;
            }
        }

        std::optional<std::filesystem::path> resolveMount(const std::string &mount, const std::filesystem::path &relativePath) {
            ensureMountOverridesLoaded();
            const auto &table = mountOverrides().table;
            if (const auto it = table.find(mount); it != table.end()) {
                return (it->second / relativePath).lexically_normal();
            }
            return std::nullopt;
        }
    }
    AssetManager::AssetManager(ResourceManager &resourceManager) : resourceManager_(resourceManager) {
    }

    AssetManager::~AssetManager() {
        clearCaches();
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
            if (!state || !state->asset) continue; // all handles dropped before upload - skip
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
            std::erase_if(materialByPath_, [](const auto &kv) { return kv.second.expired(); });
        }
    }

    void AssetManager::clearCaches() {
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
        materialByPath_.clear();
    }

    std::filesystem::path AssetManager::resolveAssetPath(const std::string &path) const {
        const std::filesystem::path filePath(path);
        if (filePath.is_absolute() || path.starts_with("##")) {
            return filePath;
        }

        if (!rootPath_.empty()) {
            std::string normalized = filePath.generic_string();
            const std::string rootName = rootPath_.filename().generic_string();
            if (!rootName.empty() && (normalized == rootName || normalized.starts_with(rootName + "/"))) {
                normalized = normalized.size() == rootName.size()
                    ? std::string{}
                    : normalized.substr(rootName.size() + 1);
            }

            return (rootPath_ / normalized).lexically_normal();
        }

        return filePath;
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

            // Check paths.json override first
            if (auto overridden = resolveMount(mount, relativePath)) {
                return *overridden;
            }

            // Built-in mounts: search upward for <mount>/assets/
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
