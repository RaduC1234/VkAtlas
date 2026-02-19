#include "CullingSystem.hpp"

#include "core/Log.hpp"
#include "entity/Object.hpp"
#include <iostream>

namespace Atlas {
    CullingSystem::CullingSystem(Device &device, const DescriptorSetLayout& globalSetLayout) : device(device) {
        createBuffers();
        createDescriptors();
        createPipelineLayout(globalSetLayout);
        createPipeline();
    }

    CullingSystem::~CullingSystem() {
        if (cullingLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device.device(), this->cullingLayout, nullptr);
        }
    }

    void CullingSystem::createBuffers() {
        this->instanceBuffer = std::make_unique<Buffer>(
            device,
            sizeof(GPUInstanceData),
            10000, // Max 10,000 instances
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VMA_MEMORY_USAGE_AUTO,
            0,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT
        );
        this->instanceBuffer->map();

        this->meshDataBuffer = std::make_unique<Buffer>(
            device,
            sizeof(GPUMeshData),
            1000, // Max 1,000 meshes
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VMA_MEMORY_USAGE_AUTO,
            0,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT
        );
        this->meshDataBuffer->map();

        this->drawCommandBuffer = std::make_unique<Buffer>(
            device,
            sizeof(VkDrawIndexedIndirectCommand),
            10000, // Max 10,000 draw commands
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY,
            0
        );

        this->drawCountBuffer = std::make_unique<Buffer>(
            device,
            sizeof(uint32_t),
            1,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY,
            0
        );
    }

    void CullingSystem::createDescriptors() {
        cullingSetLayout = DescriptorSetLayout::Builder(device)
                .addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT) // instances
                .addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT) // meshes
                .addBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT) // draw commands (output)
                .addBinding(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT) // draw count (output)
                .build();

        cullingPool = DescriptorPool::Builder(device)
                .setMaxSets(1)
                .addPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4)
                .addPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1)
                .build();

        if (!cullingPool->allocateDescriptor(cullingSetLayout->getDescriptorSetLayout(), cullingDescSet)) {
            throw std::runtime_error("Failed to allocate culling descriptor set");
        }

        auto instanceInfo = instanceBuffer->descriptorInfo();
        auto meshInfo = meshDataBuffer->descriptorInfo();
        auto drawCommandInfo = drawCommandBuffer->descriptorInfo();
        auto drawCountInfo = drawCountBuffer->descriptorInfo();

        DescriptorWriter(*cullingSetLayout, *cullingPool)
                .writeBuffer(0, &instanceInfo)
                .writeBuffer(1, &meshInfo)
                .writeBuffer(2, &drawCommandInfo)
                .writeBuffer(3, &drawCountInfo)
                .overwrite(cullingDescSet);
    }

    void CullingSystem::createPipelineLayout(const DescriptorSetLayout& globalDescriptorSetLayout) {
        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pushRange.offset = 0;
        pushRange.size = sizeof(uint32_t); // totalInstances

        const std::vector<VkDescriptorSetLayout> layouts = {
            globalDescriptorSetLayout.getDescriptorSetLayout(),
            cullingSetLayout->getDescriptorSetLayout()
        };

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = static_cast<uint32_t>(layouts.size());
        layoutInfo.pSetLayouts = layouts.data();
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;


        if (vkCreatePipelineLayout(device.device(), &layoutInfo, nullptr, &cullingLayout) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create culling pipeline layout");
        }
    }

    void CullingSystem::createPipeline() {
        ComputePipelineConfigInfo pipelineConfig{};
        pipelineConfig.pipelineLayout = cullingLayout;

        cullingPipeline = std::make_unique<Pipeline>(
            device,
            "shaders/cull_shader.comp.spv",
            pipelineConfig
        );
    }

    void CullingSystem::build(entt::registry &registry) {
        instances.clear();
        meshes.clear();

        auto view = registry.view<TransformComponent, ModelComponent>();

        uint32_t instanceID = 0;
        for (auto entity: view) {
            auto &transform = view.get<TransformComponent>(entity);

            GPUInstanceData instance{};
            instance.transform = transform.mat4();
            instance.boundingSphere = glm::vec4(transform.translation, 2.0f);

            glm::vec3 scale = transform.scale;
            float maxScale = glm::max(glm::max(scale.x, scale.y), scale.z);
            instance.aabbMin = transform.translation - glm::vec3(maxScale);
            instance.aabbMax = transform.translation + glm::vec3(maxScale);

            instance.meshID = instanceID;
            instance.textureIndex = 0;  // Will be set by setTextureIndices()

            instances.push_back(instance);
            instanceID++;
        }

        totalInstances = static_cast<uint32_t>(instances.size());

        std::cout << "CullingSystem: Built " << totalInstances << " instances" << std::endl;

        if (!instances.empty()) {
            instanceBuffer->uploadData(instances.data(), instances.size() * sizeof(GPUInstanceData));
        }
    }

    void CullingSystem::setTextureIndices(const std::vector<uint32_t>& textureIndices) {
        if (textureIndices.size() != instances.size()) {
            std::cerr << "CullingSystem: Texture indices count mismatch! Expected " << instances.size()
                      << " but got " << textureIndices.size() << std::endl;
            return;
        }

        for (size_t i = 0; i < instances.size(); ++i) {
            instances[i].textureIndex = textureIndices[i];
        }

        std::cout << "CullingSystem: Set texture indices for " << instances.size() << " instances" << std::endl;

        // Re-upload instance buffer with updated texture indices
        if (!instances.empty()) {
            instanceBuffer->uploadData(instances.data(), instances.size() * sizeof(GPUInstanceData));
        }
    }

    void CullingSystem::setMeshData(const std::vector<GPUMeshData>& meshDataArray) {
        meshes = meshDataArray;

        //std::cout << "CullingSystem: Set " << meshes.size() << " mesh data entries" << std::endl;

        if (!meshes.empty()) {
            meshDataBuffer->uploadData(meshes.data(), meshes.size() * sizeof(GPUMeshData), 0);
        }
    }

    void CullingSystem::cull(VkCommandBuffer commandBuffer, VkDescriptorSet globalDescriptorSet) {
        if (totalInstances == 0) {
            return;
        }

        uint32_t zero = 0;
        vkCmdUpdateBuffer(commandBuffer, drawCountBuffer->get(), 0, sizeof(uint32_t), &zero);

        VkBufferMemoryBarrier resetBarrier{};
        resetBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        resetBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        resetBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        resetBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        resetBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        resetBarrier.buffer = drawCountBuffer->get();
        resetBarrier.offset = 0;
        resetBarrier.size = VK_WHOLE_SIZE;

        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0,
            0, nullptr,
            1, &resetBarrier,
            0, nullptr
        );

        cullingPipeline->bind(commandBuffer);

        vkCmdBindDescriptorSets(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            cullingLayout,
            0, 2, std::array<VkDescriptorSet, 2>{globalDescriptorSet, cullingDescSet}.data(),
            0, nullptr
        );

        vkCmdPushConstants(
            commandBuffer,
            cullingLayout,
            VK_SHADER_STAGE_COMPUTE_BIT,
            0,
            sizeof(uint32_t),
            &totalInstances
        );

        // Workgroup size is 256 (defined in shader)
        uint32_t workgroups = (totalInstances + 255) / 256;
        vkCmdDispatch(commandBuffer, workgroups, 1, 1);

        // CRITICAL: Need barriers for draw commands, draw count, AND instance buffer
        VkBufferMemoryBarrier computeBarriers[3];

        // Barrier for draw commands
        computeBarriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        computeBarriers[0].pNext = nullptr;
        computeBarriers[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        computeBarriers[0].dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
        computeBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        computeBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        computeBarriers[0].buffer = drawCommandBuffer->get();
        computeBarriers[0].offset = 0;
        computeBarriers[0].size = VK_WHOLE_SIZE;

        // Barrier for draw count (CRITICAL FIX!)
        computeBarriers[1].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        computeBarriers[1].pNext = nullptr;
        computeBarriers[1].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        computeBarriers[1].dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
        computeBarriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        computeBarriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        computeBarriers[1].buffer = drawCountBuffer->get();
        computeBarriers[1].offset = 0;
        computeBarriers[1].size = VK_WHOLE_SIZE;

        // Barrier for instance buffer (vertex shader needs to read it)
        computeBarriers[2].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        computeBarriers[2].pNext = nullptr;
        computeBarriers[2].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;  // Compute reads it
        computeBarriers[2].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;  // Vertex shader reads it
        computeBarriers[2].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        computeBarriers[2].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        computeBarriers[2].buffer = instanceBuffer->get();
        computeBarriers[2].offset = 0;
        computeBarriers[2].size = VK_WHOLE_SIZE;

        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
            0,
            0, nullptr,
            3, computeBarriers,  // ALL THREE buffers!
            0, nullptr
            );
    }
}