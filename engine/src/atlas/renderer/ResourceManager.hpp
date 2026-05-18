#pragma once

#include <concepts>
#include <memory>
#include <mutex>
#include <type_traits>
#include <vector>

#include "asset/Cubemap.hpp"
#include "asset/Mesh.hpp"
#include "asset/Texture.hpp"
#include "resources/IGPUResource.hpp"
#include "renderer/Device.hpp"
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

        // Marks PENDING_DESTROY, defers GPU destruction until timeline confirms idle.
        // If still in uploadQueue — removed immediately, no timeline wait needed.
        void remove(std::shared_ptr<IGPUResource> resource);

        // Batches all pending uploads into one vkQueueSubmit.
        // Retires pending destroys whose timeline value has passed.
        // Called every few seconds — not every frame.
        void update();

        // -------------------------------------------------------------------------
        // Renderer interface — called on render thread
        // -------------------------------------------------------------------------

        // Records ownership acquire barriers for all resources uploaded since last frame.
        // Call at START of beginFrame() into the frame command buffer.
        void recordPendingAcquires(VkCommandBuffer cmd);

        // Returns true + value if frame submit must wait on transfer timeline.
        // Call before vkQueueSubmit. Resets state — call exactly once per frame.
        bool consumePendingWait(uint64_t &outValue);

        // Sets acquired resources to READY and updates their bindless slots.
        // Call immediately after vkQueueSubmit.
        void markAcquiredReady();

    private:
        struct UploadEntry {
            std::shared_ptr<IGPUResource> resource;
            std::shared_ptr<void> capturedAsset; // Ref<T> as void — keeps CPU alive
        };

        struct PendingAcquire {
            std::shared_ptr<IGPUResource> resource;
            uint64_t transferSignalValue;
        };

        struct PendingDestroy {
            std::shared_ptr<IGPUResource> resource;
            uint64_t timelineValue;
        };

        void retirePendingDestroys();

        Device &device_;

        // Upload queue — AssetManager thread only, no mutex needed
        std::vector<UploadEntry> uploadQueue_;

        // Destroy queue — AssetManager thread only, no mutex needed
        std::vector<PendingDestroy> destroyQueue_;

        // Acquire queue — written by transfer completion callback, read by render thread
        std::mutex acquireMutex_;
        std::vector<PendingAcquire> pendingAcquires_;

        // Set between recordPendingAcquires() and markAcquiredReady()
        std::vector<PendingAcquire> readyOnSubmit_;
        uint64_t pendingWaitValue_ = 0;
        bool hasPendingWait_ = false;

        // Default texture — owns the VkImage/VkImageView/VkSampler for the 1x1 white
        std::unique_ptr<GPUTexture> defaultTexture_;
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

        // Ref<T> captured as shared_ptr<void> — keeps CPU pixel/vertex data alive
        // until the transfer callback fires and resets capturedAsset
        uploadQueue_.push_back({resource, asset});
        return resource;
    }
} // namespace Atlas
