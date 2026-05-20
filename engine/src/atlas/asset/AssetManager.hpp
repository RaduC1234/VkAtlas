#pragma once

#include <algorithm>
#include <concepts>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Cubemap.hpp"
#include "Mesh.hpp"
#include "Texture.hpp"
#include "accessors/IAssetAccessor.hpp"
#include "core/Log.hpp"
#include "entt/entity/entity.hpp"
#include "renderer/resources/GPUCubemap.hpp"
#include "renderer/resources/GPUMesh.hpp"
#include "renderer/resources/GPUTexture.hpp"


namespace Atlas {
    template<typename T>
    concept AssetType = std::same_as<T, Texture> || std::same_as<T, Mesh> || std::same_as<T, Cubemap>;

    template<typename T>
    concept FileLoadable = (std::signed_integral<T> || std::unsigned_integral<T> || std::same_as<T, std::byte>) && !std::same_as<T, bool>;

    template<AssetType T>
    class AssetHandle {
    public:
        AssetHandle() = default;

        static AssetHandle invalid() { return {}; }

        bool valid() const { return state_ != nullptr; }

        explicit operator bool() const { return valid(); }

        // True if GPU upload is complete and resource is safe to sample/bind
        bool isReady() const {
            return state_ && state_->gpu && state_->gpu->isReady();
        }

        const T *get() const { return state_ ? state_->asset.get() : nullptr; }
        const T *operator->() const { return get(); }
        const T &operator*() const { return *get(); }

        VkDescriptorImageInfo descriptor() const requires std::same_as<T, Texture> || std::same_as<T, Cubemap> {
            if (!state_ || !state_->gpu || !state_->gpu->isReady()) {
                if constexpr (std::same_as<T, Texture>) {
                    return IGPUResource::default_<GPUTexture>().descriptor();
                } else {
                    return IGPUResource::default_<GPUCubemap>().descriptor();
                }
            }

            auto &gpu = static_cast<std::conditional_t<std::same_as<T, Texture>, GPUTexture, GPUCubemap> &>(*state_->gpu);
            return {gpu.getSampler(), gpu.getImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        }

        void registerBindlessSlot(VkDevice device, VkDescriptorSet set, uint32_t binding, uint32_t arrayElement) const requires std::same_as<T, Texture> || std::same_as<T, Cubemap>;

        VkDescriptorBufferInfo vertexBufferInfo() const requires std::same_as<T, Mesh> {
            if (!state_ || !state_->gpu || !isReady()) return {};
            auto &gpu = static_cast<GPUMesh &>(*state_->gpu);
            return {gpu.getVertexBuffer(), 0, VK_WHOLE_SIZE};
        }

        VkDescriptorBufferInfo indexBufferInfo() const requires std::same_as<T, Mesh> {
            if (!state_ || !state_->gpu || !isReady()) return {};
            auto &gpu = static_cast<GPUMesh &>(*state_->gpu);
            return {gpu.getIndexBuffer(), 0, VK_WHOLE_SIZE};
        }

        void bind(VkCommandBuffer cmd) const requires std::same_as<T, Mesh> {
            if (!state_ || !state_->gpu || !isReady()) return;
            static_cast<GPUMesh &>(*state_->gpu).bind(cmd);
        }

        void draw(VkCommandBuffer cmd) const requires std::same_as<T, Mesh> {
            if (!state_ || !state_->gpu || !isReady()) return;
            static_cast<GPUMesh &>(*state_->gpu).draw(cmd);
        }

        VkDeviceAddress blasAddress() const requires std::same_as<T, Mesh> {
            if (!state_ || !state_->gpu || !isReady()) return 0;
            return static_cast<GPUMesh &>(*state_->gpu).accelerationStructure().deviceAddress();
        }

        void buildAccelerationStructure() const requires std::same_as<T, Mesh> {
            if (!state_ || !state_->gpu || !isReady()) return;
            static_cast<GPUMesh &>(*state_->gpu).buildAccelerationStructure();
        }

        // -------------------------------------------------------------------------
        // Comparison — handles to the same asset are equal
        // -------------------------------------------------------------------------
        bool operator==(const AssetHandle &other) const {
            return state_ == other.state_;
        }

        bool operator!=(const AssetHandle &other) const {
            return state_ != other.state_;
        }

        const void *identity() const {
            return state_.get();
        }

    private:
        friend class AssetManager;

        struct State {
            struct BindlessSlot {
                VkDevice device = VK_NULL_HANDLE;
                VkDescriptorSet set = VK_NULL_HANDLE;
                uint32_t binding = 0;
                uint32_t arrayElement = 0;
            };

            std::shared_ptr<T> asset; // CPU data
            std::shared_ptr<IGPUResource> gpu; // null until AssetManager::update() runs
            std::vector<BindlessSlot> bindlessSlots;
        };

        // Only AssetManager can construct a valid Handle
        explicit AssetHandle(std::shared_ptr<State> state) : state_(std::move(state)) {
        }

        std::shared_ptr<State> state_;
    };

