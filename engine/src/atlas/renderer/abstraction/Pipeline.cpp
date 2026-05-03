#include "Pipeline.hpp"

#include "asset/AssetManager.hpp"

namespace Atlas {
    Pipeline::Pipeline(Device &device, const std::string &vertexVertPath, const std::string &fragmentVertPath, const GraphicsPipelineConfigInfo &configInfo) : device{device}, type_{PipelineType::Graphics} {
        createGraphicsPipeline(vertexVertPath, fragmentVertPath, configInfo);
    }

    Pipeline::Pipeline(Device &device, const std::string &computeShaderPath, const ComputePipelineConfigInfo &configInfo) : device{device}, type_{PipelineType::Compute} {
        createComputePipeline(computeShaderPath, configInfo);
    }

    Pipeline::Pipeline(Device &device, const std::string &rayGenerationShaderPath, const std::string &missShaderPath, const std::string &closestHitShaderPath, const std::string &anyHitShaderPath, const std::string &shadowMissShaderPath, const RayTracingPipelineConfigInfo &configInfo) : device(device), type_(PipelineType::RayTracing) {
        createRayTracingPipeline(rayGenerationShaderPath, missShaderPath, closestHitShaderPath, anyHitShaderPath, shadowMissShaderPath, configInfo);
    }

    Pipeline::~Pipeline() {
        switch (this->type_) {
            case PipelineType::Graphics: {
                if (fragShaderModule != VK_NULL_HANDLE) vkDestroyShaderModule(device.device(), fragShaderModule, nullptr);
                if (vertShaderModule != VK_NULL_HANDLE) vkDestroyShaderModule(device.device(), vertShaderModule, nullptr);
                break;
            }
            case PipelineType::Compute: {
                if (compShaderModule != VK_NULL_HANDLE) vkDestroyShaderModule(device.device(), compShaderModule, nullptr);
                break;
            }
            case PipelineType::RayTracing: {
                if (rayGenModule != VK_NULL_HANDLE) vkDestroyShaderModule(device.device(), rayGenModule, nullptr);
                if (rayMissModule != VK_NULL_HANDLE) vkDestroyShaderModule(device.device(), rayMissModule, nullptr);
                if (rayClosestHitModule != VK_NULL_HANDLE) vkDestroyShaderModule(device.device(), rayClosestHitModule, nullptr);
                if (rayAnyHitModule != VK_NULL_HANDLE) vkDestroyShaderModule(device.device(), rayAnyHitModule, nullptr);
                if (shadowMissModule != VK_NULL_HANDLE) vkDestroyShaderModule(device.device(), shadowMissModule, nullptr);
                break;
            }
            default: {
                assert("Invalid pipeline type in destructor");
            }
        }

        vkDestroyPipeline(device.device(), pipeline_, nullptr);
    }

