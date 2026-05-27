#pragma once

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#include <vulkan/vulkan.h>

#include "renderer/resources/GPUCubemap.hpp"
#include "renderer/resources/GPUMesh.hpp"
#include "renderer/resources/GPUTexture.hpp"

namespace Atlas {
    class AssetManager;
    class Cubemap;
    class Material;
    class Mesh;
    class Texture;

    template<typename T>
    concept GPUAssetType = std::same_as<T, Texture> || std::same_as<T, Mesh> || std::same_as<T, Cubemap>;

    template<typename T>
    concept CPUAssetType = std::same_as<T, Material>;

    template<typename T>
    concept AssetType = GPUAssetType<T> || CPUAssetType<T>;

    template<AssetType T>
    class AssetHandle {
    public:
        AssetHandle() = default;

        static AssetHandle invalid() { return {}; }

        bool valid() const { return state_ != nullptr; }

        explicit operator bool() const { return valid(); }

        bool isReady() const {
            if constexpr (GPUAssetType<T>) {
                return state_ && state_->gpu && state_->gpu->isReady();
            } else {
                return state_ && state_->asset != nullptr;
            }
        }

        T *get() { return state_ ? state_->asset.get() : nullptr; }
        T *get() const { return state_ ? state_->asset.get() : nullptr; }
        T *operator->() { return get(); }
        T *operator->() const { return get(); }
        T &operator*() { return *get(); }
        T &operator*() const { return *get(); }

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

            std::shared_ptr<T> asset;
            std::shared_ptr<IGPUResource> gpu;
            std::vector<BindlessSlot> bindlessSlots;
        };

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
}

namespace std {
    template<Atlas::AssetType T>
    struct hash<Atlas::AssetHandle<T> > {
        size_t operator()(const Atlas::AssetHandle<T> &h) const noexcept {
            return std::hash<const void *>{}(h.identity());
        }
    };
}
