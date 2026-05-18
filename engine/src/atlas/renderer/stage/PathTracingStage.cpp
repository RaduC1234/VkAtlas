#include "PathTracingStage.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include <array>

#include "core/Log.hpp"
#include "entity/Object.hpp"

namespace Atlas {
    struct PTObjectData {
        glm::mat4 modelMatrix;
        glm::mat4 normalMatrix;
        glm::uvec4 textureIndices;
        glm::vec4 baseColor;
        uint32_t firstIndex;
        uint32_t indexCount;
        uint32_t firstVertex;
        uint32_t flags;
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
        glm::vec3 rectRight;
        glm::vec3 rectUp;
    };

    struct PushConstants {
        uint32_t sampleIndex;
        uint32_t maxBounces;
        uint32_t frameIndex;
        uint32_t lightCount;
    };

    constexpr uint32_t MATERIAL_FLAG_ALPHA_MASKED = 1u << 0u;

    PathTracingStage::PathTracingStage(Device &device, AssetManager &assets, const DescriptorSetLayout &globalSetLayout)
        : IRenderStage(Queue::GRAPHICS), device(device), assets(assets), globalSetLayout(globalSetLayout) {
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

        // Scene-wide packed geometry — rebuilt each onSceneChanged.
        // Placeholder 1-element buffers so descriptors are always valid at init.
        vertexBuffer = std::make_unique<GPUBuffer>(
            GPUBuffer::simple(device)
            .setSize(sizeof(GPUMesh::Vertex))
            .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
            .setMemoryUsage(VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)
            .build()
        );

        indexBuffer = std::make_unique<GPUBuffer>(
            GPUBuffer::simple(device)
            .setSize(sizeof(uint32_t))
            .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
            .setMemoryUsage(VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)
            .build()
        );

        createDescriptors();
        createPipelineLayout();
        createPipeline();
        buildSBT();

        VkDescriptorImageInfo defaultInfo = IGPUResource::default_<GPUTexture>().descriptor();
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = ptSet;
        write.dstBinding = 6;
        write.dstArrayElement = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &defaultInfo;
        vkUpdateDescriptorSets(device.device(), 1, &write, 0, nullptr);
    }

    PathTracingStage::~PathTracingStage() {
        if (pipelineLayout != VK_NULL_HANDLE)
            vkDestroyPipelineLayout(device.device(), pipelineLayout, nullptr);
    }

    void PathTracingStage::getDeclaredOutputs(std::vector<Resource::Description> &out) const {
        out.push_back({
            .name = "post_color",
            .type = Resource::Type::SHADER_WRITE,
            .format = VK_FORMAT_R32G32B32A32_SFLOAT,
            .imageUsage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        });
    }

    void PathTracingStage::getDeclaredInputs(std::vector<std::string> &out) const {
    }

    void PathTracingStage::onResourcesCreated(const Context &ctx) {
        outputImage = &ctx.resources.at("post_color").get().asImage();

        const auto extent = outputImage->extent();
        auto accum = GPUImage::Builder(device)
                .setExtent(extent.width, extent.height)
                .setFormat(VK_FORMAT_R32G32B32A32_SFLOAT)
                .setUsage(VK_IMAGE_USAGE_STORAGE_BIT)
                .setDebugName("path_tracing_accumulation")
                .addView(VK_IMAGE_ASPECT_COLOR_BIT)
                .build();
        accumulationImage = std::make_unique<GPUImage>(std::move(accum));

        updateDescriptorSet();
    }