    void Pipeline::bind(VkCommandBuffer commandBuffer) {
        switch (this->type_) {
            case PipelineType::Graphics: {
                vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
                break;
            }
            case PipelineType::Compute: {
                vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
                break;
            }
            case PipelineType::RayTracing: {
                vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline_);
                break;
            }
            default: {
                throw std::runtime_error("Invalid pipeline type in bind()");
            }
        }
    }

    void Pipeline::createGraphicsPipeline(
        const std::string &vertFilepath,
        const std::string &fragFilepath,
        const GraphicsPipelineConfigInfo &configInfo) {
        assert(
            configInfo.pipelineLayout != VK_NULL_HANDLE &&
            "Cannot create graphics pipeline: no pipelineLayout provided in configInfo"
        );
        assert(
            configInfo.renderPass != VK_NULL_HANDLE &&
            "Cannot create graphics pipeline: no renderPass provided in configInfo"
        );

        const auto fragCode = AssetManager::loadFileAsU8(fragFilepath);
        const auto vertCode = AssetManager::loadFileAsU8(vertFilepath);

        createShaderModule(vertCode, &vertShaderModule);
        createShaderModule(fragCode, &fragShaderModule);

        VkPipelineShaderStageCreateInfo shaderStages[2];
        shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        shaderStages[0].module = vertShaderModule;
        shaderStages[0].pName = "main";
        shaderStages[0].flags = 0;
        shaderStages[0].pNext = nullptr;
        shaderStages[0].pSpecializationInfo = nullptr;

        shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        shaderStages[1].module = fragShaderModule;
        shaderStages[1].pName = "main";
        shaderStages[1].flags = 0;
        shaderStages[1].pNext = nullptr;
        shaderStages[1].pSpecializationInfo = nullptr;

        auto bindingDescriptions = configInfo.bindingDescriptions;
        auto attributeDescriptions = configInfo.attributeDescriptions;
        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
        vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(bindingDescriptions.size());
        vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();
        vertexInputInfo.pVertexBindingDescriptions = bindingDescriptions.data();

        VkGraphicsPipelineCreateInfo pipelineInfo = {};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = shaderStages;
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &configInfo.inputAssemblyInfo;
        pipelineInfo.pViewportState = &configInfo.viewportInfo;
        pipelineInfo.pRasterizationState = &configInfo.rasterizationInfo;
        pipelineInfo.pMultisampleState = &configInfo.multisampleInfo;
        pipelineInfo.pColorBlendState = &configInfo.colorBlendInfo;
        pipelineInfo.pDepthStencilState = &configInfo.depthStencilInfo;
        pipelineInfo.pDynamicState = &configInfo.dynamicStateInfo;

        pipelineInfo.layout = configInfo.pipelineLayout;
        pipelineInfo.renderPass = configInfo.renderPass;
        pipelineInfo.subpass = configInfo.subpass;

        pipelineInfo.basePipelineIndex = -1;
        pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

        if (vkCreateGraphicsPipelines(device.device(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline_) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create graphics pipeline.");
        }
    }

    void Pipeline::createComputePipeline(
        const std::string &computeShaderPath,
        const ComputePipelineConfigInfo &configInfo) {
        assert(
            configInfo.pipelineLayout != VK_NULL_HANDLE &&
            "Cannot create compute pipeline: no pipelineLayout provided in configInfo"
        );

        const auto computeCode = AssetManager::loadFileAsU8(computeShaderPath);

        createShaderModule(computeCode, &compShaderModule);

        VkPipelineShaderStageCreateInfo computeShaderStageInfo{};
        computeShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        computeShaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        computeShaderStageInfo.module = compShaderModule;
        computeShaderStageInfo.pName = "main";

        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage = computeShaderStageInfo;
        pipelineInfo.layout = configInfo.pipelineLayout;


        if (vkCreateComputePipelines(device.device(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline_) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create compute pipeline.");
        }
    }

    void Pipeline::createRayTracingPipeline(
        const std::string &rayGenerationShaderPath,
        const std::string &missShaderPath,
        const std::string &closestHitShaderPath,
        const std::string &anyHitShaderPath,
        const std::string &shadowMissShaderPath,
        const RayTracingPipelineConfigInfo &configInfo) {
        assert(
            configInfo.pipelineLayout != VK_NULL_HANDLE &&
            "Cannot create ray tracing pipeline: no pipelineLayout provided in configInfo"
        );
        assert(
            !rayGenerationShaderPath.empty() &&
            "Cannot create ray tracing pipeline: no ray generation shader provided"
        );

        std::vector<VkPipelineShaderStageCreateInfo> shaderStages;

        // Index 0 — raygen
        const auto rayGenCode = AssetManager::loadFileAsU8(rayGenerationShaderPath);
        createShaderModule(rayGenCode, &rayGenModule);
        VkPipelineShaderStageCreateInfo rayGenStage{};
        rayGenStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        rayGenStage.stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
        rayGenStage.module = rayGenModule;
        rayGenStage.pName = "main";
        shaderStages.push_back(rayGenStage);

        // Index 1 — primary miss
        if (!missShaderPath.empty()) {
            const auto missCode = AssetManager::loadFileAsU8(missShaderPath);
            createShaderModule(missCode, &rayMissModule);
            VkPipelineShaderStageCreateInfo missStage{};
            missStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            missStage.stage = VK_SHADER_STAGE_MISS_BIT_KHR;
            missStage.module = rayMissModule;
            missStage.pName = "main";
            shaderStages.push_back(missStage);
        }

        // Index 2 — shadow miss
        if (!shadowMissShaderPath.empty()) {
            const auto shadowMissCode = AssetManager::loadFileAsU8(shadowMissShaderPath);
            createShaderModule(shadowMissCode, &shadowMissModule);
            VkPipelineShaderStageCreateInfo shadowMissStage{};
            shadowMissStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            shadowMissStage.stage = VK_SHADER_STAGE_MISS_BIT_KHR;
            shadowMissStage.module = shadowMissModule;
            shadowMissStage.pName = "main";
            shaderStages.push_back(shadowMissStage);
        }

        // Index 3 — closest hit
        if (!closestHitShaderPath.empty()) {
            const auto chitCode = AssetManager::loadFileAsU8(closestHitShaderPath);
            createShaderModule(chitCode, &rayClosestHitModule);
            VkPipelineShaderStageCreateInfo chitStage{};
            chitStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            chitStage.stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
            chitStage.module = rayClosestHitModule;
            chitStage.pName = "main";
            shaderStages.push_back(chitStage);
        }

        // Index 4 — any hit (optional)
        if (!anyHitShaderPath.empty()) {
            const auto ahitCode = AssetManager::loadFileAsU8(anyHitShaderPath);
            createShaderModule(ahitCode, &rayAnyHitModule);
            VkPipelineShaderStageCreateInfo ahitStage{};
            ahitStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            ahitStage.stage = VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
            ahitStage.module = rayAnyHitModule;
            ahitStage.pName = "main";
            shaderStages.push_back(ahitStage);
        }

        // ---- Shader groups ----
        // SBT layout:
        //   group 0 — raygen       (general,   shader index 0)
        //   group 1 — primary miss (general,   shader index 1)
        //   group 2 — shadow miss  (general,   shader index 2)
        //   group 3 — hit group    (triangles, closest hit index 3, any hit index 4 if present)

        std::vector<VkRayTracingShaderGroupCreateInfoKHR> shaderGroups;

        // Group 0 — raygen
        VkRayTracingShaderGroupCreateInfoKHR rayGenGroup{};
        rayGenGroup.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        rayGenGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        rayGenGroup.generalShader = 0;
        rayGenGroup.closestHitShader = VK_SHADER_UNUSED_KHR;
        rayGenGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
        rayGenGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
        shaderGroups.push_back(rayGenGroup);

        // Group 1 — primary miss
        if (!missShaderPath.empty()) {
            VkRayTracingShaderGroupCreateInfoKHR missGroup{};
            missGroup.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
            missGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
            missGroup.generalShader = 1;
            missGroup.closestHitShader = VK_SHADER_UNUSED_KHR;
            missGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
            missGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
            shaderGroups.push_back(missGroup);
        }

        // Group 2 — shadow miss
        if (!shadowMissShaderPath.empty()) {
            VkRayTracingShaderGroupCreateInfoKHR shadowMissGroup{};
            shadowMissGroup.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
            shadowMissGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
            shadowMissGroup.generalShader = 2;
            shadowMissGroup.closestHitShader = VK_SHADER_UNUSED_KHR;
            shadowMissGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
            shadowMissGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
            shaderGroups.push_back(shadowMissGroup);
        }

        // Group 3 — hit group
        if (!closestHitShaderPath.empty()) {
            VkRayTracingShaderGroupCreateInfoKHR hitGroup{};
            hitGroup.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
            hitGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
            hitGroup.generalShader = VK_SHADER_UNUSED_KHR;
            hitGroup.closestHitShader = 3;
            hitGroup.anyHitShader = !anyHitShaderPath.empty() ? 4 : VK_SHADER_UNUSED_KHR;
            hitGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
            shaderGroups.push_back(hitGroup);
        }

        VkRayTracingPipelineCreateInfoKHR pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
        pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
        pipelineInfo.pStages = shaderStages.data();
        pipelineInfo.groupCount = static_cast<uint32_t>(shaderGroups.size());
        pipelineInfo.pGroups = shaderGroups.data();
        pipelineInfo.maxPipelineRayRecursionDepth = configInfo.maxRecursionDepth;
        pipelineInfo.layout = configInfo.pipelineLayout;
        pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
        pipelineInfo.basePipelineIndex = -1;

        if (vkCreateRayTracingPipelinesKHR(device.device(),VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline_) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create ray tracing pipeline.");
        }

        shaderGroupCount_ = static_cast<uint32_t>(shaderGroups.size());
    }

    void Pipeline::createShaderModule(const std::vector<char> &code, VkShaderModule *shaderModule) const {
        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = code.size();
        createInfo.pCode = reinterpret_cast<const uint32_t *>(code.data());

        if (vkCreateShaderModule(this->device.device(), &createInfo, nullptr, shaderModule) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create shader module");
        }
    }

    void Pipeline::defaultGraphicsPipelineConfigInfo(GraphicsPipelineConfigInfo &configInfo) {
        configInfo.inputAssemblyInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        configInfo.inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        configInfo.inputAssemblyInfo.primitiveRestartEnable = VK_FALSE;

        configInfo.viewportInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        configInfo.viewportInfo.viewportCount = 1;
        configInfo.viewportInfo.pViewports = nullptr;
        configInfo.viewportInfo.scissorCount = 1;
        configInfo.viewportInfo.pScissors = nullptr;

        configInfo.rasterizationInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        configInfo.rasterizationInfo.depthClampEnable = VK_FALSE;
        configInfo.rasterizationInfo.rasterizerDiscardEnable = VK_FALSE;
        configInfo.rasterizationInfo.polygonMode = VK_POLYGON_MODE_FILL;
        configInfo.rasterizationInfo.lineWidth = 1.0f;
        configInfo.rasterizationInfo.cullMode = VK_CULL_MODE_NONE;
        configInfo.rasterizationInfo.frontFace = VK_FRONT_FACE_CLOCKWISE;
        configInfo.rasterizationInfo.depthBiasEnable = VK_FALSE;
        configInfo.rasterizationInfo.depthBiasConstantFactor = 0.0; // optional
        configInfo.rasterizationInfo.depthBiasClamp = 0.0f; // optional
        configInfo.rasterizationInfo.depthBiasSlopeFactor = 0.0f; // optional

        configInfo.multisampleInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        configInfo.multisampleInfo.sampleShadingEnable = VK_FALSE;
        configInfo.multisampleInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        configInfo.multisampleInfo.minSampleShading = 1.0f; // Optional
        configInfo.multisampleInfo.pSampleMask = nullptr; // Optional
        configInfo.multisampleInfo.alphaToCoverageEnable = VK_FALSE; // Optional
        configInfo.multisampleInfo.alphaToOneEnable = VK_FALSE; // Optional

        configInfo.colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        configInfo.colorBlendAttachment.blendEnable = VK_FALSE;
        configInfo.colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE; // Optional
        configInfo.colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
        configInfo.colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD; // Optional
        configInfo.colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; // Optional
        configInfo.colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
        configInfo.colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD; // Optional

        configInfo.colorBlendInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        configInfo.colorBlendInfo.logicOpEnable = VK_FALSE;
        configInfo.colorBlendInfo.logicOp = VK_LOGIC_OP_COPY; // Optional
        configInfo.colorBlendInfo.attachmentCount = 1;
        configInfo.colorBlendInfo.pAttachments = &configInfo.colorBlendAttachment;
        configInfo.colorBlendInfo.blendConstants[0] = 0.0f; // Optional
        configInfo.colorBlendInfo.blendConstants[1] = 0.0f; // Optional
        configInfo.colorBlendInfo.blendConstants[2] = 0.0f; // Optional
        configInfo.colorBlendInfo.blendConstants[3] = 0.0f; // Optional

        configInfo.depthStencilInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        configInfo.depthStencilInfo.depthTestEnable = VK_TRUE;
        configInfo.depthStencilInfo.depthWriteEnable = VK_TRUE;
        configInfo.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_LESS;
        configInfo.depthStencilInfo.depthBoundsTestEnable = VK_FALSE;
        configInfo.depthStencilInfo.minDepthBounds = 0.0f; // Optional
        configInfo.depthStencilInfo.maxDepthBounds = 1.0f; // Optional
        configInfo.depthStencilInfo.stencilTestEnable = VK_FALSE;
        configInfo.depthStencilInfo.front = {}; // Optional
        configInfo.depthStencilInfo.back = {}; // Optional

        configInfo.dynamicStateEnables = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        configInfo.dynamicStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        configInfo.dynamicStateInfo.pDynamicStates = configInfo.dynamicStateEnables.data();
        configInfo.dynamicStateInfo.dynamicStateCount = static_cast<uint32_t>(configInfo.dynamicStateEnables.size());
        configInfo.dynamicStateInfo.flags = 0;

        configInfo.dynamicStateEnables = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        configInfo.dynamicStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        configInfo.dynamicStateInfo.pDynamicStates = configInfo.dynamicStateEnables.data();
        configInfo.dynamicStateInfo.dynamicStateCount = static_cast<uint32_t>(configInfo.dynamicStateEnables.size());
        configInfo.dynamicStateInfo.flags = 0;
    }

    void Pipeline::defaultComputePipelineConfigInfo(ComputePipelineConfigInfo &configInfo) {
    }
}
