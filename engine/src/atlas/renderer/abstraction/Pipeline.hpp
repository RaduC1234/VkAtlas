#pragma once

#include "renderer/Device.hpp"

namespace Atlas {
    enum class PipelineType {
        Graphics,
        Compute,
        RayTracing
    };

    struct GraphicsPipelineConfigInfo {
        GraphicsPipelineConfigInfo() = default;
        GraphicsPipelineConfigInfo(const GraphicsPipelineConfigInfo &) = delete;
        GraphicsPipelineConfigInfo &operator=(const GraphicsPipelineConfigInfo &) = delete;

        std::vector<VkVertexInputBindingDescription> bindingDescriptions{};
        std::vector<VkVertexInputAttributeDescription> attributeDescriptions{};
        VkPipelineViewportStateCreateInfo viewportInfo;
        VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo;
        VkPipelineRasterizationStateCreateInfo rasterizationInfo;
        VkPipelineMultisampleStateCreateInfo multisampleInfo;
        VkPipelineColorBlendAttachmentState colorBlendAttachment;
        VkPipelineColorBlendStateCreateInfo colorBlendInfo;
        VkPipelineDepthStencilStateCreateInfo depthStencilInfo;
        std::vector<VkDynamicState> dynamicStateEnables;
        VkPipelineDynamicStateCreateInfo dynamicStateInfo;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        VkRenderPass renderPass = VK_NULL_HANDLE;
        uint32_t subpass = 0;
    };

    struct ComputePipelineConfigInfo {
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    };

    struct RayTracingPipelineConfigInfo {
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        uint32_t maxRecursionDepth = 2;
    };

    class Pipeline {
    public:
        Pipeline(Device &device,
                 const std::string &vertexVertPath,
                 const std::string &fragmentVertPath,
                 const GraphicsPipelineConfigInfo &configInfo);

        Pipeline(Device &device,
                 const std::string &computeShaderPath,
                 const ComputePipelineConfigInfo &configInfo);

        Pipeline(Device &device,
                 const std::string &rayGenerationShaderPath,
                 const std::string &missShaderPath,
                 const std::string &closestHitShaderPath,
                 const std::string &anyHitShaderPath,
                 const std::string &shadowMissShaderPath,
                 const RayTracingPipelineConfigInfo &configInfo);

        ~Pipeline();

        Pipeline(const Pipeline &) = delete;
        Pipeline &operator=(const Pipeline &) = delete;

        PipelineType type() const { return type_; }
        uint32_t shaderGroupCount() const { return shaderGroupCount_; }
        VkPipeline pipeline() const { return pipeline_; }

        void bind(VkCommandBuffer commandBuffer);

        static void defaultGraphicsPipelineConfigInfo(GraphicsPipelineConfigInfo &configInfo);
        static void defaultComputePipelineConfigInfo(ComputePipelineConfigInfo &configInfo);

    private:
        void createGraphicsPipeline(
            const std::string &vertFilepath,
            const std::string &fragFilepath,
            const GraphicsPipelineConfigInfo &configInfo);

        void createComputePipeline(
            const std::string &computeShaderPath,
            const ComputePipelineConfigInfo &configInfo);

        void createRayTracingPipeline(
            const std::string &rayGenerationShaderPath,
            const std::string &missShaderPath,
            const std::string &closestHitShaderPath,
            const std::string &anyHitShaderPath,
            const std::string &shadowMissShaderPath, const RayTracingPipelineConfigInfo &configInfo);

        void createShaderModule(const std::vector<char> &code, VkShaderModule *shaderModule) const;

        Device &device;
        const PipelineType type_;
        VkPipeline pipeline_;

        VkShaderModule vertShaderModule = VK_NULL_HANDLE;
        VkShaderModule fragShaderModule = VK_NULL_HANDLE;
        VkShaderModule compShaderModule = VK_NULL_HANDLE;
        VkShaderModule rayGenModule = VK_NULL_HANDLE;
        VkShaderModule rayMissModule = VK_NULL_HANDLE;
        VkShaderModule rayClosestHitModule = VK_NULL_HANDLE;
        VkShaderModule rayAnyHitModule = VK_NULL_HANDLE;
        VkShaderModule shadowMissModule = VK_NULL_HANDLE;

        uint32_t shaderGroupCount_ = 0;
    };
}
