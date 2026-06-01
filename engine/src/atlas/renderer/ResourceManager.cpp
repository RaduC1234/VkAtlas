#include "ResourceManager.hpp"

#include <algorithm>
#include <cstddef>
#include <stdexcept>

#include "asset/Texture.hpp"
#include "core/Profiler.hpp"
#include "resources/GPUTexture.hpp"
#include "renderer/abstraction/GPUBuffer.hpp"

namespace Atlas {
    ResourceManager::ResourceManager(Device &device) : device_(device) {
        createDefaults();
    }

    ResourceManager::~ResourceManager() {
        if (inFlightUpload_) {
            vkWaitForFences(device_.device(), 1, &inFlightUpload_->fence, VK_TRUE, UINT64_MAX);
            device_.freeGraphicsCommandBuffer(inFlightUpload_->commandBuffer);
            vkDestroyFence(device_.device(), inFlightUpload_->fence, nullptr);
            inFlightUpload_.reset();
        }

        uploadQueue_.clear();
        destroyQueue_.clear();
        GPUResource::destroyDefaults();
    }

    // -------------------------------------------------------------------------
    // createDefaults — synchronous, called once at construction
    // -------------------------------------------------------------------------

    void ResourceManager::createDefaults() {
        ATLAS_PROFILE_FUNCTION();

        // Defaults are blocking startup uploads.
        GPUResource::createDefault<GPUTexture>(device_);
        GPUResource::createDefault<GPUCubemap>(device_);
        GPUResource::createDefault<GPUMesh>(device_);
    }

    // -------------------------------------------------------------------------
    // remove() — AssetManager thread
    // -------------------------------------------------------------------------

    void ResourceManager::remove(std::shared_ptr<IGPUResource> resource) {
        ATLAS_PROFILE_FUNCTION();

        if (!resource) return;

        resource->setStatus(IGPUResource::Status::PENDING_DESTROY);

        // Still in upload queue — erase before recordUpload() ever runs.
        // VkImage/VkImageView/VkSampler exist, but no commands were submitted.
        auto it = std::ranges::find_if(uploadQueue_,
                                       [&](const UploadEntry &e) { return e.resource == resource; });

        if (it != uploadQueue_.end()) {
            uploadQueue_.erase(it);
            // capturedAsset drops here — CPU memory freed if no other holders
            // shared_ptr<GpuResource> drops — ~GPUTexture frees VkImage etc.
            return;
        }

        // READY or in-flight — defer destruction until update() retires it.
        destroyQueue_.push_back({
            std::move(resource),
            device_.currentTransferTimelineValue()
        });
    }

    // -------------------------------------------------------------------------
    // update() — AssetManager thread
    // -------------------------------------------------------------------------

    void ResourceManager::update() {
        ATLAS_PROFILE_FUNCTION();

        pollUpload();

        if (!inFlightUpload_ && !uploadQueue_.empty()) {
            ATLAS_PROFILE_SCOPE("ResourceManager::submitUploadBatch");
            constexpr size_t maxUploadsPerBatch = 1;

            std::vector<UploadEntry> batch;
            const size_t count = std::min(maxUploadsPerBatch, uploadQueue_.size());
            batch.reserve(count);

            for (size_t i = 0; i < count; ++i) {
                batch.push_back(std::move(uploadQueue_[i]));
            }

            uploadQueue_.erase(uploadQueue_.begin(), uploadQueue_.begin() + static_cast<std::ptrdiff_t>(count));

            VkCommandBuffer cmd = device_.beginGraphicsCommands();
            for (auto &entry: batch) {
                entry.resource->recordUpload(cmd);
            }

            VkFenceCreateInfo fenceInfo{};
            fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

            VkFence fence = VK_NULL_HANDLE;
            if (vkCreateFence(device_.device(), &fenceInfo, nullptr, &fence) != VK_SUCCESS) {
                throw std::runtime_error("ResourceManager: failed to create upload fence");
            }

            device_.submitGraphicsCommands(cmd, fence);
            inFlightUpload_ = InFlightUpload{cmd, fence, std::move(batch)};
        }

        retirePendingDestroys();
    }

    void ResourceManager::pollUpload() {
        ATLAS_PROFILE_FUNCTION();

        if (!inFlightUpload_) {
            return;
        }

        const VkResult result = vkGetFenceStatus(device_.device(), inFlightUpload_->fence);
        if (result == VK_NOT_READY) {
            return;
        }

        if (result != VK_SUCCESS) {
            throw std::runtime_error("ResourceManager: failed to poll upload fence");
        }

        for (auto &entry: inFlightUpload_->batch) {
            entry.resource->onUploadComplete();
            entry.capturedAsset.reset();
            entry.resource->setStatus(IGPUResource::Status::READY);
            entry.resource->updateBindlessSlot();
        }

        device_.freeGraphicsCommandBuffer(inFlightUpload_->commandBuffer);
        vkDestroyFence(device_.device(), inFlightUpload_->fence, nullptr);
        inFlightUpload_.reset();
    }

    void ResourceManager::retirePendingDestroys() {
        ATLAS_PROFILE_FUNCTION();

        std::erase_if(destroyQueue_, [this](const PendingDestroy &pd) {
            return device_.isTransferComplete(pd.timelineValue);
        });
    }
} // namespace Atlas
