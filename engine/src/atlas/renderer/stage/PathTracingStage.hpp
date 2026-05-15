#pragma once
#include <entt/signal/sigh.hpp>

#include "IRenderStage.hpp"
#include "asset/AssetManager.hpp"
#include "renderer/Camera.hpp"
#include "renderer/abstraction/Descriptors.hpp"
#include "renderer/abstraction/Pipeline.hpp"

namespace Atlas {
    class PathTracingStage : public IRenderStage {
    public:
        PathTracingStage(Device &device, AssetManager &assets, const DescriptorSetLayout &globalSetLayout);
        ~PathTracingStage();

        void getDeclaredOutputs(std::vector<Resource::Description> &out) const override;
        void getDeclaredInputs(std::vector<std::string> &out) const override;
        void onResourcesCreated(const Context &ctx) override;
        void onUpdate(entt::registry &registry) override;
        void record(VkCommandBuffer cmd, VkDescriptorSet globalSet) override;

        void reset();

    private:
        void createDescriptors();
        void createPipelineLayout();
        void createPipeline();
        void buildSBT();
        void updateDescriptorSet();
        void updateDescriptorSets();
        void onCameraUpdated(entt::registry &registry, entt::entity entity);
        void onCameraDestroyed(entt::registry &registry, entt::entity entity);

        uint32_t registerTexture(AssetHandle handle);
        static bool cameraDataChanged(const Camera::Data &lhs, const Camera::Data &rhs);

        uint32_t alignUp(uint32_t size, uint32_t alignment) const;

        static constexpr uint32_t MAX_TEXTURES = 512;
        static constexpr uint32_t MAX_OBJECTS = 1024;
        static constexpr uint32_t MAX_LIGHTS = 32;
        static constexpr uint32_t MAX_BOUNCES = 6;

        Device &device;
        AssetManager &assets;
        const DescriptorSetLayout &globalSetLayout;

        GPUImage *outputImage = nullptr;
        AccelerationStructure tlas_;

        std::unique_ptr<GPUBuffer> objectBuffer;
        std::unique_ptr<GPUBuffer> lightBuffer;
        std::unique_ptr<GPUBuffer> vertexBuffer;
        std::unique_ptr<GPUBuffer> indexBuffer;
        std::unique_ptr<GPUImage> accumulationImage;

        std::unique_ptr<DescriptorSetLayout> ptSetLayout;
        std::unique_ptr<DescriptorPool> ptPool;
        VkDescriptorSet ptSet = VK_NULL_HANDLE;
        VkDescriptorSet bindlessTextureSet = VK_NULL_HANDLE; // alias for ptSet

        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        std::unique_ptr<Pipeline> pipeline;

        std::unique_ptr<GPUBuffer> sbtBuffer;
        VkStridedDeviceAddressRegionKHR sbtRaygen{};
        VkStridedDeviceAddressRegionKHR sbtMiss{};
        VkStridedDeviceAddressRegionKHR sbtHit{};
        VkStridedDeviceAddressRegionKHR sbtCallable{};

        uint32_t currentSample = 0;
        uint32_t frameIndex = 0;
        uint32_t objectCount = 0;
        uint32_t lightCount = 0;
        bool active = true;
        bool hasCameraData = false;
        Camera::Data lastCameraData{};
        entt::scoped_connection cameraConstructConnection;
        entt::scoped_connection cameraUpdateConnection;
        entt::scoped_connection cameraDestroyConnection;

        // Texture registry
        std::unordered_map<AssetHandle, uint32_t> handleToSlot;
        uint32_t nextTextureSlot = 1;
        AssetHandle defaultWhiteHandle = INVALID_ASSET_HANDLE;
    };
}
