#include "PathTracingStage.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include "core/Log.hpp"
#include "entity/Object.hpp"

namespace Atlas {
    // =========================================================================
    // GPU-side structs — must match shader exactly
    // =========================================================================

    struct PTObjectData {
        glm::mat4 modelMatrix;
        glm::mat4 normalMatrix;
        glm::uvec4 textureIndices;
        glm::vec4 baseColor;
        uint32_t firstIndex;
        uint32_t indexCount;
        uint32_t firstVertex;
        uint32_t pad;
    };

    struct PTLight {
        uint32_t type;
        float intensity;
        float range;
        float innerConeAngle;
        glm::vec4 color;
        float outerConeAngle;
        glm::vec3 position;
        float width;
        glm::vec3 direction;
        float height;
    };

    struct PushConstants {
        uint32_t sampleIndex;
        uint32_t maxBounces;
        uint32_t frameIndex;
        uint32_t pad;
    };

    // =========================================================================
    // Construction / destruction
    // =========================================================================

    PathTracingStage::PathTracingStage(Device &device, const DescriptorSetLayout &globalSetLayout) : IRenderStage(Queue::GRAPHICS), device(device), globalSetLayout(globalSetLayout) {
        defaultWhiteHandle = AssetManager::get().createDefaultWhiteTexture();

        objectBuffer = std::make_unique<GPUBuffer>(
            GPUBuffer::simple(device)
            .setSize(sizeof(PTObjectData) * MAX_OBJECTS)
            .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
            .setMemoryUsage(VMA_MEMORY_USAGE_AUTO)
            .setAllocationFlags(VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT)
            .build()
        );

        lightBuffer = std::make_unique<GPUBuffer>(
            GPUBuffer::simple(device)
            .setSize(sizeof(PTLight) * MAX_LIGHTS)
            .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
            .setMemoryUsage(VMA_MEMORY_USAGE_AUTO)
            .setAllocationFlags(VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT)
            .build()
        );

        createDescriptors();
        createPipelineLayout();
        createPipeline();
        buildSBT();

        registerTexture(defaultWhiteHandle);
    }

    PathTracingStage::~PathTracingStage() {
        if (pipelineLayout != VK_NULL_HANDLE)
            vkDestroyPipelineLayout(device.device(), pipelineLayout, nullptr);
    }

    // =========================================================================
    // IRenderStage
    // =========================================================================

    void PathTracingStage::getDeclaredOutputs(std::vector<Resource::Description> &out) const {
        out.push_back({
            .name = "post_color",
            .type = Resource::Type::SHADER_WRITE,
            .format = VK_FORMAT_R32G32B32A32_SFLOAT,
            .imageUsage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        });
    }

    void PathTracingStage::getDeclaredInputs(std::vector<std::string> &out) const {
        // No graph inputs — all data comes from ECS + owned GPU buffers
    }

    void PathTracingStage::onResourcesCreated(const Context &ctx) {
        outputImage = &ctx.resources.at("post_color").get().asImage();
        updateDescriptorSet();
    }

    // =========================================================================
    // onSceneChanged — build TLAS + upload scene data
    // =========================================================================

