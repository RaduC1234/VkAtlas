#pragma once
#include "IRenderStage.hpp"
#include "asset/AssetManager.hpp"
#include "renderer/abstraction/Descriptors.hpp"
#include "renderer/abstraction/Pipeline.hpp"

namespace Atlas {
    class PathTracingStage : public IRenderStage {
    public:
        PathTracingStage(Device &device, const DescriptorSetLayout &globalSetLayout);
        ~PathTracingStage();

        void getDeclaredOutputs(std::vector<Resource::Description> &out) const override;
        void getDeclaredInputs(std::vector<std::string> &out) const override;
        void onResourcesCreated(const Context &ctx) override;
        void onSceneChanged(entt::registry &registry) override;
        void record(VkCommandBuffer cmd, VkDescriptorSet globalSet) override;

        void reset();

    private:
        void createDescriptors();
        void createPipelineLayout();
        void createPipeline();
        void buildSBT();
        void updateDescriptorSet();
        void updateDescriptorSets();

        uint32_t registerTexture(AssetHandle handle);

        uint32_t alignUp(uint32_t size, uint32_t alignment) const;

        static constexpr uint32_t MAX_TEXTURES = 512;
        static constexpr uint32_t MAX_OBJECTS = 1024;
        static constexpr uint32_t MAX_LIGHTS = 16;
        static constexpr uint32_t MAX_BOUNCES = 4;

        Device &device;
        const DescriptorSetLayout &globalSetLayout;

        GPUImage *outputImage = nullptr;
        AccelerationStructure tlas_;

        // Scene data buffers
        std::unique_ptr<GPUBuffer> objectBuffer;
        std::unique_ptr<GPUBuffer> lightBuffer;

        // Descriptors
        std::unique_ptr<DescriptorSetLayout> ptSetLayout;
        std::unique_ptr<DescriptorPool> ptPool;
        VkDescriptorSet ptSet = VK_NULL_HANDLE;

        std::unique_ptr<DescriptorSetLayout> textureSetLayout;
        std::unique_ptr<DescriptorPool> texturePool;
        VkDescriptorSet bindlessTextureSet = VK_NULL_HANDLE;

        // Pipeline
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        std::unique_ptr<Pipeline> pipeline;

        // SBT
        std::unique_ptr<GPUBuffer> sbtBuffer;
        VkStridedDeviceAddressRegionKHR sbtRaygen{};
        VkStridedDeviceAddressRegionKHR sbtMiss{};
        VkStridedDeviceAddressRegionKHR sbtHit{};
        VkStridedDeviceAddressRegionKHR sbtCallable{};

        // State
        uint32_t currentSample = 0;
        uint32_t frameIndex = 0;
        uint32_t objectCount = 0;
        uint32_t lightCount = 0;
        bool active = true;

        // Texture registry
        std::unordered_map<AssetHandle, uint32_t> handleToSlot;
        uint32_t nextTextureSlot = 1;
        AssetHandle defaultWhiteHandle = INVALID_ASSET_HANDLE;
    };
}
