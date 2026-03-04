#include "RenderSystemV2.hpp"

#include <cassert>
#include <stdexcept>

#include "asset/AssetManager.hpp"
#include "entity/Object.hpp"
#include "renderer/Buffer.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>

namespace Atlas {
    RenderSystemV2::RenderSystemV2(Device &device, VkRenderPass renderPass, const DescriptorSetLayout &globalSetLayout)
        : device(device) {
        createDescriptors();
        createPipelineLayout(globalSetLayout);
        createPipeline(renderPass);
        createGPUBuffers();

        defaultWhiteTextureHandle = AssetManager::get().createDefaultWhiteTexture();
        registerTexture(defaultWhiteTextureHandle);
    }

    RenderSystemV2::~RenderSystemV2() {
        vkDestroyPipelineLayout(device.device(), pipelineLayout, nullptr);
    }

    void RenderSystemV2::createDescriptors() {
        // Set 1 — bindless textures
        textureSetLayout = DescriptorSetLayout::Builder(device)
                .addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, MAX_TEXTURES)
                .setBindingFlags(0, VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT)
                .setLayoutFlags(VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT)
                .build();

        // Set 2 — per-object SSBO
        objectDataSetLayout = DescriptorSetLayout::Builder(device)
                .addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 1)
                .build();

        // Set 3 — lights SSBO
        lightSetLayout = DescriptorSetLayout::Builder(device)
                .addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT, 1)
                .build();

        rendererPool = DescriptorPool::Builder(device)
                .setMaxSets(3)
                .addPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_TEXTURES)
                .addPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2)
                .setPoolFlags(VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT)
                .build();

        if (!rendererPool->allocateDescriptor(textureSetLayout->getDescriptorSetLayout(), bindlessTextureSet))
            throw std::runtime_error("Failed to allocate bindless texture descriptor set");

        if (!rendererPool->allocateDescriptor(objectDataSetLayout->getDescriptorSetLayout(), objectDataSet))
            throw std::runtime_error("Failed to allocate object data descriptor set");

        if (!rendererPool->allocateDescriptor(lightSetLayout->getDescriptorSetLayout(), lightSet))
            throw std::runtime_error("Failed to allocate light descriptor set");
    }

    void RenderSystemV2::createPipelineLayout(const DescriptorSetLayout &globalSetLayout) {
        const std::vector layouts{
            globalSetLayout.getDescriptorSetLayout(), // set 0 — camera / global UBO
            textureSetLayout->getDescriptorSetLayout(), // set 1 — bindless textures
            objectDataSetLayout->getDescriptorSetLayout(), // set 2 — per-object SSBO
            lightSetLayout->getDescriptorSetLayout(), // set 3 — lights SSBO
        };

        VkPipelineLayoutCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        info.setLayoutCount = static_cast<uint32_t>(layouts.size());
        info.pSetLayouts = layouts.data();

        if (vkCreatePipelineLayout(device.device(), &info, nullptr, &pipelineLayout) != VK_SUCCESS)
            throw std::runtime_error("Failed to create pipeline layout");
    }

    void RenderSystemV2::createPipeline(VkRenderPass renderPass) {
        assert(pipelineLayout != VK_NULL_HANDLE);

        GraphicsPipelineConfigInfo config{};
        Pipeline::defaultGraphicsPipelineConfigInfo(config);
        config.bindingDescriptions = Mesh::Vertex::getBindingDescriptions();
        config.attributeDescriptions = Mesh::Vertex::getAttributeDescriptions();
        config.renderPass = renderPass;
        config.pipelineLayout = pipelineLayout;

        renderPipeline = std::make_unique<Pipeline>(
            device,
            "shaders/RenderSystemV2.indirect_shader.vert.spv",
            "shaders/RenderSystemV2.indirect_shader.frag.spv",
            config
        );
    }

    void RenderSystemV2::createGPUBuffers() {
        mergedVertexBuffer = std::make_unique<Buffer>(
            device, VERTEX_BUDGET,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
        );
        mergedIndexBuffer = std::make_unique<Buffer>(
            device, INDEX_BUDGET,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
        );

        indirectCommandBuffer = std::make_unique<Buffer>(
            device, sizeof(VkDrawIndexedIndirectCommand) * MAX_OBJECTS,
            VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
            VMA_MEMORY_USAGE_AUTO,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT
        );
        indirectCommandBuffer->map();

        objectDataBuffer = std::make_unique<Buffer>(
            device, sizeof(GPUObjectData) * MAX_OBJECTS,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VMA_MEMORY_USAGE_AUTO,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
        );
        auto objectBufferInfo = objectDataBuffer->descriptorInfo();
        DescriptorWriter(*objectDataSetLayout, *rendererPool)
                .writeBuffer(0, &objectBufferInfo)
                .overwrite(objectDataSet);

        lightsBuffer = std::make_unique<Buffer>(
            device, sizeof(Light) * MAX_LIGHTS,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VMA_MEMORY_USAGE_AUTO,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT
        );
        lightsBuffer->map();
        auto lightBufferInfo = lightsBuffer->descriptorInfo();
        DescriptorWriter(*lightSetLayout, *rendererPool)
                .writeBuffer(0, &lightBufferInfo)
                .overwrite(lightSet);
    }

    uint32_t RenderSystemV2::registerTexture(AssetHandle handle) {
        if (handle == INVALID_ASSET_HANDLE) return 0;

        auto [it, inserted] = handleToTextureSlot.emplace(handle, nextTextureSlot);
        if (!inserted) return it->second; // already registered

        if (nextTextureSlot >= MAX_TEXTURES)
            throw std::runtime_error("Exceeded maximum bindless texture count");

        const auto texture = AssetManager::get().getTexture(handle);
        if (!texture) return 0;

        const uint32_t slot = nextTextureSlot++;
        it->second = slot;

        const VkDescriptorImageInfo imageInfo{
            .sampler = texture->getSampler(),
            .imageView = texture->getImageView(),
            .imageLayout = texture->getImageLayout(),
        };

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = bindlessTextureSet;
        write.dstBinding = 0;
        write.dstArrayElement = slot;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &imageInfo;
        vkUpdateDescriptorSets(device.device(), 1, &write, 0, nullptr);

        return slot;
    }

    void RenderSystemV2::registerMesh(AssetHandle handle) {
        if (handle == INVALID_ASSET_HANDLE || meshAllocations.contains(handle)) return;

        const auto mesh = AssetManager::get().getMesh(handle);
        if (!mesh) return;

        const auto &vertices = mesh->getVertices();
        const auto &indices = mesh->getIndices();

        if (nextVertex + vertices.size() > VERTEX_BUDGET / sizeof(Mesh::Vertex))
            throw std::runtime_error("Merged vertex buffer out of space");
        if (nextIndex + indices.size() > INDEX_BUDGET / sizeof(uint32_t))
            throw std::runtime_error("Merged index buffer out of space");

        const MeshAllocation alloc{
            nextVertex, static_cast<uint32_t>(vertices.size()),
            nextIndex, static_cast<uint32_t>(indices.size()),
        };
        meshAllocations[handle] = alloc;

        const VkDeviceSize vSize = vertices.size() * sizeof(Mesh::Vertex);
        Buffer vStaging(
            device,
            vSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VMA_MEMORY_USAGE_AUTO,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT
        );

        vStaging.uploadData(vertices.data(), vSize);
        Buffer::copy(device, vStaging.get(), mergedVertexBuffer->get(), vSize, 0, nextVertex * sizeof(Mesh::Vertex));

        const VkDeviceSize iSize = indices.size() * sizeof(uint32_t);
        Buffer iStaging(
            device,
            iSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VMA_MEMORY_USAGE_AUTO,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT
        );

        iStaging.uploadData(indices.data(), iSize);
        Buffer::copy(device, iStaging.get(), mergedIndexBuffer->get(), iSize, 0, nextIndex * sizeof(uint32_t));

        nextVertex += alloc.vertexCount;
        nextIndex += alloc.indexCount;
    }

    uint32_t RenderSystemV2::resolveTextureIndex(AssetHandle handle) const {
        if (handle == INVALID_ASSET_HANDLE) return 0;
        const auto it = handleToTextureSlot.find(handle);
        return it != handleToTextureSlot.end() ? it->second : 0;
    }

    void RenderSystemV2::build(entt::registry &registry) {
        auto *drawCommands = static_cast<VkDrawIndexedIndirectCommand *>(indirectCommandBuffer->getMapped());

        for (auto entity: registry.view<TransformComponent, MaterialComponent, ModelComponent>()) {
            auto &transform = registry.get<TransformComponent>(entity);
            auto &material = registry.get<MaterialComponent>(entity);
            auto &model = registry.get<ModelComponent>(entity);

            registerTexture(material.albedoTexture);
            registerTexture(material.normalMap);
            registerTexture(material.metallicRoughnessMap);
            registerMesh(model.meshHandle);

            const auto allocIt = meshAllocations.find(model.meshHandle);
            if (allocIt == meshAllocations.end()) continue;

            const MeshAllocation &alloc = allocIt->second;

            // Dense index assigned by Storage — used as firstInstance so the shader
            // can index objectData[gl_InstanceIndex] without any indirection.
            const glm::mat4 model4x4 = transform.mat4();
            const auto denseIdx = static_cast<uint32_t>(objectData.size());
            objectData.emplace(
                entity, GPUObjectData{
                    .modelMatrix = model4x4,
                    .normalMatrix = glm::mat4(glm::inverseTranspose(glm::mat3(model4x4))),
                    .textureIndices = glm::uvec4(
                        resolveTextureIndex(material.albedoTexture != INVALID_ASSET_HANDLE ? material.albedoTexture : defaultWhiteTextureHandle),
                        resolveTextureIndex(material.normalMap != INVALID_ASSET_HANDLE ? material.normalMap : defaultWhiteTextureHandle),
                        resolveTextureIndex(material.metallicRoughnessMap != INVALID_ASSET_HANDLE ? material.metallicRoughnessMap : defaultWhiteTextureHandle),
                        0u
                    ),
                    .baseColor = material.baseColor,
                });

            drawCommands[denseIdx] = {
                .indexCount = alloc.indexCount,
                .instanceCount = 1,
                .firstIndex = alloc.firstIndex,
                .vertexOffset = static_cast<int32_t>(alloc.firstVertex),
                .firstInstance = denseIdx,
            };
        }

        // Lights
        for (auto entity: registry.view<TransformComponent, LightComponent>()) {
            auto [transform, light] = registry.get<TransformComponent, LightComponent>(entity);
            lights.emplace(entity, Light{
                               .type = static_cast<uint32_t>(light.type),
                               .intensity = light.intensity,
                               .range = light.range,
                               .innerConeAngle = light.innerConeAngle,
                               .color = light.color,
                               .outerConeAngle = light.outerConeAngle,
                               .position = transform.translation,
                           });
        }

        // Single contiguous upload for both SSBOs
        objectDataBuffer->uploadData(objectData.data(), sizeof(GPUObjectData) * objectData.size());
        lightsBuffer->uploadData(lights.data(), sizeof(Light) * lights.size());
    }


    void RenderSystemV2::render(VkCommandBuffer graphicsCommandBuffer, VkDescriptorSet globalSet) {
        if (objectData.empty()) return;

        const auto drawCount = static_cast<uint32_t>(objectData.size());
        renderPipeline->bind(graphicsCommandBuffer);

        const VkDescriptorSet sets[] = {
            globalSet,
            bindlessTextureSet,
            objectDataSet,
            lightSet
        };

        vkCmdBindDescriptorSets(graphicsCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 4, sets, 0, nullptr);

        VkBuffer vertexBuf = mergedVertexBuffer->get();
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(graphicsCommandBuffer, 0, 1, &vertexBuf, &offset);
        vkCmdBindIndexBuffer(graphicsCommandBuffer, mergedIndexBuffer->get(), 0, VK_INDEX_TYPE_UINT32);

        vkCmdDrawIndexedIndirect(
            graphicsCommandBuffer,
            indirectCommandBuffer->get(),
            0, drawCount,
            sizeof(VkDrawIndexedIndirectCommand)
        );
    }
} // namespace Atlas