    template<AssetType T>
    void AssetHandle<T>::registerBindlessSlot(VkDevice device, VkDescriptorSet set, uint32_t binding, uint32_t arrayElement) const requires std::same_as<T, Texture> || std::same_as<T, Cubemap> {
        if (!state_) return;

        typename AssetHandle<T>::State::BindlessSlot slot{
            .device = device,
            .set = set,
            .binding = binding,
            .arrayElement = arrayElement,
        };

        const auto it = std::ranges::find_if(state_->bindlessSlots, [&](const typename AssetHandle<T>::State::BindlessSlot &other) {
            return other.device == slot.device &&
                   other.set == slot.set &&
                   other.binding == slot.binding &&
                   other.arrayElement == slot.arrayElement;
        });

        if (it == state_->bindlessSlots.end()) {
            state_->bindlessSlots.push_back(slot);
        }

        if (!state_->gpu) return;

        if constexpr (std::same_as<T, Texture>) {
            static_cast<GPUTexture &>(*state_->gpu).registerBindlessSlot(device, set, binding, arrayElement);
        } else {
            static_cast<GPUCubemap &>(*state_->gpu).registerBindlessSlot(device, set, binding, arrayElement);
        }
    }

    class AssetManager {
    public:
        AssetManager(ResourceManager &resourceManager, ExecutorService &executorService);
        ~AssetManager();

        AssetManager(const AssetManager &) = delete;
        AssetManager &operator=(const AssetManager &) = delete;
        AssetManager(AssetManager &&) = delete;
        AssetManager &operator=(AssetManager &&) = delete;


        std::vector<entt::entity> importAsset(const std::string &virtualPath, entt::registry &registry, entt::entity parentEntity = entt::null);

        template<AssetType T>
        AssetHandle<T> store(std::shared_ptr<T> asset, const std::string &virtualPath);

        template<AssetType T>
        AssetHandle<T> store(const std::string &virtualPath);

        std::filesystem::path rootPath() const;
        void overwriteRootPath(const std::filesystem::path &path);

        void update();

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
        }

        template<typename T>
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

        std::mutex pendingMutex_;
        std::vector<WeakState<Texture> > pendingTextures_;
        std::vector<WeakState<Mesh> > pendingMeshes_;
        std::vector<WeakState<Cubemap> > pendingCubemaps_;

        ResourceManager &resourceManager_;
        std::filesystem::path rootPath_;
        std::unordered_map<std::string, std::shared_ptr<IAccessor> > accessors_;

        static std::filesystem::path resolveFilePath(const std::string &path);
    };

    template<AssetType T>
    AssetHandle<T> AssetManager::store(std::shared_ptr<T> asset, const std::string &virtualPath) {
        const uint64_t hash = asset->getHash();

        std::lock_guard lock(pendingMutex_);

        // 1. Dedup by path — if this exact path is already tracked and still alive, return it
        auto &byPath = pathMap<T>();
        if (auto it = byPath.find(virtualPath); it != byPath.end()) {
            if (auto state = it->second.lock()) {
                AT_TRACE("Asset dedup hit (path: {})", virtualPath);
                return AssetHandle<T>(state);
            }
        }

        // 2. Dedup by hash — same content under a different path
        auto &byHash = hashMap<T>();
        if (auto it = byHash.find(hash); it != byHash.end()) {
            if (auto state = it->second.lock()) {
                AT_TRACE("Asset dedup hit (hash: {}), aliasing path: {}", hash, virtualPath);
                byPath[virtualPath] = state; // register the new path alias
                return AssetHandle<T>(state);
            }
        }

        // 3. New asset
        auto state = std::make_shared<typename AssetHandle<T>::State>();
        state->asset = std::move(asset);
        state->gpu = nullptr; // filled by update()

        byHash[hash] = state;
        byPath[virtualPath] = state;
        pendingList<T>().push_back(state);

        AT_TRACE("Stored asset (path: {}, hash: {})", virtualPath, hash);
        return AssetHandle<T>(state);
    }

    template<AssetType T>
    AssetHandle<T> AssetManager::store(const std::string &virtualPath) {
        if constexpr (std::is_same_v<T, Texture>) {
            return store(Texture::fromFile(virtualPath), virtualPath);
        } else if constexpr (std::is_same_v<T, Cubemap>) {
            return store(Cubemap::fromFile(virtualPath), virtualPath);
        }

        return AssetHandle<T>::invalid();
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

namespace std {
    template<Atlas::AssetType T>
    struct hash<Atlas::AssetHandle<T> > {
        size_t operator()(const Atlas::AssetHandle<T> &h) const noexcept {
            return std::hash<const void *>{}(h.identity());
        }
    };
}
