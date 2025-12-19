#pragma once
#include "asset/AssetManager.hpp"
#include "renderer/Buffer.hpp"
#include "renderer/Camera.hpp"
#include "renderer/Descriptors.hpp"
#include "renderer/Device.hpp"
#include "renderer/Pipeline.hpp"

namespace Atlas {
    class SkyboxSystem {
    public:
        SkyboxSystem(Device& device, VkRenderPass renderPass);
        ~SkyboxSystem();

        void render(VkCommandBuffer commandBuffer, Camera& camera);
    private:
        Device& device;
        std::unique_ptr<Pipeline> pipeline;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        std::unique_ptr<Buffer> uniformBuffer;

        std::unique_ptr<DescriptorSetLayout> descriptorSetLayout;
        std::unique_ptr<DescriptorPool> descriptorPool;
        VkDescriptorSet descriptorSet{VK_NULL_HANDLE};

        AssetHandle cubemapHandle{INVALID_ASSET_HANDLE};
        AssetHandle boundCubemap{INVALID_ASSET_HANDLE};

        void createPipeline(VkRenderPass renderPass);
        void createCubeVertexBuffer();
        void updateDescriptor();
    };
}
