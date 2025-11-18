#include "RenderSystem.hpp"

// std
#include <cassert>
#include <stdexcept>

#include "asset/AssetManager.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

#include "entity/Object.hpp"
#include "renderer/Buffer.hpp"
#include "renderer/SwapChain.hpp"

namespace Atlas {
    struct alignas(16) GlobalUbo {
        glm::mat4 projection{1.0f};
        glm::mat4 view{1.0f};
        glm::vec4 ambientColor{1.0f, 1.0f, 1.0f, 0.002f};
        glm::vec3 lightPosition{-1.0f};
        float padding1;
        glm::vec4 lightColor{1.0f};
    };

    struct SimplePushConstantData {
        glm::mat4 modelMatrix{1.0f};
        glm::mat4 normalMatrix{1.0f};
        glm::vec4 baseColor{1.0f};
        uint32_t textureIndex{0};
    };

    RenderSystem::RenderSystem(Device &device, VkRenderPass renderPass): device(device) {
        createDescriptors();
        createPipelineLayout();
        createPipeline(renderPass);

        const AssetHandle defaultTexture = AssetManager::get().createDefaultWhiteTexture();
        registerTexture(defaultTexture);
        commitSamplersToDescriptors();
        defaultWhiteTextureHandle = defaultTexture;
    }

    RenderSystem::~RenderSystem() {
        vkDestroyPipelineLayout(device.device(), pipelineLayout, nullptr);
    }

    void RenderSystem::createDescriptors() {
        // Create UBO buffers for each frame in flight
        uboBuffers.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);
        for (auto &uboBuffer: uboBuffers) {
            uboBuffer = std::make_unique<Buffer>(
                device,
                sizeof(GlobalUbo),
                1,
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VMA_MEMORY_USAGE_AUTO,
                device.properties.limits.minUniformBufferOffsetAlignment,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT
            );
            uboBuffer->map();
        }

        globalSetLayout = DescriptorSetLayout::Builder(device)
                .addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
                .build();

