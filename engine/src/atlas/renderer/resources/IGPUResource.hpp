#pragma once

#include <atomic>
#include <memory>
#include <vulkan/vulkan.h>

namespace Atlas {
    class Device;
    class GPUTexture;
    class GPUCubemap;
    class GPUMesh;

    class IGPUResource {
    public:
        enum class Status { PENDING_UPLOAD, READY, PENDING_DESTROY };

        enum class Type { TEXTURE, MESH, CUBEMAP };

        virtual ~IGPUResource() = default;

        virtual void recordUpload(VkCommandBuffer cmd) = 0;
        virtual void onUploadComplete() = 0;
        virtual void updateBindlessSlot() = 0;

        Type type() const { return type_; }
        Status status() const { return status_.load(std::memory_order::acquire); }
        bool isReady() const { return status() == Status::READY; }

        template<typename TGpu>
        static TGpu &default_();

        template<typename TGpu>
        static void createDefault(Device &device);

        static void destroyDefaults();

    protected:
        friend class ResourceManager;
        explicit IGPUResource(Type type) : type_(type) {
        }

        void setStatus(Status s) { status_.store(s, std::memory_order::release); }

    private:
        Type type_;
        std::atomic<Status> status_{Status::PENDING_UPLOAD};

        static std::unique_ptr<GPUTexture> defaultTexture_;
        static std::unique_ptr<GPUCubemap> defaultCubemap_;
        static std::unique_ptr<GPUMesh> defaultMesh_;
    };

    template<>
    GPUTexture &IGPUResource::default_<GPUTexture>();

    template<>
    GPUCubemap &IGPUResource::default_<GPUCubemap>();

    template<>
    GPUMesh &IGPUResource::default_<GPUMesh>();

    template<>
    void IGPUResource::createDefault<GPUTexture>(Device &device);

    template<>
    void IGPUResource::createDefault<GPUCubemap>(Device &device);

    template<>
    void IGPUResource::createDefault<GPUMesh>(Device &device);

    using GPUResource = IGPUResource;
} // namespace Atlas
