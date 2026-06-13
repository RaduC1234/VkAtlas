#pragma once

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "RenderStage.hpp"
#include "asset/AssetManager.hpp"
#include "entity/Object.hpp"
#include "renderer/Device.hpp"
#include "renderer/abstraction/Descriptors.hpp"
#include "renderer/abstraction/Pipeline.hpp"

namespace Atlas {
    // Renders a depth-only shadow map for the scene's first directional light,
    // fitted to the camera frustum (truncated at SHADOW_DISTANCE) with texel
    // snapping for stability. The geometry pass samples it through shadow_data.
    class ShadowStage : public RenderStage {
    public:
        static constexpr uint32_t SHADOW_MAP_SIZE = 2048;
        static constexpr float SHADOW_DISTANCE = 50.0f; // world units of camera frustum that receive shadows

        explicit ShadowStage(Device &device);
        ~ShadowStage() override;

        ShadowStage(const ShadowStage &) = delete;
        ShadowStage &operator=(const ShadowStage &) = delete;

        void getDeclaredOutputs(std::vector<Resource::Description> &out) const override;
        void getDeclaredInputs(std::vector<std::string> &out) const override;

        void onResourcesCreated(const Context &ctx) override;
        void onUpdate(entt::registry &registry) override;

        void record(VkCommandBuffer cmd, VkDescriptorSet globalSet) override;

    private:
        using RasterDraw = std::pair<AssetHandle<Mesh>, uint32_t>;
        using RasterTextureBinding = std::pair<AssetHandle<Texture>, uint32_t>;
        using RasterDrawData = std::tuple<std::vector<RasterDraw>, std::vector<RasterTextureBinding>, uint32_t>;

        // Mirrored by the ShadowDataBuffer block in Geometry.frag.
        struct alignas(16) ShadowData {
            glm::mat4 lightViewProj{1.0f};
            uint32_t lightIndex{0}; // first directional light; CullingStage sorts directionals first
            uint32_t enabled{0};
            float texelSize{1.0f / SHADOW_MAP_SIZE};
            float _pad{};
        };

        void createRenderPass();
        void createFramebuffer();
        void createPipeline();

        Device &device;

        const GPUImage *shadowMap = nullptr;
        GPUBuffer *shadowDataBuffer = nullptr;
        const RasterDrawData *drawData = nullptr;

        VkRenderPass renderPass = VK_NULL_HANDLE;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;

        std::unique_ptr<DescriptorPool> pool;
        std::unique_ptr<DescriptorSetLayout> objectDataSetLayout;
        VkDescriptorSet objectDataSet = VK_NULL_HANDLE;

        std::unique_ptr<Pipeline> pipeline;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;

        ShadowData shadowData{};
    };
} // namespace Atlas
