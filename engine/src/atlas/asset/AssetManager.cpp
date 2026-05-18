#include "AssetManager.hpp"

#include <algorithm>
#include <cctype>
#include <exception>
#include <filesystem>
#include <fstream>

#include "core/Log.hpp"
#include "accessors/GLTFAccessor.hpp"
#include "accessors/OBJAccessor.hpp"
#include "renderer/ResourceManager.hpp"
#include "renderer/resources/GPUCubemap.hpp"

namespace Atlas {
    std::filesystem::path resolveFilePath(const std::string &path) {
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

    AssetManager::AssetManager(ResourceManager &resourceManager, ExecutorService &executorService) : resourceManager_(resourceManager) {
        registerLoader<GLTFAccessor>(*this, executorService);
        registerLoader<OBJAccessor>(*this, executorService);
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

    AssetHandle<Texture> AssetManager::createTexture(std::vector<std::byte> pixels, uint32_t width, uint32_t height, VkFormat format, VkSamplerAddressMode addressMode) {
        auto asset = std::make_shared<Texture>(std::move(pixels), width, height, format, addressMode);
        const uint64_t hash = asset->getHash();

        std::lock_guard lock(pendingMutex_);

        if (auto it = textureByHash_.find(hash); it != textureByHash_.end()) {
            if (auto state = it->second.lock()) {
                AT_TRACE("Texture dedup hit (hash: {})", hash);
                return AssetHandle<Texture>(state);
            }
        }

        // New asset — create state, cache weak_ptr, queue GPU upload
        auto state = std::make_shared<typename AssetHandle<Texture>::State>();
        state->asset = std::move(asset);
        state->gpu = nullptr; // filled by update()

        textureByHash_[hash] = state;
        pendingTextures_.push_back(state);

        AT_TRACE("Created texture (hash: {}, {}x{})", hash, width, height);
        return AssetHandle<Texture>(state);
    }

    AssetHandle<Texture> AssetManager::createTexturePlaceholder() {
        auto state = std::make_shared<typename AssetHandle<Texture>::State>();
        return AssetHandle<Texture>(std::move(state));
    }

    void AssetManager::fulfillTexture(AssetHandle<Texture> handle, std::vector<std::byte> pixels, uint32_t width, uint32_t height, VkFormat format, VkSamplerAddressMode addressMode) {
        if (!handle.state_ || pixels.empty() || width == 0 || height == 0) {
            return;
        }

        auto asset = std::make_shared<Texture>(std::move(pixels), width, height, format, addressMode);
        const uint64_t hash = asset->getHash();

        std::lock_guard lock(pendingMutex_);

        if (handle.state_->asset || handle.state_->gpu) {
            return;
        }

        handle.state_->asset = std::move(asset);
        textureByHash_[hash] = handle.state_;
        pendingTextures_.push_back(handle.state_);

        AT_TRACE("Created texture (hash: {}, {}x{})", hash, width, height);
    }

    AssetHandle<Mesh> AssetManager::createMesh(std::vector<Mesh::Vertex> vertices, std::vector<uint32_t> indices) {
        auto asset = std::make_shared<Mesh>(std::move(vertices), std::move(indices));
        const uint64_t hash = asset->getHash();

        std::lock_guard lock(pendingMutex_);

        if (auto it = meshByHash_.find(hash); it != meshByHash_.end()) {
            if (auto state = it->second.lock()) {
                AT_TRACE("Mesh dedup hit (hash: {})", hash);
                return AssetHandle<Mesh>(state);
            }
        }

        auto state = std::make_shared<typename AssetHandle<Mesh>::State>();
        state->asset = std::move(asset);
        state->gpu = nullptr;

        meshByHash_[hash] = state;
        pendingMeshes_.push_back(state);

        AT_TRACE("Created mesh (hash: {}, {} verts, {} indices)", hash, state->asset->vertices().size(), state->asset->indices().size());
        return AssetHandle<Mesh>(state);
    }

    AssetHandle<Cubemap> AssetManager::createCubemap(std::vector<std::byte> pixels, uint32_t width, uint32_t height, uint32_t mipLevels, VkFormat format, std::vector<VkBufferImageCopy> copyRegions) {
        auto asset = std::make_shared<Cubemap>(std::move(pixels), width, height, mipLevels, format, std::move(copyRegions));
        const uint64_t hash = asset->getHash();

        std::lock_guard lock(pendingMutex_);

        if (auto it = cubemapByHash_.find(hash); it != cubemapByHash_.end()) {
            if (auto state = it->second.lock()) {
                AT_TRACE("Cubemap dedup hit (hash: {})", hash);
                return AssetHandle<Cubemap>(state);
            }
        }

        auto state = std::make_shared<typename AssetHandle<Cubemap>::State>();
        state->asset = std::move(asset);
        state->gpu = nullptr;

        cubemapByHash_[hash] = state;
        pendingCubemaps_.push_back(state);

        AT_TRACE("Created cubemap (hash: {}, {}x{})", hash, width, height);
        return AssetHandle<Cubemap>(state);
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

    std::vector<char> AssetManager::loadFileAsU8(const std::string &path) {
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

        std::vector<char> buffer(static_cast<size_t>(size));
        if (size > 0 && !file.read(buffer.data(), size)) {
            AT_ERROR("Failed to read file: {}", filePath.string());
            return {};
        }

        AT_TRACE("Loaded file: {} ({} bytes)", path, size);
        return buffer;
    }

    void AssetManager::saveFileAsU8(const std::vector<char> &data, const std::string &path) {
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
                out.write(data.data(), static_cast<std::streamsize>(data.size()));
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

    std::string AssetManager::loadFileAsString(const std::string &path) {
        const auto data = loadFileAsU8(path);
        return {data.begin(), data.end()};
    }

    void AssetManager::saveFileAsString(const std::string &data, const std::string &path) {
        const std::vector bytes(data.begin(), data.end());
        saveFileAsU8(bytes, path);
    }
} // namespace Atlas
