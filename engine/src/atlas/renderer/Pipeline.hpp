#pragma once

#include "Device.hpp"

namespace Atlas {

    enum class PipelineType {
        Graphics,
        Compute
    };

    struct GraphicsPipelineConfigInfo {
        GraphicsPipelineConfigInfo() = default;
        GraphicsPipelineConfigInfo(const GraphicsPipelineConfigInfo&) = delete;
        GraphicsPipelineConfigInfo& operator=(const GraphicsPipelineConfigInfo&) = delete;

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

    class Pipeline {
    public:
        Pipeline(Device& device,
            const std::string &vertexVertPath,
            const std::string &fragmentVertPath,
            const GraphicsPipelineConfigInfo& configInfo);

        Pipeline(Device& device,
                 const std::string& computeShaderPath,
                 const ComputePipelineConfigInfo& configInfo);

        ~Pipeline();

        Pipeline(const Pipeline&) = delete;
        Pipeline& operator=(const Pipeline&) = delete;

        PipelineType getType() const { return type; }

        void bind(VkCommandBuffer commandBuffer);

        static void defaultGraphicsPipelineConfigInfo(GraphicsPipelineConfigInfo &configInfo);
        static void defaultComputePipelineConfigInfo(ComputePipelineConfigInfo &configInfo);

    private:
        void createGraphicsPipeline(
            const std::string &vertFilepath,
            const std::string &fragFilepath,
            const GraphicsPipelineConfigInfo &configInfo);

        void createComputePipeline(
            const std::string & computeShaderPath,
            const ComputePipelineConfigInfo& configInfo);

        void createShaderModule(const std::vector<char> &code, VkShaderModule* shaderModule) const;

        Device& device;
        const PipelineType type;
        VkPipeline pipeline;

        VkShaderModule vertShaderModule;
        VkShaderModule fragShaderModule;
        VkShaderModule compShaderModule;
    };
}
