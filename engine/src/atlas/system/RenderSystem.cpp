#include "RenderSystem.hpp"

// std
#include <cassert>
#include <stdexcept>

#include "core/AssetManager.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

#include "entity/Object.hpp"

namespace Atlas {
    struct SimplePushConstantData {
        glm::mat4 modelMatrix{1.0f};
        glm::mat4 normalMatrix{1.0f};
    };

    RenderSystem::RenderSystem(Device &device, VkRenderPass renderPass, VkDescriptorSetLayout globalSetLayout): device(device) {
        createPipelineLayout(globalSetLayout);
        createPipeline(renderPass);
    }

    RenderSystem::~RenderSystem() { vkDestroyPipelineLayout(device.device(), pipelineLayout, nullptr); }

    void RenderSystem::createPipelineLayout(VkDescriptorSetLayout globalSetLayout) {
        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(SimplePushConstantData);

        std::vector<VkDescriptorSetLayout> descriptorSetLayouts{globalSetLayout};


        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
        pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

        if (vkCreatePipelineLayout(device.device(), &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
            throw std::runtime_error("failed to create pipeline layout!");
        }
    }

    void RenderSystem::createPipeline(VkRenderPass renderPass) {
        assert(pipelineLayout != VK_NULL_HANDLE && "Cannot create pipeline before pipeline layout");

        PipelineConfigInfo pipelineConfig{};
        Pipeline::defaultPipelineConfigInfo(pipelineConfig);
        pipelineConfig.renderPass = renderPass;
        pipelineConfig.pipelineLayout = pipelineLayout;
        pipeline = std::make_unique<Pipeline>(
            device,
            "shaders/simple_shader.vert.spv",
            "shaders/simple_shader.frag.spv",
            pipelineConfig);
    }

    void RenderSystem::update(entt::registry &registry, VkCommandBuffer commandBuffer, const Camera &camera, VkDescriptorSet globalDescriptorSet) const {
        pipeline->bind(commandBuffer);

        vkCmdBindDescriptorSets(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipelineLayout,
            0, 1,
            &globalDescriptorSet,
            0,
            nullptr
        );

        auto view = registry.view<TransformComponent, MaterialComponent, ModelComponent>();
        for (auto entity: view) {
            auto &transform = view.get<TransformComponent>(entity);
            //auto &material = view.get<MaterialComponent>(entity);
            auto &model = view.get<ModelComponent>(entity);

            SimplePushConstantData push{};
            push.modelMatrix= transform.mat4();
            push.normalMatrix = transform.normalMatrix();

            vkCmdPushConstants(
                commandBuffer,
                pipelineLayout,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0,
                sizeof(SimplePushConstantData),
                &push
            );

            model.model->bind(commandBuffer);
            model.model->draw(commandBuffer);
        }
    }
}