    void PathTracingStage::onSceneChanged(entt::registry &registry) {
        std::vector<VkAccelerationStructureInstanceKHR> tlasInstances;
        std::vector<PTObjectData> cpuObjects;
        std::vector<PTLight> cpuLights;

        tlasInstances.reserve(MAX_OBJECTS);
        cpuObjects.reserve(MAX_OBJECTS);
        cpuLights.reserve(MAX_LIGHTS);

        // ---- Meshes --------------------------------------------------------
        for (auto entity: registry.view<TransformComponent, ModelComponent, MaterialComponent>()) {
            if (cpuObjects.size() >= MAX_OBJECTS) {
                AT_WARN("PathTracingStage: MAX_OBJECTS reached");
                break;
            }

            auto &transform = registry.get<TransformComponent>(entity);
            auto &model = registry.get<ModelComponent>(entity);
            auto &material = registry.get<MaterialComponent>(entity);

            if (model.meshHandle == INVALID_ASSET_HANDLE) continue;
            const auto mesh = AssetManager::get().getMesh(model.meshHandle);
            if (!mesh || !mesh->accelerationStructure().isValid()) continue;

            const uint32_t objectIndex = static_cast<uint32_t>(cpuObjects.size());
            const glm::mat4 m = transform.mat4();

            // TLAS instance — row-major 3x4 transform
            const glm::mat4 mT = glm::transpose(m);
            VkAccelerationStructureInstanceKHR instance{};
            memcpy(&instance.transform, &mT, sizeof(instance.transform));
            instance.instanceCustomIndex = objectIndex;
            instance.mask = 0xFF;
            instance.instanceShaderBindingTableRecordOffset = 0;
            instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
            instance.accelerationStructureReference = mesh->accelerationStructure().deviceAddress();
            tlasInstances.push_back(instance);

            // Object data for hit shader
            cpuObjects.push_back({
                .modelMatrix = m,
                .normalMatrix = glm::mat4(glm::inverseTranspose(glm::mat3(m))),
                .textureIndices = glm::uvec4(
                    registerTexture(material.albedoTexture != INVALID_ASSET_HANDLE ? material.albedoTexture : defaultWhiteHandle),
                    registerTexture(material.normalMap != INVALID_ASSET_HANDLE ? material.normalMap : defaultWhiteHandle),
                    registerTexture(material.metallicRoughnessMap != INVALID_ASSET_HANDLE ? material.metallicRoughnessMap : defaultWhiteHandle),
                    registerTexture(material.ambientOcclusion != INVALID_ASSET_HANDLE ? material.ambientOcclusion : defaultWhiteHandle)
                ),
                .baseColor = material.baseColor,
                .firstIndex = 0, // mesh owns its own index buffer — hit shader uses gl_PrimitiveID directly
                .indexCount = static_cast<uint32_t>(mesh->getIndices().size()),
                .firstVertex = 0,
                .pad = 0,
            });
        }

        // ---- Lights --------------------------------------------------------
        for (auto entity: registry.view<TransformComponent, LightComponent>()) {
            if (cpuLights.size() >= MAX_LIGHTS) break;

            auto &transform = registry.get<TransformComponent>(entity);
            auto &light = registry.get<LightComponent>(entity);

            cpuLights.push_back({
                .type = static_cast<uint32_t>(light.type),
                .intensity = light.intensity,
                .range = light.range == 0.f ? 20.f : light.range,
                .innerConeAngle = light.innerConeAngle,
                .color = glm::vec4(light.color, 1.f),
                .outerConeAngle = light.outerConeAngle,
                .position = transform.translation,
                .width = light.width,
                .direction = light.direction,
                .height = light.height,
            });
        }

        objectCount = static_cast<uint32_t>(cpuObjects.size());
        lightCount = static_cast<uint32_t>(cpuLights.size());

        // ---- Build TLAS --------------------------------------------------------
        if (!tlasInstances.empty())
            tlas_ = AccelerationStructure::buildTLAS(device, tlasInstances);

        // ---- Upload scene data ------------------------------------------------
        if (!cpuObjects.empty())
            objectBuffer->uploadData(cpuObjects.data(), cpuObjects.size() * sizeof(PTObjectData));

        if (!cpuLights.empty())
            lightBuffer->uploadData(cpuLights.data(), cpuLights.size() * sizeof(PTLight));

        // Descriptor set needs to be updated with the new TLAS
        updateDescriptorSet();

        // Reset accumulation on scene change
        reset();

        AT_INFO("PathTracingStage: {} objects, {} lights", objectCount, lightCount);
    }

    // =========================================================================
    // record
    // =========================================================================

    void PathTracingStage::record(VkCommandBuffer cmd, VkDescriptorSet globalSet) {
        if (!active || !tlas_.isValid() || !outputImage) return;

        pipeline->bind(cmd);

        const std::array<VkDescriptorSet, 3> sets = {
            globalSet,
            ptSet,
            bindlessTextureSet,
        };

        vkCmdBindDescriptorSets(cmd,
                                VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
                                pipelineLayout,
                                0,
                                static_cast<uint32_t>(sets.size()), sets.data(),
                                0, nullptr);

        const PushConstants pc{
            .sampleIndex = currentSample,
            .maxBounces = MAX_BOUNCES,
            .frameIndex = frameIndex,
            .pad = 0,
        };

        vkCmdPushConstants(cmd, pipelineLayout,
                           VK_SHADER_STAGE_RAYGEN_BIT_KHR
                           | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR
                           | VK_SHADER_STAGE_MISS_BIT_KHR,
                           0, sizeof(PushConstants), &pc);

        const auto extent = outputImage->extent();

        vkCmdTraceRaysKHR(cmd,
                          &sbtRaygen,
                          &sbtMiss,
                          &sbtHit,
                          &sbtCallable,
                          extent.width, extent.height, 1);

        currentSample++;
        frameIndex++;
    }