        globalPool = DescriptorPool::Builder(device)
                .setMaxSets(SwapChain::MAX_FRAMES_IN_FLIGHT)
                .addPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, SwapChain::MAX_FRAMES_IN_FLIGHT)
                .build();

        globalDescriptorSets.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);
        for (int i = 0; i < globalDescriptorSets.size(); i++) {
            auto bufferInfo = uboBuffers[i]->descriptorInfo();
            DescriptorWriter(*globalSetLayout, *globalPool)
                    .writeBuffer(0, &bufferInfo)
                    .build(globalDescriptorSets[i]);
        }

        textureSetLayout = DescriptorSetLayout::Builder(device)
               .addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1024)
               .setBindingFlags(0, VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT)
               .setLayoutFlags(VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT)
               .build();

        bindlessTexturePool = DescriptorPool::Builder(device)
                .setMaxSets(1)
                .addPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1024)
                .setPoolFlags(VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT)
                .build();

        if (!bindlessTexturePool->allocateDescriptor(textureSetLayout->getDescriptorSetLayout(), bindlessTextureSet)) {
            throw std::runtime_error("Failed to allocate bindless texture descriptor set");
        }
    }

    void RenderSystem::createPipelineLayout() {
        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(SimplePushConstantData);

        std::vector<VkDescriptorSetLayout> descriptorSetLayouts{
            globalSetLayout->getDescriptorSetLayout(),
            textureSetLayout->getDescriptorSetLayout()
        };

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

    uint32_t RenderSystem::registerTexture(AssetHandle handle) {
        if (handle == INVALID_ASSET_HANDLE) {
            return 1; // Return default white texture index
        }

        auto it = handleToGPUIndex.find(handle);
        if (it != handleToGPUIndex.end()) {
            return it->second;
        }

        if (nextTextureIndex >= 1024) {
            throw std::runtime_error("Exceeded maximum bindless texture count (1024)");
        }

        const auto texture = AssetManager::get().getTexture(handle);

        if (!texture) {
            return 1; // Return default texture if asset not found
        }

        uint32_t gpuIndex = nextTextureIndex++;
        handleToGPUIndex[handle] = gpuIndex;
        waitingToBeCommitedSamplers.push_back(texture);

        return gpuIndex;
    }

    void RenderSystem::commitSamplersToDescriptors() {
        if (waitingToBeCommitedSamplers.empty()) {
            return;
        }

        std::vector<VkDescriptorImageInfo> imageInfos;
        imageInfos.reserve(waitingToBeCommitedSamplers.size());

        for (const auto &texture: waitingToBeCommitedSamplers) {
            VkDescriptorImageInfo imageInfo{};
            imageInfo.imageLayout = texture->getImageLayout();
            imageInfo.imageView = texture->getImageView();
            imageInfo.sampler = texture->getSampler();
            imageInfos.push_back(imageInfo);
        }

        VkWriteDescriptorSet writeDescriptorSet{};
        writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptorSet.dstSet = bindlessTextureSet;
        writeDescriptorSet.dstBinding = 0;
        uint32_t startArrayElement = nextTextureIndex - static_cast<uint32_t>(waitingToBeCommitedSamplers.size());
        writeDescriptorSet.dstArrayElement = startArrayElement; // update only from the end of the gpu array.
        writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writeDescriptorSet.descriptorCount = static_cast<uint32_t>(imageInfos.size());
        writeDescriptorSet.pImageInfo = imageInfos.data();

        vkUpdateDescriptorSets(device.device(), 1, &writeDescriptorSet, 0, nullptr);

        waitingToBeCommitedSamplers.clear();
    }

    void RenderSystem::prepareTextures(entt::registry &registry) {
        bool newTexturesRegistered = false;

        auto view = registry.view<MaterialComponent>();
        for (auto entity: view) {
            auto &material = view.get<MaterialComponent>(entity);

            if (material.albedoTexture != INVALID_ASSET_HANDLE) {
                if (handleToGPUIndex.find(material.albedoTexture) == handleToGPUIndex.end()) {
                    registerTexture(material.albedoTexture);
                    newTexturesRegistered = true;
                }
            }
        }

        if (newTexturesRegistered) {
            commitSamplersToDescriptors();
        }
    }

    void RenderSystem::updateUBO(int frameIndex, const glm::mat4 &projection, const glm::mat4 &view, const glm::vec4 &ambientColor, const glm::vec3 &lightPosition, const glm::vec4 &lightColor) {
        GlobalUbo ubo{};
        ubo.projection = projection;
        ubo.view = view;
        ubo.ambientColor = ambientColor;
        ubo.lightPosition = lightPosition;
        ubo.lightColor = lightColor;

        uboBuffers[frameIndex]->uploadData(&ubo, sizeof(GlobalUbo));
    }

    void RenderSystem::render(entt::registry &registry, VkCommandBuffer commandBuffer, int frameIndex) {
        pipeline->bind(commandBuffer);

        vkCmdBindDescriptorSets(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipelineLayout,
            0, 1,
            &globalDescriptorSets[frameIndex],
            0,
            nullptr
        );

        vkCmdBindDescriptorSets(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipelineLayout,
            1, 1,
            &bindlessTextureSet,
            0,
            nullptr
        );

        auto& assetManager = AssetManager::get();
        auto view = registry.view<TransformComponent, MaterialComponent, ModelComponent>();

        for (auto entity: view) {
            auto &transform = view.get<TransformComponent>(entity);
            auto &material = view.get<MaterialComponent>(entity);
            auto &modelComp = view.get<ModelComponent>(entity);

            auto mesh = assetManager.getMesh(modelComp.meshHandle);
            if (!mesh) continue; // skip if mesh not loaded. todo: implement resource streaming

            SimplePushConstantData push{};
            push.modelMatrix = transform.mat4();
            push.normalMatrix = transform.normalMatrix();
            push.baseColor = material.baseColor;

            AssetHandle textureHandle = material.albedoTexture != INVALID_ASSET_HANDLE
                ? material.albedoTexture
                : defaultWhiteTextureHandle;

            push.textureIndex = handleToGPUIndex[textureHandle];

            vkCmdPushConstants(
                commandBuffer,
                pipelineLayout,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0,
                sizeof(SimplePushConstantData),
                &push
            );

            mesh->bind(commandBuffer);
            mesh->draw(commandBuffer);
        }
    }
}
