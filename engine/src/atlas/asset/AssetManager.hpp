#pragma once

#include <algorithm>
#include <concepts>
#include <filesystem>
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
#include "entt/entity/entity.hpp"
#include "renderer/resources/GPUCubemap.hpp"
#include "renderer/resources/GPUMesh.hpp"
#include "renderer/resources/GPUTexture.hpp"


namespace Atlas {
    template<typename T>
    concept AssetType = std::same_as<T, Texture> || std::same_as<T, Mesh> || std::same_as<T, Cubemap>;

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
        ~AssetManager() = default;

        AssetManager(const AssetManager &) = delete;
        AssetManager &operator=(const AssetManager &) = delete;
        AssetManager(AssetManager &&) = delete;
        AssetManager &operator=(AssetManager &&) = delete;


        std::vector<entt::entity> importAsset(const std::string &virtualPath, entt::registry &registry, entt::entity parentEntity = entt::null);

        AssetHandle<Texture> createTexture(std::vector<std::byte> pixels, uint32_t width, uint32_t height, VkFormat format, VkSamplerAddressMode addressMode);
        AssetHandle<Texture> createTexturePlaceholder();
        void fulfillTexture(AssetHandle<Texture> handle, std::vector<std::byte> pixels, uint32_t width, uint32_t height, VkFormat format, VkSamplerAddressMode addressMode);
        AssetHandle<Mesh> createMesh(std::vector<Mesh::Vertex> vertices, std::vector<uint32_t> indices);
        AssetHandle<Cubemap> createCubemap(std::vector<std::byte> pixels, uint32_t width, uint32_t height, uint32_t mipLevels, VkFormat format, std::vector<VkBufferImageCopy> copyRegions);

        std::filesystem::path rootPath() const;
        void overwriteRootPath(const std::filesystem::path &path);

        void update();

        static std::vector<char> loadFileAsU8(const std::string &path);
        static void saveFileAsU8(const std::vector<char> &data, const std::string &path);
        static std::string loadFileAsString(const std::string &path);
        static void saveFileAsString(const std::string &data, const std::string &path);
    private:
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
    };
}

namespace std {
    template<Atlas::AssetType T>
    struct hash<Atlas::AssetHandle<T> > {
        size_t operator()(const Atlas::AssetHandle<T> &h) const noexcept {
            return std::hash<const void *>{}(h.identity());
        }
    };
}
