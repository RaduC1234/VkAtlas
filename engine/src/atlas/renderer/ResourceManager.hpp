#pragma once

#include <concepts>
#include <memory>
#include <optional>
#include <type_traits>
#include <vector>

#include "asset/Cubemap.hpp"
#include "asset/Mesh.hpp"
#include "asset/Texture.hpp"
#include "resources/IGPUResource.hpp"
#include "renderer/Device.hpp"
#include "resources/GPUCubemap.hpp"
#include "resources/GPUMesh.hpp"
#include "resources/GPUTexture.hpp"

namespace Atlas {
    template<typename T>
    concept GPUUploadable = std::same_as<T, Texture> || std::same_as<T, Mesh> || std::same_as<T, Cubemap>;

    class ResourceManager {
    public:
        explicit ResourceManager(Device &device);
        void createDefaults();
        ~ResourceManager();

        ResourceManager(const ResourceManager &) = delete;
        ResourceManager &operator=(const ResourceManager &) = delete;
        ResourceManager(ResourceManager &&) = delete;
        ResourceManager &operator=(ResourceManager &&) = delete;

        // -------------------------------------------------------------------------
        // AssetManager interface — called on AssetManager thread
        // -------------------------------------------------------------------------

        // Phase 1 — allocates GPU objects immediately (VkImage, VkImageView, VkSampler)
        // and fills staging buffer. Pure CPU, returns immediately.
        // Queues upload for next update() call.
        // Status starts as PENDING_UPLOAD — getCurrentImageView() returns default.
        template<GPUUploadable T>
        std::shared_ptr<IGPUResource> add(std::shared_ptr<T> asset);

        // Marks PENDING_DESTROY and defers GPU destruction until update().
        // If still in uploadQueue — removed immediately.
        void remove(std::shared_ptr<IGPUResource> resource);

        // Uploads pending resources and retires pending destroys.
        void update();

    private:
        struct UploadEntry {
            std::shared_ptr<IGPUResource> resource;
            std::shared_ptr<void> capturedAsset; // keeps CPU asset alive until upload finishes
        };

        struct PendingDestroy {
            std::shared_ptr<IGPUResource> resource;
            uint64_t timelineValue;
        };

        struct InFlightUpload {
            VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
            VkFence fence = VK_NULL_HANDLE;
            std::vector<UploadEntry> batch;
        };

        void pollUpload();
        void retirePendingDestroys();

        Device &device_;

        // Upload queue — AssetManager thread only, no mutex needed
        std::vector<UploadEntry> uploadQueue_;
        std::optional<InFlightUpload> inFlightUpload_;

        // Destroy queue — AssetManager thread only, no mutex needed
        std::vector<PendingDestroy> destroyQueue_;
    };

    // -------------------------------------------------------------------------
    // Template implementation
    // -------------------------------------------------------------------------

    template<GPUUploadable T>
    std::shared_ptr<IGPUResource> ResourceManager::add(std::shared_ptr<T> asset) {
        std::shared_ptr<IGPUResource> resource;

        if constexpr (std::is_same_v<T, Texture>) {
            resource = std::make_shared<GPUTexture>(device_, *asset);
        } else if constexpr (std::is_same_v<T, Mesh>) {
            resource = std::make_shared<GPUMesh>(device_, *asset);
        } else if constexpr (std::is_same_v<T, Cubemap>) {
            resource = std::make_shared<GPUCubemap>(device_, *asset);
        }

        // Captured as shared_ptr<void> — keeps CPU pixel/vertex data alive
        // until update() records and completes the upload.
        uploadQueue_.push_back({resource, asset});
        return resource;
    }
} // namespace Atlas
