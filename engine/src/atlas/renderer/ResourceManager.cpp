#include "ResourceManager.hpp"

#include <algorithm>

#include "asset/Texture.hpp"
#include "resources/GPUTexture.hpp"
#include "renderer/abstraction/GPUBuffer.hpp"

namespace Atlas {
    ResourceManager::ResourceManager(Device &device) : device_(device) {
        createDefaults();
    }

    ResourceManager::~ResourceManager() {
        uploadQueue_.clear();
        destroyQueue_.clear();
        pendingAcquires_.clear();
        readyOnSubmit_.clear();
        GPUResource::destroyDefaults();
    }

    // -------------------------------------------------------------------------
    // createDefaults — synchronous, called once at construction
    // -------------------------------------------------------------------------

    void ResourceManager::createDefaults() {
        // Defaults are blocking startup uploads only; runtime uploads stay async.
        GPUResource::createDefault<GPUTexture>(device_);
        GPUResource::createDefault<GPUCubemap>(device_);
        GPUResource::createDefault<GPUMesh>(device_);
    }

    // -------------------------------------------------------------------------
    // remove() — AssetManager thread
    // -------------------------------------------------------------------------

    void ResourceManager::remove(std::shared_ptr<IGPUResource> resource) {
        if (!resource) return;

        resource->setStatus(IGPUResource::Status::PENDING_DESTROY);

        // Still in upload queue — erase before recordTransfer() ever runs.
        // VkImage/VkImageView/VkSampler exist but no transfer was submitted,
        // so no timeline value to wait on. Destruct immediately.
        auto it = std::find_if(uploadQueue_.begin(), uploadQueue_.end(),
                               [&](const UploadEntry &e) { return e.resource == resource; });

        if (it != uploadQueue_.end()) {
            uploadQueue_.erase(it);
            // capturedAsset drops here — CPU memory freed if no other holders
            // shared_ptr<GpuResource> drops — ~GPUTexture frees VkImage etc.
            return;
        }

        // READY or in-flight — defer GPU destruction until timeline confirms idle
        destroyQueue_.push_back({
            std::move(resource),
            device_.currentTransferTimelineValue()
        });
    }

    // -------------------------------------------------------------------------
    // update() — AssetManager thread, every few seconds
    // -------------------------------------------------------------------------

    void ResourceManager::update() {
        if (!uploadQueue_.empty()) {
            std::vector<UploadEntry> batch = std::move(uploadQueue_);

            VkCommandBuffer cmd = device_.beginGraphicsCommands();

            for (auto &entry: batch) {
                entry.resource->recordTransfer(cmd);
            }

            device_.endGraphicsCommands(cmd);

            for (auto &entry: batch) {
                entry.resource->onTransferComplete();
                entry.capturedAsset.reset();
                entry.resource->setStatus(IGPUResource::Status::READY);
                entry.resource->updateBindlessSlot();
            }
        }

        retirePendingDestroys();
    }

    // -------------------------------------------------------------------------
    // recordPendingAcquires() — render thread, start of beginFrame()
    // -------------------------------------------------------------------------

    void ResourceManager::recordPendingAcquires(VkCommandBuffer cmd) {
        device_.pollTransferCallbacks();

        assert(readyOnSubmit_.empty() &&
            "markAcquiredReady() was not called after last frame's submit");

        std::lock_guard lock(acquireMutex_);
        if (pendingAcquires_.empty()) return;

        for (auto &a: pendingAcquires_) {
            // Skip resources swept by GC before this frame processed them
            if (a.resource->status() == IGPUResource::Status::PENDING_DESTROY)
                continue;

            a.resource->recordOwnershipAcquire(cmd);
            pendingWaitValue_ = std::max(pendingWaitValue_, a.transferSignalValue);
            readyOnSubmit_.push_back(a);
        }

        hasPendingWait_ = !readyOnSubmit_.empty();
        pendingAcquires_.clear();
    }

    // -------------------------------------------------------------------------
    // consumePendingWait() — render thread, before vkQueueSubmit
    // -------------------------------------------------------------------------

    bool ResourceManager::consumePendingWait(uint64_t &outValue) {
        if (!hasPendingWait_) return false;
        outValue = pendingWaitValue_;
        pendingWaitValue_ = 0;
        hasPendingWait_ = false;
        return true;
    }

    // -------------------------------------------------------------------------
    // markAcquiredReady() — render thread, immediately after vkQueueSubmit
    // -------------------------------------------------------------------------

    void ResourceManager::markAcquiredReady() {
        for (auto &a: readyOnSubmit_) {
            // Status READY — getCurrentImageView() now returns real VkImageView
            a.resource->setStatus(IGPUResource::Status::READY);

            // Update bindless slot from default → real VkImageView
            // One vkUpdateDescriptorSets per texture, once, never again
            a.resource->updateBindlessSlot();
        }
        readyOnSubmit_.clear();
    }

    // -------------------------------------------------------------------------
    // retirePendingDestroys() — AssetManager thread, called from update()
    // -------------------------------------------------------------------------

    void ResourceManager::retirePendingDestroys() {
        // Erase when GPU has moved past the timeline value
        // shared_ptr destructs on erase — ~GPUTexture frees VkImage/VkImageView/VkSampler
        std::erase_if(destroyQueue_, [this](const PendingDestroy &pd) {
            return device_.isTransferComplete(pd.timelineValue);
        });
    }
} // namespace Atlas
