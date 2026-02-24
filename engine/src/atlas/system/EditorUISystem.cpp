#if defined(ATLAS_PLATFORM_DESKTOP)

#include "EditorUISystem.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

#include <stdexcept>

#include "core/Log.hpp"
#include "entity/Object.hpp"

namespace Atlas {
    struct alignas(16) EditorUISystem::BillboardPushConstant {
        glm::vec3 position;
        float scale;
        glm::vec4 color;
        glm::uint textureIndex;
    };

    EditorUISystem::EditorUISystem(Device &device, VkRenderPass renderPass, VkDescriptorSetLayout globalSetLayout) : device(device) {
        createDescriptors();
        createPipelineLayout(globalSetLayout);
        createPipeline(renderPass);
        uploadTextures();
    }

    void EditorUISystem::createPipelineLayout(VkDescriptorSetLayout globalSetLayout) {
        std::vector descriptorSetLayouts{globalSetLayout};

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
        pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();
        pipelineLayoutInfo.pushConstantRangeCount = 0;
        pipelineLayoutInfo.pPushConstantRanges = nullptr;
        if (vkCreatePipelineLayout(device.device(), &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
            throw std::runtime_error("failed to create pipeline layout!");
        }
    }

    void EditorUISystem::createDescriptors() {
        auto builder = DescriptorSetLayout::Builder(device);

        for (int i = 0; i < MAX_TEXTURES; i++) {
            builder.addBinding(i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);
        }

        textureSetLayout = builder.build();

        bindlessTexturePool = DescriptorPool::Builder(device)
                .addPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_TEXTURES)
                .setMaxSets(1)
                .build();
    }

    void EditorUISystem::createPipeline(VkRenderPass renderPass) {
        assert(pipelineLayout != VK_NULL_HANDLE && "Cannot create pipeline before pipeline layout!");

        GraphicsPipelineConfigInfo pipelineConfig{};
        Pipeline::defaultGraphicsPipelineConfigInfo(pipelineConfig);
        pipelineConfig.bindingDescriptions.clear();
        pipelineConfig.attributeDescriptions.clear();
        pipelineConfig.renderPass = renderPass;
        pipelineConfig.pipelineLayout = pipelineLayout;
        pipeline = std::make_unique<Pipeline>(
            device,
            "shaders/EditorUISystem.billboards.vert.spv",
            "shaders/EditorUISystem.billboards.frag.spv",
            pipelineConfig);
    }

    void EditorUISystem::uploadTextures() {
        static const std::array<std::string, 3> iconPaths = {"icons/light_point.png", "icons/light_spot", "light/directional"};
        auto &assetManager = AssetManager::get();
        DescriptorWriter writer(*textureSetLayout, *bindlessTexturePool);

        if (const auto tex = assetManager.getTexture(assetManager.createDefaultWhiteTexture())) {
            VkDescriptorImageInfo imageInfo{};
            imageInfo.sampler = tex->getSampler();
            imageInfo.imageView = tex->getImageView();
            imageInfo.imageLayout = tex->getImageLayout();

            writer.writeImage(0, &imageInfo);
        }

        for (uint32_t i = 1; i <= static_cast<uint32_t>(LightType::DIRECTIONAL); i++) {
            if (const auto tex = assetManager.getTexture(assetManager.loadTexture(iconPaths[i]))) {
                VkDescriptorImageInfo imageInfo{};
                imageInfo.sampler = tex->getSampler();
                imageInfo.imageView = tex->getImageView();
                imageInfo.imageLayout = tex->getImageLayout();

                writer.writeImage(i, &imageInfo);
            }
        }
        writer.build(textureSet);
    }

    void EditorUISystem::render(entt::registry &registry, VkCommandBuffer commandBuffer, VkDescriptorSet globalDescriptorSet) const {
        this->pipeline->bind(commandBuffer);

        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &globalDescriptorSet, 0, nullptr);

        const auto view = registry.view<TransformComponent, LightComponent>();
        BillboardPushConstant push{};

        for (const auto entity: view) {
            auto &transform = view.get<TransformComponent>(entity);
            auto &light = view.get<LightComponent>(entity);

            push.color = glm::vec4(light.color, 1.0f);
            push.position = transform.translation;
            push.scale = transform.scale.x;
            push.textureIndex = static_cast<glm::uint>(light.type);
        }

        vkCmdDraw(commandBuffer, 6, 1, 0, 0);
    }
}

#endif
