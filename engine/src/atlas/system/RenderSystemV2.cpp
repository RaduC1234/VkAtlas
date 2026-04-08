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

#include "core/Log.hpp"

namespace Atlas {
    RenderSystemV2::RenderSystemV2(Device &device, VkRenderPass renderPass, const DescriptorSetLayout &globalSetLayout)
        : device(device) {
        createDescriptors();
        createPipelineLayout(globalSetLayout);
        createPipelines(renderPass);
        defaultWhiteTextureHandle = AssetManager::get().createDefaultWhiteTexture();
        registerTexture(defaultWhiteTextureHandle);

        createGPUBuffers();
    }

    RenderSystemV2::~RenderSystemV2() {
        vkDestroyPipelineLayout(device.device(), pipelineLayout, nullptr);
    }

    void RenderSystemV2::createDescriptors() {
        // set 1 - environment
        environmentSetLayout = DescriptorSetLayout::Builder(device)
                .addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1)
                .addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1)
                .build();

        // Set 2 — bindless textures
        textureSetLayout = DescriptorSetLayout::Builder(device)
                .addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, MAX_TEXTURES)
                .setBindingFlags(0, VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT)
                .setLayoutFlags(VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT)
                .build();

        // Set 3 — per-object SSBO
        objectDataSetLayout = DescriptorSetLayout::Builder(device)
                .addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 1)
                .build();

        // Set 4 — lights SSBO
        lightSetLayout = DescriptorSetLayout::Builder(device)
                .addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT, 1)
                .build();

        rendererPool = DescriptorPool::Builder(device)
                .setMaxSets(5)
                .addPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_TEXTURES + 2)
                .addPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3)
                .setPoolFlags(VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT)
                .build();

        if (!rendererPool->allocateDescriptor(environmentSetLayout->getDescriptorSetLayout(), environmentSet))
            throw std::runtime_error("Failed to allocate environment descriptor set");

        if (!rendererPool->allocateDescriptor(textureSetLayout->getDescriptorSetLayout(), bindlessTextureSet))
            throw std::runtime_error("Failed to allocate bindless texture descriptor set");

        if (!rendererPool->allocateDescriptor(objectDataSetLayout->getDescriptorSetLayout(), objectDataSet))
            throw std::runtime_error("Failed to allocate object data descriptor set");

        if (!rendererPool->allocateDescriptor(objectDataSetLayout->getDescriptorSetLayout(), transparentObjectDataSet))
            throw std::runtime_error("Failed to allocate transparent object data descriptor set");

        if (!rendererPool->allocateDescriptor(lightSetLayout->getDescriptorSetLayout(), lightSet))
            throw std::runtime_error("Failed to allocate light descriptor set");
    }

    void RenderSystemV2::createPipelineLayout(const DescriptorSetLayout &globalSetLayout) {
        const std::vector layouts{
            globalSetLayout.getDescriptorSetLayout(), // set 0 — camera / global UBO
            environmentSetLayout->getDescriptorSetLayout(), // set 1 — IBL cubemaps
            textureSetLayout->getDescriptorSetLayout(), // set 2 — bindless textures
            objectDataSetLayout->getDescriptorSetLayout(), // set 3 — per-object SSBO
            lightSetLayout->getDescriptorSetLayout(), // set 4 — lights SSBO
        };

        VkPipelineLayoutCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        info.setLayoutCount = static_cast<uint32_t>(layouts.size());
        info.pSetLayouts = layouts.data();

        if (vkCreatePipelineLayout(device.device(), &info, nullptr, &pipelineLayout) != VK_SUCCESS)
            throw std::runtime_error("Failed to create pipeline layout");
    }

    void RenderSystemV2::createPipelines(VkRenderPass renderPass) {
        assert(pipelineLayout != VK_NULL_HANDLE);

        /*ComputePipelineConfigInfo computeConfig{};
        Pipeline::defaultComputePipelineConfigInfo(computeConfig);
        cullingPipeline = std::make_unique<Pipeline>(
            device,
            "shaders/RenderSystemV2.culling.comp.spv",
            computeConfig
        );*/

        GraphicsPipelineConfigInfo graphicsConfig{};
        Pipeline::defaultGraphicsPipelineConfigInfo(graphicsConfig);
        graphicsConfig.bindingDescriptions = Mesh::Vertex::getBindingDescriptions();
        graphicsConfig.attributeDescriptions = Mesh::Vertex::getAttributeDescriptions();
        graphicsConfig.renderPass = renderPass;
        graphicsConfig.pipelineLayout = pipelineLayout;

        renderPipeline = std::make_unique<Pipeline>(
            device,
            "shaders/RenderSystemV2.studio.vert.spv",
            "shaders/RenderSystemV2.real_time.frag.spv",
            graphicsConfig
        );

        graphicsConfig.bindingDescriptions = Mesh::Vertex::getBindingDescriptions();
        graphicsConfig.attributeDescriptions = Mesh::Vertex::getAttributeDescriptions();
        graphicsConfig.renderPass = renderPass;
        graphicsConfig.pipelineLayout = pipelineLayout;

        graphicsConfig.depthStencilInfo.depthWriteEnable = VK_FALSE;

        graphicsConfig.colorBlendAttachment.blendEnable = VK_TRUE;
        graphicsConfig.colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        graphicsConfig.colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        graphicsConfig.colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        graphicsConfig.colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        graphicsConfig.colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        graphicsConfig.colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

        transparentRenderPipeline = std::make_unique<Pipeline>(
            device,
            "shaders/RenderSystemV2.studio.vert.spv",
            "shaders/RenderSystemV2.real_time.frag.spv",
            graphicsConfig
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
            device,
            sizeof(VkDrawIndexedIndirectCommand) * MAX_OBJECTS,
            VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
            VMA_MEMORY_USAGE_AUTO,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT
        );
        indirectCommandBuffer->map();

        transparentIndirectCommandBuffer = std::make_unique<Buffer>(
            device,
            sizeof(VkDrawIndexedIndirectCommand) * MAX_OBJECTS,
            VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
            VMA_MEMORY_USAGE_AUTO,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT
        );
        transparentIndirectCommandBuffer->map();

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

        transparentObjectDataBuffer = std::make_unique<Buffer>(
            device, sizeof(GPUObjectData) * MAX_OBJECTS,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VMA_MEMORY_USAGE_AUTO,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
        );
        auto transparentObjectBufferInfo = transparentObjectDataBuffer->descriptorInfo();
        DescriptorWriter(*objectDataSetLayout, *rendererPool)
                .writeBuffer(0, &transparentObjectBufferInfo)
                .overwrite(transparentObjectDataSet);

        lightsBuffer = std::make_unique<Buffer>(
            device, sizeof(Light) * MAX_LIGHTS,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VMA_MEMORY_USAGE_AUTO,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT
        );
        lightsBuffer->map();
        std::memset(lightsBuffer->getMapped(), 0, sizeof(Light) * MAX_LIGHTS);

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
        auto skyboxView = registry.view<SkyboxComponent>();

        if (skyboxView.empty()) {
            AT_WARN("No skybox bound.");
        } else {
            if (skyboxView.size() > 1) {
                AT_WARN("Multiple skyboxes detected. Using the first one");
            }

            auto entity = *skyboxView.begin();
            const auto &skybox = registry.get<SkyboxComponent>(entity);
            const auto irradiance = AssetManager::get().getCubemap(skybox.irradianceHandle);
            const auto prefilter = AssetManager::get().getCubemap(skybox.prefilterHandle);

            if (irradiance && prefilter) {
                VkDescriptorImageInfo irradianceInfo{irradiance->getSampler(), irradiance->getImageView(), irradiance->getImageLayout()};
                VkDescriptorImageInfo prefilterInfo{prefilter->getSampler(), prefilter->getImageView(), prefilter->getImageLayout()};

                VkWriteDescriptorSet w0{};
                w0.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                w0.dstSet = environmentSet;
                w0.dstBinding = 0;
                w0.descriptorCount = 1;
                w0.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                w0.pImageInfo = &irradianceInfo;

                VkWriteDescriptorSet w1 = w0; // this is hacky, but I don't have time for a better implementation
                w1.dstBinding = 1;
                w1.pImageInfo = &prefilterInfo;

                const VkWriteDescriptorSet writes[] = {w0, w1};
                vkUpdateDescriptorSets(device.device(), 2, writes, 0, nullptr);
            }
        }

        auto *opaqueDrawCommands = static_cast<VkDrawIndexedIndirectCommand *>(indirectCommandBuffer->getMapped());
        auto *transparentDrawCommands = static_cast<VkDrawIndexedIndirectCommand *>(transparentIndirectCommandBuffer->getMapped());

        for (auto entity: registry.view<TransformComponent, MaterialComponent, ModelComponent>()) {
            auto &transform = registry.get<TransformComponent>(entity);
            auto &material = registry.get<MaterialComponent>(entity);
            auto &model = registry.get<ModelComponent>(entity);

            registerTexture(material.albedoTexture);
            registerTexture(material.normalMap);
            registerTexture(material.metallicRoughnessMap);
            registerTexture(material.ambientOcclusion);
            registerMesh(model.meshHandle);

            const auto allocIt = meshAllocations.find(model.meshHandle);
            if (allocIt == meshAllocations.end()) continue;

            const MeshAllocation &alloc = allocIt->second;
            const glm::mat4 model4x4 = transform.mat4();

            const GPUObjectData data{
                .modelMatrix = model4x4,
                .normalMatrix = glm::mat4(glm::inverseTranspose(glm::mat3(model4x4))),
                .textureIndices = glm::uvec4(
                    resolveTextureIndex(material.albedoTexture != INVALID_ASSET_HANDLE ? material.albedoTexture : defaultWhiteTextureHandle),
                    resolveTextureIndex(material.normalMap != INVALID_ASSET_HANDLE ? material.normalMap : defaultWhiteTextureHandle),
                    resolveTextureIndex(material.metallicRoughnessMap != INVALID_ASSET_HANDLE ? material.metallicRoughnessMap : defaultWhiteTextureHandle),
                    resolveTextureIndex(material.ambientOcclusion != INVALID_ASSET_HANDLE ? material.ambientOcclusion : defaultWhiteTextureHandle)
                ),
                .baseColor = material.baseColor, // baseColor.a drives transparency in shader
            };

            if (material.baseColor.w < 1.0f) {
                const auto denseIdx = static_cast<uint32_t>(transparentObjectData.size());
                transparentObjectData.emplace(entity, data);
                transparentDrawCommands[denseIdx] = {
                    .indexCount = alloc.indexCount,
                    .instanceCount = 1,
                    .firstIndex = alloc.firstIndex,
                    .vertexOffset = static_cast<int32_t>(alloc.firstVertex),
                    .firstInstance = denseIdx,
                };
            } else {
                const auto denseIdx = static_cast<uint32_t>(opaqueObjectData.size());
                opaqueObjectData.emplace(entity, data);
                opaqueDrawCommands[denseIdx] = {
                    .indexCount = alloc.indexCount,
                    .instanceCount = 1,
                    .firstIndex = alloc.firstIndex,
                    .vertexOffset = static_cast<int32_t>(alloc.firstVertex),
                    .firstInstance = denseIdx,
                };
            }
        }

        // Lights
        for (auto entity: registry.view<TransformComponent, LightComponent>()) {
            auto [transform, light] = registry.get<TransformComponent, LightComponent>(entity);
            lights.emplace(entity, Light(
                               static_cast<uint32_t>(light.type),
                               light.intensity,
                               light.range == 0.0f ? 20.0f : light.range,
                               light.innerConeAngle,
                               light.color,
                               light.outerConeAngle,
                               transform.translation,
                               light.width,
                               light.direction,
                               light.height
                           ));
        }

        // Upload all SSBOs
        objectDataBuffer->uploadData(opaqueObjectData.data(), sizeof(GPUObjectData) * opaqueObjectData.size());

        if (!transparentObjectData.empty()) {
            transparentObjectDataBuffer->uploadData(transparentObjectData.data(), sizeof(GPUObjectData) * transparentObjectData.size());
        }

        if (!lights.empty()) {
            lightsBuffer->uploadData(lights.data(), sizeof(Light) * lights.size());
        }
    }

    void RenderSystemV2::render(VkCommandBuffer graphicsCommandBuffer, VkDescriptorSet globalSet) {
        // Opaque (non-transparent) pass
        if (!opaqueObjectData.empty()) {
            const auto opaqueDrawCount = static_cast<uint32_t>(opaqueObjectData.size());
            renderPipeline->bind(graphicsCommandBuffer);

            const VkDescriptorSet opaqueSets[] = {
                globalSet,
                environmentSet,
                bindlessTextureSet,
                objectDataSet,
                lightSet
            };

            vkCmdBindDescriptorSets(graphicsCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 5, opaqueSets, 0, nullptr);

            VkBuffer opaqueVertexBuf = mergedVertexBuffer->get();
            VkDeviceSize opaqueOffset = 0;
            vkCmdBindVertexBuffers(graphicsCommandBuffer, 0, 1, &opaqueVertexBuf, &opaqueOffset);
            vkCmdBindIndexBuffer(graphicsCommandBuffer, mergedIndexBuffer->get(), 0, VK_INDEX_TYPE_UINT32);

            vkCmdDrawIndexedIndirect(
                graphicsCommandBuffer,
                indirectCommandBuffer->get(),
                0, opaqueDrawCount,
                sizeof(VkDrawIndexedIndirectCommand)
            );
        }

        /*// Transparent pass
        if (!transparentObjectData.empty()) {
            const auto transparentDrawCount = static_cast<uint32_t>(transparentObjectData.size());
            transparentRenderPipeline->bind(graphicsCommandBuffer);

            const VkDescriptorSet transparentSets[] = {
                globalSet,
                environmentSet,
                bindlessTextureSet,
                transparentObjectDataSet,
                lightSet
            };

            vkCmdBindDescriptorSets(graphicsCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 5, transparentSets, 0, nullptr);

            VkBuffer transparentVertexBuf = mergedVertexBuffer->get();
            VkDeviceSize transparentOffset = 0;
            vkCmdBindVertexBuffers(graphicsCommandBuffer, 0, 1, &transparentVertexBuf, &transparentOffset);
            vkCmdBindIndexBuffer(graphicsCommandBuffer, mergedIndexBuffer->get(), 0, VK_INDEX_TYPE_UINT32);

            vkCmdDrawIndexedIndirect(
                graphicsCommandBuffer,
                transparentIndirectCommandBuffer->get(),
                0, transparentDrawCount,
                sizeof(VkDrawIndexedIndirectCommand)
            );
        }*/
    }
} // namespace Atlas