    void PathTracingStage::onUpdate(entt::registry &registry) {
        cameraConstructConnection = registry.on_construct<CameraComponent>().connect<&PathTracingStage::onCameraUpdated>(*this);
        cameraUpdateConnection = registry.on_update<CameraComponent>().connect<&PathTracingStage::onCameraUpdated>(*this);
        cameraDestroyConnection = registry.on_destroy<CameraComponent>().connect<&PathTracingStage::onCameraDestroyed>(*this);

        std::vector<VkAccelerationStructureInstanceKHR> tlasInstances;
        std::vector<PTObjectData> cpuObjects;
        std::vector<PTLight> cpuLights;

        std::vector<Mesh::Vertex> allVertices;
        std::vector<uint32_t> allIndices;

        tlasInstances.reserve(MAX_OBJECTS);
        cpuObjects.reserve(MAX_OBJECTS);
        cpuLights.reserve(MAX_LIGHTS);

        const auto safeNormalize = [](const glm::vec3 &v, const glm::vec3 &fallback) {
            const float len2 = glm::dot(v, v);
            return len2 > 1e-8f ? v * glm::inversesqrt(len2) : fallback;
        };

        for (auto entity: registry.view<TransformComponent, ModelComponent, MaterialComponent>()) {
            if (cpuObjects.size() >= MAX_OBJECTS) {
                AT_WARN("PathTracingStage: MAX_OBJECTS reached");
                break;
            }

            auto &transform = registry.get<TransformComponent>(entity);
            auto &model = registry.get<ModelComponent>(entity);
            auto &material = registry.get<MaterialComponent>(entity);

            if (material.transparent && !material.alphaMasked) continue;

            if (!model.meshHandle.valid() || !model.meshHandle.isReady()) continue;
            model.meshHandle.buildAccelerationStructure();
            if (model.meshHandle.blasAddress() == 0) continue;

            const uint32_t objectIndex = static_cast<uint32_t>(cpuObjects.size());
            const glm::mat4 m = transform.mat4();

            const glm::mat4 mT = glm::transpose(m);
            VkAccelerationStructureInstanceKHR instance{};
            memcpy(&instance.transform, &mT, sizeof(instance.transform));
            instance.instanceCustomIndex = objectIndex;
            instance.mask = 0xFF;
            instance.instanceShaderBindingTableRecordOffset = 0;
            instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
            instance.accelerationStructureReference = model.meshHandle.blasAddress();
            tlasInstances.push_back(instance);

            const auto firstVertex = static_cast<uint32_t>(allVertices.size());
            const auto firstIndex = static_cast<uint32_t>(allIndices.size());

            for (const auto &v: model.meshHandle->vertices()) {
                allVertices.push_back(v);
            }
            for (const auto i: model.meshHandle->indices()) {
                allIndices.push_back(i);
            }

            cpuObjects.push_back({
                .modelMatrix = m,
                .normalMatrix = glm::mat4(glm::inverseTranspose(glm::mat3(m))),
                .textureIndices = glm::uvec4(
                    registerTexture(material.albedoTexture),
                    registerTexture(material.normalMap),
                    registerTexture(material.metallicRoughnessMap),
                    registerTexture(material.ambientOcclusion)
                ),
                .baseColor = material.baseColor,
                .firstIndex = firstIndex,
                .indexCount = static_cast<uint32_t>(model.meshHandle->indices().size()),
                .firstVertex = firstVertex,
                .flags = material.alphaMasked ? MATERIAL_FLAG_ALPHA_MASKED : 0u,
            });
        }

        for (auto entity: registry.view<TransformComponent, LightComponent>()) {
            if (cpuLights.size() >= MAX_LIGHTS) {
                AT_WARN("PathTracingStage: MAX_LIGHTS reached");
                break;
            }

            auto &transform = registry.get<TransformComponent>(entity);
            auto &light = registry.get<LightComponent>(entity);

            const glm::vec3 direction = safeNormalize(light.direction, glm::vec3(0.0f, -1.0f, 0.0f));
            const glm::vec3 rectRight = safeNormalize(light.rectRight, glm::vec3(1.0f, 0.0f, 0.0f));
            const glm::vec3 rectUp = safeNormalize(light.rectUp, glm::vec3(0.0f, 1.0f, 0.0f));

            cpuLights.push_back({
                .type = static_cast<uint32_t>(light.type),
                .intensity = light.intensity,
                .range = light.range,
                .innerConeAngle = light.innerConeAngle,
                .color = glm::vec4(light.color, 1.f),
                .outerConeAngle = light.outerConeAngle,
                .position = transform.translation,
                .width = light.width,
                .direction = direction,
                .height = light.height,
                .rectRight = rectRight,
                .rectUp = rectUp,
            });
        }

        objectCount = static_cast<uint32_t>(cpuObjects.size());
        lightCount = static_cast<uint32_t>(cpuLights.size());


        if (!tlasInstances.empty()) {
            tlas_ = AccelerationStructure::buildTLAS(device, tlasInstances);
        }

        // ---- Upload packed geometry -----------------------------------------
        if (!allVertices.empty()) {
            const VkDeviceSize vSize = allVertices.size() * sizeof(Mesh::Vertex);
            GPUBuffer vStaging(device, vSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                               VMA_MEMORY_USAGE_AUTO,
                               VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
            vStaging.uploadData(allVertices.data(), vSize);

            vertexBuffer = std::make_unique<GPUBuffer>(
                GPUBuffer::simple(device)
                .setSize(vSize)
                .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
                .setMemoryUsage(VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)
                .build()
            );
            GPUBuffer::copy(device, vStaging.get(), vertexBuffer->get(), vSize, 0, 0);
        }

        if (!allIndices.empty()) {
            const VkDeviceSize iSize = allIndices.size() * sizeof(uint32_t);
            GPUBuffer iStaging(device, iSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                               VMA_MEMORY_USAGE_AUTO,
                               VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
            iStaging.uploadData(allIndices.data(), iSize);

            indexBuffer = std::make_unique<GPUBuffer>(
                GPUBuffer::simple(device)
                .setSize(iSize)
                .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
                .setMemoryUsage(VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)
                .build()
            );
            GPUBuffer::copy(device, iStaging.get(), indexBuffer->get(), iSize, 0, 0);
        }

        if (!cpuObjects.empty()) {
            // upload scene data
            objectBuffer->uploadData(cpuObjects.data(), cpuObjects.size() * sizeof(PTObjectData));
        }

        if (!cpuLights.empty()) {
            lightBuffer->uploadData(cpuLights.data(), cpuLights.size() * sizeof(PTLight));
        }

        updateDescriptorSet(); // Descriptor set needs to be updated with the new TLAS
        reset();

        AT_INFO("PathTracingStage: {} objects, {} lights", objectCount, lightCount);
    }

    void PathTracingStage::record(VkCommandBuffer cmd, VkDescriptorSet globalSet) {
        if (!outputImage || !accumulationImage) {
            return;
        }

        // Transition display + accumulation images to GENERAL every frame.
        // On sample 0 their contents are discarded; subsequent samples preserve
        // the HDR accumulation while OutputStage restores post_color to GENERAL.
        {
            const bool preserveAccumulation = currentSample > 0;

            std::array<VkImageMemoryBarrier, 2> barriers{};
            barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barriers[0].oldLayout = preserveAccumulation ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED;
            barriers[0].newLayout = VK_IMAGE_LAYOUT_GENERAL;
            barriers[0].srcAccessMask = preserveAccumulation ? VK_ACCESS_TRANSFER_READ_BIT : 0;
            barriers[0].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[0].image = outputImage->image();
            barriers[0].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

            barriers[1] = barriers[0];
            barriers[1].srcAccessMask = preserveAccumulation ? VK_ACCESS_SHADER_WRITE_BIT : 0;
            barriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            barriers[1].image = accumulationImage->image();

            vkCmdPipelineBarrier(cmd,
                                 preserveAccumulation
                                     ? (VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR)
                                     : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                 VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                                 0, 0, nullptr, 0, nullptr,
                                 static_cast<uint32_t>(barriers.size()), barriers.data());
        }

        if (!active || !tlas_.isValid()) {
            return;
        }

        pipeline->bind(cmd);

        const std::array sets = {
            globalSet,
            ptSet,
        };

        vkCmdBindDescriptorSets(cmd,
                                VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
                                pipelineLayout,
                                0,
                                sets.size(), sets.data(),
                                0, nullptr);

        const PushConstants pc{
            .sampleIndex = currentSample,
            .maxBounces = MAX_BOUNCES,
            .frameIndex = frameIndex,
            .lightCount = lightCount,
        };

        vkCmdPushConstants(cmd, pipelineLayout,
                           VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR,
                           0, sizeof(PushConstants), &pc);

        const auto extent = outputImage->extent();

        vkCmdTraceRaysKHR(cmd, &sbtRaygen, &sbtMiss, &sbtHit, &sbtCallable, extent.width, extent.height, 1);

        currentSample++;
        frameIndex++;
    }

    void PathTracingStage::reset() {
        currentSample = 0;
        active = true;
    }

    void PathTracingStage::createDescriptors() {
        // ---- PT set (set 1) — must match PathTracing.rchit exactly ----
        // binding 0 — outputImage   (storage image,       raygen)
        // binding 1 — tlas          (accel struct,        raygen + chit)
        // binding 2 — VertexBuffer  (storage buffer,      chit + ahit)
        // binding 3 — IndexBuffer   (storage buffer,      chit + ahit)
        // binding 4 — ObjectBuffer  (storage buffer,      chit + ahit)
        // binding 5 — LightBuffer   (storage buffer,      chit)
        // binding 6 — textures[]    (combined sampler[],  chit + ahit, bindless)
        // binding 7 — accumulation  (storage image,       raygen)
        constexpr VkShaderStageFlags hitStages = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR;

        ptSetLayout = DescriptorSetLayout::Builder(device)
                .addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR, 1)
                .addBinding(1, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, 1)
                .addBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, hitStages, 1)
                .addBinding(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, hitStages, 1)
                .addBinding(4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, hitStages, 1)
                .addBinding(5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, 1)
                .addBinding(6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, hitStages, MAX_TEXTURES)
                .addBinding(7, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR, 1)
                .setBindingFlags(6, VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT)
                .setLayoutFlags(VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT)
                .build();

        ptPool = DescriptorPool::Builder(device)
                .setMaxSets(1)
                .addPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2)
                .addPoolSize(VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1)
                .addPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4)
                .addPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_TEXTURES)
                .setPoolFlags(VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT)
                .build();

        if (!ptPool->allocateDescriptor(ptSetLayout->getDescriptorSetLayout(), ptSet))
            throw std::runtime_error("PathTracingStage: failed to allocate PT descriptor set");

        bindlessTextureSet = ptSet;
    }

    void PathTracingStage::createPipelineLayout() {
        const std::vector<VkDescriptorSetLayout> layouts = {
            globalSetLayout.getDescriptorSetLayout(),
            ptSetLayout->getDescriptorSetLayout(), // set 1 — all PT resources
        };

        VkPushConstantRange pcRange{};
        pcRange.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR;
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
            "shaders/PathTracing.rahit.spv",
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
        if (groupCount != 4) {
            throw std::runtime_error("PathTracingStage: expected 4 ray tracing shader groups");
        }

        const uint32_t handleSizeAligned = alignUp(handleSize, handleAlignment);

        // Regions:
        //   raygen  — 1 group
        //   miss    — 2 groups (primary + shadow)
        //   hit     — 1 group
        const uint32_t raygenSize = alignUp(handleSizeAligned, baseAlignment);
        const uint32_t missSize = alignUp(handleSizeAligned * 2, baseAlignment);
        const uint32_t hitSize = alignUp(handleSizeAligned, baseAlignment);
        const uint32_t sbtSize = raygenSize + missSize + hitSize;

        const uint32_t dataSize = groupCount * handleSize;
        std::vector<uint8_t> handles(dataSize);
        if (vkGetRayTracingShaderGroupHandlesKHR(device.device(), pipeline->pipeline(), 0, groupCount, dataSize, handles.data()) != VK_SUCCESS) {
            throw std::runtime_error("PathTracingStage: failed to get shader group handles");
        }

        GPUBuffer stagingBuffer = GPUBuffer::simple(device)
                .setSize(sbtSize)
                .setUsage(VK_BUFFER_USAGE_TRANSFER_SRC_BIT)
                .setMemoryUsage(VMA_MEMORY_USAGE_AUTO)
                .setAllocationFlags(VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT)
                .build();
        stagingBuffer.map();

        uint8_t *pData = static_cast<uint8_t *>(stagingBuffer.getMapped());
        std::memset(pData, 0, sbtSize);
        std::memcpy(pData, handles.data() + 0 * handleSize, handleSize); // raygen — group 0
        std::memcpy(pData + raygenSize, handles.data() + 1 * handleSize, handleSize); // miss — groups 1 and 2
        std::memcpy(pData + raygenSize + handleSizeAligned, handles.data() + 2 * handleSize, handleSize);
        std::memcpy(pData + raygenSize + missSize, handles.data() + 3 * handleSize, handleSize); // hit — group 3

        stagingBuffer.flush(sbtSize);
        stagingBuffer.unmap();

        sbtBuffer = std::make_unique<GPUBuffer>(GPUBuffer::simple(device)
            .setSize(sbtSize)
            .setUsage(VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR |
                      VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                      VK_BUFFER_USAGE_TRANSFER_DST_BIT)
            .setMemoryUsage(VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)
            .build()
        );

        VkCommandBuffer cmd = device.beginGraphicsCommands();
        VkBufferCopy region{};
        region.size = sbtSize;
        vkCmdCopyBuffer(cmd, stagingBuffer.get(), sbtBuffer->get(), 1, &region);
        device.endGraphicsCommands(cmd);

        VkBufferDeviceAddressInfo addrInfo{};
        addrInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        addrInfo.buffer = sbtBuffer->get();
        const VkDeviceAddress sbtAddress = vkGetBufferDeviceAddress(device.device(), &addrInfo);

        sbtRaygen = {sbtAddress, raygenSize, raygenSize};
        sbtMiss = {sbtAddress + raygenSize, handleSizeAligned, missSize};
        sbtHit = {sbtAddress + raygenSize + missSize, handleSizeAligned, hitSize};
        sbtCallable = {};
    }

    void PathTracingStage::updateDescriptorSet() {
        if (!outputImage || !accumulationImage || !tlas_.isValid()) {
            return;
        }

        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageView = outputImage->view(0);
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageInfo.sampler = VK_NULL_HANDLE;

        VkDescriptorImageInfo accumulationInfo{};
        accumulationInfo.imageView = accumulationImage->view(0);
        accumulationInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        accumulationInfo.sampler = VK_NULL_HANDLE;

        auto tlasHandle = tlas_.handle();

        VkWriteDescriptorSetAccelerationStructureKHR asInfo{};
        asInfo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
        asInfo.accelerationStructureCount = 1;
        asInfo.pAccelerationStructures = &tlasHandle;

        auto vertexInfo = vertexBuffer->descriptorInfo();
        auto indexInfo = indexBuffer->descriptorInfo();
        auto objInfo = objectBuffer->descriptorInfo();
        auto lightInfo = lightBuffer->descriptorInfo();

        DescriptorWriter(*ptSetLayout, *ptPool)
                .writeImage(0, &imageInfo)
                .writeAccelerationStructure(1, &asInfo)
                .writeBuffer(2, &vertexInfo)
                .writeBuffer(3, &indexInfo)
                .writeBuffer(4, &objInfo)
                .writeBuffer(5, &lightInfo)
                .writeImage(7, &accumulationInfo)
                .overwrite(ptSet);
    }

    void PathTracingStage::onCameraUpdated(entt::registry &registry, entt::entity entity) {
        const auto &camera = registry.get<CameraComponent>(entity).camera;
        const auto cameraData = camera.getData();

        if (!hasCameraData || cameraDataChanged(cameraData, lastCameraData)) {
            reset();
            lastCameraData = cameraData;
            hasCameraData = true;
        }
    }

    void PathTracingStage::onCameraDestroyed(entt::registry &, entt::entity) {
        hasCameraData = false;
        reset();
    }

    bool PathTracingStage::cameraDataChanged(const Camera::Data &lhs, const Camera::Data &rhs) {
        constexpr float epsilon = 0.00001f;
        for (int column = 0; column < 4; ++column) {
            for (int row = 0; row < 4; ++row) {
                if (std::abs(lhs.projection[column][row] - rhs.projection[column][row]) > epsilon ||
                    std::abs(lhs.view[column][row] - rhs.view[column][row]) > epsilon) {
                    return true;
                }
            }
        }

        return false;
    }

    uint32_t PathTracingStage::registerTexture(AssetHandle<Texture> handle) {
        if (!handle.valid()) return 0;

        auto [it, inserted] = handleToSlot.emplace(handle, nextTextureSlot);
        if (!inserted) {
            handle.registerBindlessSlot(device.device(), ptSet, 6, it->second);
            return it->second;
        }

        if (nextTextureSlot >= MAX_TEXTURES) {
            AT_WARN("PathTracingStage: MAX_TEXTURES exceeded");
            return 0;
        }

        const uint32_t slot = nextTextureSlot++;
        it->second = slot;
        handle.registerBindlessSlot(device.device(), ptSet, 6, slot);

        VkDescriptorImageInfo info = handle.descriptor();
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = ptSet;
        write.dstBinding = 6; // textures[] at binding 6 in set 1
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