    // =========================================================================
    // reset
    // =========================================================================

    void PathTracingStage::reset() {
        currentSample = 0;
        active = true;
    }

    // =========================================================================
    // Private — setup
    // =========================================================================

    void PathTracingStage::createDescriptors() {
        // ---- Bindless texture set (set 2) ----
        textureSetLayout = DescriptorSetLayout::Builder(device)
                .addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                            VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, MAX_TEXTURES)
                .setBindingFlags(0, VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT
                                    | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT)
                .setLayoutFlags(VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT)
                .build();

        texturePool = DescriptorPool::Builder(device)
                .setMaxSets(1)
                .addPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_TEXTURES)
                .setPoolFlags(VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT)
                .build();

        if (!texturePool->allocateDescriptor(
            textureSetLayout->getDescriptorSetLayout(), bindlessTextureSet))
            throw std::runtime_error("PathTracingStage: failed to allocate bindless texture set");

        // ---- PT set (set 1) ----
        // binding 0 — output image      (storage image)
        // binding 1 — TLAS              (acceleration structure)
        // binding 2 — object buffer     (storage buffer)
        // binding 3 — light buffer      (storage buffer)
        ptSetLayout = DescriptorSetLayout::Builder(device)
                .addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR, 1)
                .addBinding(1, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, 1)
                .addBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, 1)
                .addBinding(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, 1)
                .build();

        ptPool = DescriptorPool::Builder(device)
                .setMaxSets(1)
                .addPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1)
                .addPoolSize(VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1)
                .addPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2)
                .build();

        if (!ptPool->allocateDescriptor(ptSetLayout->getDescriptorSetLayout(), ptSet))
            throw std::runtime_error("PathTracingStage: failed to allocate PT descriptor set");
    }

    void PathTracingStage::createPipelineLayout() {
        const std::vector<VkDescriptorSetLayout> layouts = {
            globalSetLayout.getDescriptorSetLayout(), // set 0 — global (camera)
            ptSetLayout->getDescriptorSetLayout(), // set 1 — PT resources + TLAS
            textureSetLayout->getDescriptorSetLayout(), // set 2 — bindless textures
        };

        VkPushConstantRange pcRange{};
        pcRange.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR
                             | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR
                             | VK_SHADER_STAGE_MISS_BIT_KHR;
        pcRange.offset = 0;
        pcRange.size = sizeof(PushConstants);

        VkPipelineLayoutCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        info.setLayoutCount = static_cast<uint32_t>(layouts.size());
        info.pSetLayouts = layouts.data();
        info.pushConstantRangeCount = 1;
        info.pPushConstantRanges = &pcRange;

        if (vkCreatePipelineLayout(device.device(), &info, nullptr, &pipelineLayout) != VK_SUCCESS)
            throw std::runtime_error("PathTracingStage: failed to create pipeline layout");
    }

    void PathTracingStage::createPipeline() {
        RayTracingPipelineConfigInfo configInfo{};
        configInfo.pipelineLayout = pipelineLayout;
        configInfo.maxRecursionDepth = 2;

        pipeline = std::make_unique<Pipeline>(
            device,
            "shaders/PathTracing.rgen.spv",
            "shaders/PathTracing.rmiss.spv",
            "shaders/PathTracing.rchit.spv",
            "", // no any-hit
            "shaders/PathTracingShadow.rmiss.spv",
            configInfo
        );
    }

    void PathTracingStage::buildSBT() {
        const auto &rtProps = device.rayTracingPipelineProperties();
        const uint32_t handleSize = rtProps.shaderGroupHandleSize;
        const uint32_t handleAlignment = rtProps.shaderGroupHandleAlignment;
        const uint32_t baseAlignment = rtProps.shaderGroupBaseAlignment;
        const uint32_t groupCount = pipeline->shaderGroupCount();

        // Each handle is padded to handleAlignment, each region is padded to baseAlignment
        const uint32_t handleSizeAligned = alignUp(handleSize, handleAlignment);

        // Regions:
        //   raygen  — 1 group
        //   miss    — 2 groups (primary + shadow)
        //   hit     — 1 group
        const uint32_t raygenSize = alignUp(handleSizeAligned, baseAlignment);
        const uint32_t missSize = alignUp(handleSizeAligned * 2, baseAlignment);
        const uint32_t hitSize = alignUp(handleSizeAligned, baseAlignment);
        const uint32_t sbtSize = raygenSize + missSize + hitSize;

        // Fetch raw handles from pipeline
        const uint32_t dataSize = groupCount * handleSize;
        std::vector<uint8_t> handles(dataSize);
        if (vkGetRayTracingShaderGroupHandlesKHR(device.device(), pipeline->pipeline(), 0, groupCount, dataSize, handles.data()) != VK_SUCCESS) {
            throw std::runtime_error("PathTracingStage: failed to get shader group handles");
        }

        // Upload to SBT buffer
        sbtBuffer = std::make_unique<GPUBuffer>(
            GPUBuffer::simple(device)
            .setSize(sbtSize)
            .setUsage(VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
            .setMemoryUsage(VMA_MEMORY_USAGE_AUTO)
            .setAllocationFlags(VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT)
            .build()
        );

        // Write handles into mapped buffer respecting alignment
        uint8_t *pData = static_cast<uint8_t *>(sbtBuffer->getMapped());
        // raygen — group 0
        memcpy(pData, handles.data() + 0 * handleSize, handleSize);
        // miss — groups 1 and 2
        memcpy(pData + raygenSize, handles.data() + 1 * handleSize, handleSize);
        memcpy(pData + raygenSize + handleSizeAligned, handles.data() + 2 * handleSize, handleSize);
        // hit — group 3
        memcpy(pData + raygenSize + missSize, handles.data() + 3 * handleSize, handleSize);

        // Build device address regions
        VkBufferDeviceAddressInfo addrInfo{};
        addrInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        addrInfo.buffer = sbtBuffer->get();
        const VkDeviceAddress sbtAddress = vkGetBufferDeviceAddress(device.device(), &addrInfo);

        sbtRaygen = {sbtAddress, raygenSize, handleSizeAligned};
        sbtMiss = {sbtAddress + raygenSize, missSize, handleSizeAligned};
        sbtHit = {sbtAddress + raygenSize + missSize, hitSize, handleSizeAligned};
        sbtCallable = {};
    }

    void PathTracingStage::updateDescriptorSet() {
        if (!outputImage || !tlas_.isValid()) return;

        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageView = outputImage->view(0);
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageInfo.sampler = VK_NULL_HANDLE;

        auto tlasHandle = tlas_.handle();

        VkWriteDescriptorSetAccelerationStructureKHR asInfo{};
        asInfo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
        asInfo.accelerationStructureCount = 1;
        asInfo.pAccelerationStructures = &tlasHandle;

        auto objInfo = objectBuffer->descriptorInfo();
        auto lightInfo = lightBuffer->descriptorInfo();

        DescriptorWriter(*ptSetLayout, *ptPool)
                .writeImage(0, &imageInfo)
                .writeAccelerationStructure(1, &asInfo)
                .writeBuffer(2, &objInfo)
                .writeBuffer(3, &lightInfo)
                .overwrite(ptSet);
    }

    uint32_t PathTracingStage::registerTexture(AssetHandle handle) {
        if (handle == INVALID_ASSET_HANDLE) return 0;

        auto [it, inserted] = handleToSlot.emplace(handle, nextTextureSlot);
        if (!inserted) return it->second;

        if (nextTextureSlot >= MAX_TEXTURES) {
            AT_WARN("PathTracingStage: MAX_TEXTURES exceeded");
            return 0;
        }

        const auto texture = AssetManager::get().getTexture(handle);
        if (!texture) return 0;

        const uint32_t slot = nextTextureSlot++;
        it->second = slot;

        VkDescriptorImageInfo info{
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
        write.pImageInfo = &info;
        vkUpdateDescriptorSets(device.device(), 1, &write, 0, nullptr);

        return slot;
    }

    uint32_t PathTracingStage::alignUp(uint32_t size, uint32_t alignment) const {
        return (size + alignment - 1) & ~(alignment - 1);
    }
} // namespace Atlas
