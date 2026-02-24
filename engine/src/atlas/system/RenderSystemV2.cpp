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

    struct RenderSystemV2::GPUObjectData {
        glm::mat4 modelMatrix;
        glm::mat4 normalMatrix;
        glm::uvec4 textureIndices; // albedo, normal, metallicRoughness, unused
        glm::vec4 baseColor; // material base color (RGBA)
    };

    // Tracks where a mesh lives inside the merged vertex/index buffers
    struct RenderSystemV2::MeshAllocation {
        uint32_t firstVertex = 0;
        uint32_t vertexCount = 0;
        uint32_t firstIndex = 0;
        uint32_t indexCount = 0;
    };

    struct RenderSystemV2::Light {
        alignas(4) uint32_t type{static_cast<uint32_t>(LightType::SPOT)}; // 0 = directional, 1 = point, 2 = spot
        alignas(4) float intensity{1.0f};
        alignas(4) float range{0.0f};
        alignas(4) float innerConeAngle{0.0f};
        alignas(16) glm::vec3 color{1.0f};
        alignas(4) float outerConeAngle{glm::radians(45.0f)};
        alignas(16) glm::vec3 position{0.0f};
        alignas(4) float padding{0.0f};
    };

    RenderSystemV2::RenderSystemV2(Device &device, VkRenderPass renderPass, const DescriptorSetLayout &globalSetLayout) : device(device) {
        createDescriptors();
        createPipelineLayout(globalSetLayout);
        createPipeline(renderPass);
        createMergedBuffers();
        createIndirectBuffers();

        const AssetHandle defaultTexture = AssetManager::get().createDefaultWhiteTexture();
        registerTexture(defaultTexture);
        commitSamplersToDescriptors();
        defaultWhiteTextureHandle = defaultTexture;
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

        // Set 2 — object SSBO
        objectDataSetLayout = DescriptorSetLayout::Builder(device)
                .addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 1)
                .build();

        rendererPool = DescriptorPool::Builder(device)
                .setMaxSets(2)
                .addPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_TEXTURES)
                .addPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1)
                .setPoolFlags(VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT)
                .build();

        if (!rendererPool->allocateDescriptor(textureSetLayout->getDescriptorSetLayout(), bindlessTextureSet))
            throw std::runtime_error("Failed to allocate bindless texture descriptor set");

        if (!rendererPool->allocateDescriptor(objectDataSetLayout->getDescriptorSetLayout(), objectDataSet))
            throw std::runtime_error("Failed to allocate object data descriptor set");
    }

    void RenderSystemV2::createPipelineLayout(const DescriptorSetLayout &globalSetLayout) {
        std::vector<VkDescriptorSetLayout> layouts{
            globalSetLayout.getDescriptorSetLayout(), // set 0 — camera / global UBO
            textureSetLayout->getDescriptorSetLayout(), // set 1 — bindless textures
            objectDataSetLayout->getDescriptorSetLayout(), // set 2 — object SSBO
        };

        VkPipelineLayoutCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        info.setLayoutCount = static_cast<uint32_t>(layouts.size());
        info.pSetLayouts = layouts.data();
        info.pushConstantRangeCount = 0;
        info.pPushConstantRanges = nullptr;

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

        pipeline = std::make_unique<Pipeline>(
            device,
            "shaders/RenderSystemV2.indirect_shader.vert.spv",
            "shaders/RenderSystemV2.indirect_shader.frag.spv",
            config
        );
    }

    void RenderSystemV2::createMergedBuffers() {
        mergedVertexBuffer = std::make_unique<Buffer>(
            device,
            VERTEX_BUDGET,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
        );

        mergedIndexBuffer = std::make_unique<Buffer>(
            device,
            INDEX_BUDGET,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
        );
    }

    void RenderSystemV2::createIndirectBuffers() {
        indirectCommandBuffer = std::make_unique<Buffer>(
            device,
            sizeof(VkDrawIndexedIndirectCommand) * MAX_OBJECTS,
            VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
            VMA_MEMORY_USAGE_AUTO,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT
        );

        // Object SSBO
        objectDataBuffer = std::make_unique<Buffer>(
            device,
            sizeof(GPUObjectData) * MAX_OBJECTS,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VMA_MEMORY_USAGE_AUTO,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT
        );

        // Pre-map both buffers permanently — they're written every frame
        indirectCommandBuffer->map();
        objectDataBuffer->map();
        auto bufferInfo = objectDataBuffer->descriptorInfo();
        DescriptorWriter(*objectDataSetLayout, *rendererPool)
                .writeBuffer(0, &bufferInfo)
                .overwrite(objectDataSet);
    }


    uint32_t RenderSystemV2::registerTexture(AssetHandle handle) {
        if (handle == INVALID_ASSET_HANDLE) return 0;

        auto it = handleToGPUIndex.find(handle);
        if (it != handleToGPUIndex.end()) {
            return it->second;
        }

        if (nextTextureIndex >= MAX_TEXTURES) {
            throw std::runtime_error("Exceeded maximum bindless texture count");
        }

        auto texture = AssetManager::get().getTexture(handle);
        if (!texture) {
            return 0;
        }

        uint32_t idx = nextTextureIndex++;
        handleToGPUIndex[handle] = idx;
        waitingToBeCommitedSamplers.push_back(texture);
        return idx;
    }

    uint32_t RenderSystemV2::registerMesh(AssetHandle handle) {
        if (handle == INVALID_ASSET_HANDLE) return 0;

        // Already registered — return existing first index
        auto it = meshAllocations.find(handle);
        if (it != meshAllocations.end()) return it->second.firstIndex;

        const auto mesh = AssetManager::get().getMesh(handle);

        if (!mesh) {
            return 0;
        }

        const auto &vertices = mesh->getVertices();
        const auto &indices = mesh->getIndices();

        if (nextVertex + vertices.size() > VERTEX_BUDGET / sizeof(Mesh::Vertex)) {
            throw std::runtime_error("Merged vertex buffer out of space");
        }

        if (nextIndex + indices.size() > INDEX_BUDGET / sizeof(uint32_t)) {
            throw std::runtime_error("Merged index buffer out of space");
        }

        const MeshAllocation alloc(
            nextVertex,
            static_cast<uint32_t>(vertices.size()),
            nextIndex,
            static_cast<uint32_t>(indices.size())
        );

        meshAllocations[handle] = alloc;

        pendingMeshUploads.push_back({handle, vertices, indices, nextVertex, nextIndex,});

        nextVertex += alloc.vertexCount;
        nextIndex += alloc.indexCount;

        return alloc.firstIndex;
    }

    void RenderSystemV2::commitSamplersToDescriptors() {
        if (waitingToBeCommitedSamplers.empty()) return;

        std::vector<VkDescriptorImageInfo> imageInfos;
        imageInfos.reserve(waitingToBeCommitedSamplers.size());

        for (const auto &tex: waitingToBeCommitedSamplers) {
            imageInfos.push_back({
                .sampler = tex->getSampler(),
                .imageView = tex->getImageView(),
                .imageLayout = tex->getImageLayout(),
            });
        }

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = bindlessTextureSet;
        write.dstBinding = 0;
        write.dstArrayElement = nextTextureIndex - static_cast<uint32_t>(waitingToBeCommitedSamplers.size());
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = static_cast<uint32_t>(imageInfos.size());
        write.pImageInfo = imageInfos.data();

        vkUpdateDescriptorSets(device.device(), 1, &write, 0, nullptr);
        waitingToBeCommitedSamplers.clear();
    }

    void RenderSystemV2::commitMeshesToDescriptors() {
        if (pendingMeshUploads.empty()) return;

        for (const auto &upload: pendingMeshUploads) {
            // vertices
            VkDeviceSize vOffset = upload.firstVertex * sizeof(Mesh::Vertex);
            VkDeviceSize vSize = upload.vertices.size() * sizeof(Mesh::Vertex);

            Buffer vStaging(
                device, vSize,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VMA_MEMORY_USAGE_AUTO,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT
            );
            vStaging.uploadData(upload.vertices.data(), vSize);
            Buffer::copy(device, vStaging.get(), mergedVertexBuffer->get(), vSize, 0, vOffset);

            // indices
            VkDeviceSize iOffset = upload.firstIndex * sizeof(uint32_t);
            VkDeviceSize iSize = upload.indices.size() * sizeof(uint32_t);

            Buffer iStaging(
                device, iSize,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VMA_MEMORY_USAGE_AUTO,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT
            );

            iStaging.uploadData(upload.indices.data(), iSize);
            Buffer::copy(device, iStaging.get(), mergedIndexBuffer->get(), iSize, 0, iOffset);
        }

        pendingMeshUploads.clear();
    }

    void RenderSystemV2::prepare(entt::registry &registry) {
        bool newTextures = false;
        bool newMeshes = false;

        auto view = registry.view<TransformComponent, MaterialComponent, ModelComponent>();
        for (auto entity: view) {
            auto &material = view.get<MaterialComponent>(entity);
            auto &model = view.get<ModelComponent>(entity);

            if (!handleToGPUIndex.contains(material.albedoTexture)) {
                registerTexture(material.albedoTexture);
                newTextures = true;
            }

            if (!handleToGPUIndex.contains(material.normalMap)) {
                registerTexture(material.normalMap);
                newTextures = true;
            }
            if (!handleToGPUIndex.contains(material.metallicRoughnessMap)) {
                registerTexture(material.metallicRoughnessMap);
                newTextures = true;
            }

            if (model.meshHandle != INVALID_ASSET_HANDLE &&
                !meshAllocations.contains(model.meshHandle)) {
                registerMesh(model.meshHandle);
                newMeshes = true;
            }
        }

        // These do GPU uploads — safe here since we're outside command buffer recording
        if (newTextures) commitSamplersToDescriptors();
        if (newMeshes) commitMeshesToDescriptors();

        //view = registry.view<LightComponent>();
    }

    void RenderSystemV2::rebuildDrawList(entt::registry &registry) {
        currentDrawCount = 0;

        auto *drawCommands = static_cast<VkDrawIndexedIndirectCommand *>(indirectCommandBuffer->getMapped());
        auto *objectData = static_cast<GPUObjectData *>(objectDataBuffer->getMapped());

        auto view = registry.view<TransformComponent, MaterialComponent, ModelComponent>();
        for (auto entity: view) {
            auto &transform = view.get<TransformComponent>(entity);
            auto &material = view.get<MaterialComponent>(entity);
            auto &model = view.get<ModelComponent>(entity);

            auto allocIt = meshAllocations.find(model.meshHandle);
            if (allocIt == meshAllocations.end()) continue;

            const MeshAllocation &alloc = allocIt->second;
            uint32_t slot = currentDrawCount;

            drawCommands[slot].indexCount = alloc.indexCount;
            drawCommands[slot].instanceCount = 1;
            drawCommands[slot].firstIndex = alloc.firstIndex;
            drawCommands[slot].vertexOffset = static_cast<int32_t>(alloc.firstVertex);
            drawCommands[slot].firstInstance = slot;

            glm::mat4 model4x4 = transform.mat4();
            objectData[slot].modelMatrix = model4x4;
            objectData[slot].normalMatrix = glm::mat4(glm::inverseTranspose(glm::mat3(model4x4)));
            objectData[slot].baseColor = material.baseColor;


            AssetHandle albedoHandle = material.albedoTexture != INVALID_ASSET_HANDLE
                                           ? material.albedoTexture
                                           : defaultWhiteTextureHandle;

            AssetHandle normalHandle = material.normalMap != INVALID_ASSET_HANDLE
                                           ? material.normalMap
                                           : defaultWhiteTextureHandle;

            AssetHandle roughnessHandle = material.metallicRoughnessMap != INVALID_ASSET_HANDLE
                                              ? material.metallicRoughnessMap
                                              : defaultWhiteTextureHandle;

            auto resolveTexIdx = [&](AssetHandle h) -> uint32_t {
                auto it = handleToGPUIndex.find(h);
                return it != handleToGPUIndex.end() ? it->second : 0;
            };

            objectData[slot].textureIndices = glm::uvec4(
                resolveTexIdx(albedoHandle),
                resolveTexIdx(normalHandle),
                resolveTexIdx(roughnessHandle),
                0
            );

            ++currentDrawCount;
        }
    }

    void RenderSystemV2::render(entt::registry &registry, VkCommandBuffer commandBuffer, VkDescriptorSet globalSet) {
        rebuildDrawList(registry);

        if (currentDrawCount == 0) return;

        pipeline->bind(commandBuffer);

        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &globalSet, 0, nullptr);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 1, 1, &bindlessTextureSet, 0, nullptr);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 2, 1, &objectDataSet, 0, nullptr);

        VkBuffer vertexBuf = mergedVertexBuffer->get();
        VkBuffer indexBuf = mergedIndexBuffer->get();
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuf, &offset);
        vkCmdBindIndexBuffer(commandBuffer, indexBuf, 0, VK_INDEX_TYPE_UINT32);

        vkCmdDrawIndexedIndirect(
            commandBuffer,
            indirectCommandBuffer->get(),
            0,
            currentDrawCount,
            sizeof(VkDrawIndexedIndirectCommand)
        );
    }
}
