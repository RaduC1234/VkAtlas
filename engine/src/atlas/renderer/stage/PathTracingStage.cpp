#include "PathTracingStage.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include <array>
#include <cstdint>
#include <cstring>
#include <functional>

#include "core/Log.hpp"
#include "core/Profiler.hpp"
#include "entity/Object.hpp"

namespace Atlas {
    struct PTObjectData {
        glm::mat4 modelMatrix;
        glm::mat4 normalMatrix;
        glm::uvec4 textureIndices;
        glm::vec4 baseColor;
        glm::vec4 materialFactors;
        glm::vec4 sheenColorStrength;
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
    constexpr uint32_t MATERIAL_FLAG_CLOTH_CHARLIE = 1u << 1u;

    PathTracingStage::PathTracingStage(Device &device, AssetManager &assets, const DescriptorSetLayout &globalSetLayout)
        : RenderStage(Queue::GRAPHICS), device(device), assets(assets), globalSetLayout(globalSetLayout) {
        objectBuffer = std::make_unique<GPUBuffer>(GPUBuffer::simple(device).setSize(sizeof(PTObjectData) * MAX_OBJECTS).setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT).setMemoryUsage(VMA_MEMORY_USAGE_AUTO).setAllocationFlags(VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT).build());
        lightBuffer = std::make_unique<GPUBuffer>(GPUBuffer::simple(device).setSize(sizeof(PTLight) * MAX_LIGHTS).setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT).setMemoryUsage(VMA_MEMORY_USAGE_AUTO).setAllocationFlags(VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT).build());
        vertexBuffer = std::make_unique<GPUBuffer>(GPUBuffer::simple(device).setSize(sizeof(Mesh::Vertex)).setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT).setMemoryUsage(VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE).build());
        indexBuffer = std::make_unique<GPUBuffer>(GPUBuffer::simple(device).setSize(sizeof(uint32_t)).setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT).setMemoryUsage(VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE).build());

        createDescriptors();
        createPipelineLayout();
        createPipeline();
        buildSBT();

        VkDescriptorImageInfo defaultInfo = IGPUResource::default_<GPUTexture>().descriptor();
        VkWriteDescriptorSet write{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = ptSet, .dstBinding = 6, .dstArrayElement = 0, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &defaultInfo};
        vkUpdateDescriptorSets(device.device(), 1, &write, 0, nullptr);
    }

    PathTracingStage::~PathTracingStage() {
        vkDestroySampler(device.device(), envSampler, nullptr);
        if (pipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device.device(), pipelineLayout, nullptr);
    }

    void PathTracingStage::getDeclaredOutputs(std::vector<Resource::Description> &out) const {
        out.push_back({.name = "geometry_color", .type = Resource::Type::SHADER_WRITE, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .imageUsage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT});
        out.push_back({.name = "geometry_depth", .type = Resource::Type::ATTACHMENT_DEPTH, .format = VK_FORMAT_D24_UNORM_S8_UINT, .imageUsage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT});
    }

    void PathTracingStage::getDeclaredInputs(std::vector<std::string> &out) const {
    }

    void PathTracingStage::onResourcesCreated(const Context &ctx) {
        outputImage = &ctx.resources.at("geometry_color").get().asImage();
        geometryDepth = &ctx.resources.at("geometry_depth").get().asImage();

        const auto extent = outputImage->extent();
        auto accum = GPUImage::Builder(device).setExtent(extent.width, extent.height).setFormat(VK_FORMAT_R32G32B32A32_SFLOAT).setUsage(VK_IMAGE_USAGE_STORAGE_BIT).setDebugName("path_tracing_accumulation").addView(VK_IMAGE_ASPECT_COLOR_BIT).build();
        accumulationImage = std::make_unique<GPUImage>(std::move(accum));
        updateDescriptorSet();
    }

    void PathTracingStage::onUpdate(entt::registry &registry) {
        ATLAS_PROFILE_SCOPE("PathTracingStage::onUpdate");
        cameraConstructConnection = registry.on_construct<CameraComponent>().connect<&PathTracingStage::onCameraUpdated>(*this);
        cameraUpdateConnection = registry.on_update<CameraComponent>().connect<&PathTracingStage::onCameraUpdated>(*this);
        cameraDestroyConnection = registry.on_destroy<CameraComponent>().connect<&PathTracingStage::onCameraDestroyed>(*this);

        auto skyboxView = registry.view<SkyboxComponent>();
        entt::entity activeSkybox = entt::null;
        uint32_t activeSkyboxCount = 0;
        for (const entt::entity entity: skyboxView) {
            if (const auto *node = registry.try_get<SceneNodeComponent>(entity); node && (node->deleted || !node->visible)) continue;
            if (activeSkybox == entt::null) activeSkybox = entity;
            ++activeSkyboxCount;
        }

        if (activeSkybox == entt::null) {
            if (envHandle.valid() || envReady) {
                envHandle = {};
                envReady = false;
                updateDescriptorSet();
                reset();
            }
        } else {
            if (activeSkyboxCount > 1) AT_WARN("PathTracingStage: multiple skyboxes detected, using the first one");
            const auto &skybox = registry.get<SkyboxComponent>(activeSkybox);
            const bool skyboxReady = skybox.skyboxHandle.valid() && skybox.skyboxHandle.isReady();
            if (skybox.skyboxHandle != envHandle || skyboxReady != envReady) {
                envHandle = skybox.skyboxHandle;
                envReady = skyboxReady;
                updateDescriptorSet();
                reset();
            }
        }

        bool waitingForMeshes = false;
        const uint64_t geoSig = geometrySignature(registry, waitingForMeshes);
        const uint64_t xfSig = transformSignature(registry);
        const uint64_t texSig = textureReadinessSignature();

        if (waitingForMeshes) {
            if (geometryBuilt && texSig != lastTextureReadinessSignature) {
                lastTextureReadinessSignature = texSig;
                reset();
            }
            return;
        }

        const bool geometryChanged = !geometryBuilt || geoSig != lastGeometrySignature;
        const bool transformChanged = geometryBuilt && !geometryChanged && xfSig != lastTransformSignature;
        const bool textureChanged = geometryBuilt && !geometryChanged && !transformChanged && texSig != lastTextureReadinessSignature;

        if (!geometryChanged && !transformChanged && !textureChanged) return;

        const auto safeNormalize = [](const glm::vec3 &v, const glm::vec3 &fallback) {
            const float len2 = glm::dot(v, v);
            return len2 > 1e-8f ? v * glm::inversesqrt(len2) : fallback;
        };

        std::vector<VkAccelerationStructureInstanceKHR> tlasInstances;
        std::vector<PTObjectData> cpuObjects;
        std::vector<PTLight> cpuLights;
        tlasInstances.reserve(MAX_OBJECTS);
        cpuObjects.reserve(MAX_OBJECTS);
        cpuLights.reserve(MAX_LIGHTS);

        std::vector<Mesh::Vertex> allVertices;
        std::vector<uint32_t> allIndices;

        for (auto entity: registry.view<TransformComponent, ModelComponent, MaterialComponent>()) {
            if (const auto *node = registry.try_get<SceneNodeComponent>(entity); node && (node->deleted || !node->visible)) continue;
            if (cpuObjects.size() >= MAX_OBJECTS) {
                AT_WARN("PathTracingStage: MAX_OBJECTS reached");
                break;
            }

            auto &transform = registry.get<TransformComponent>(entity);
            auto &model = registry.get<ModelComponent>(entity);
            auto &materialComponent = registry.get<MaterialComponent>(entity);

            const Material fallbackMaterial{};
            const Material *material = materialComponent.materialHandle.get();
            if (!material) material = &fallbackMaterial;

            if (material->alphaMode == AlphaMode::BLEND) continue;
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

            if (geometryChanged) {
                const auto firstVertex = static_cast<uint32_t>(allVertices.size());
                const auto firstIndex = static_cast<uint32_t>(allIndices.size());
                for (const auto &v: model.meshHandle->vertices()) allVertices.push_back(v);
                for (const auto i: model.meshHandle->indices()) allIndices.push_back(i);

                uint32_t flags = 0u;
                if (material->alphaMode == AlphaMode::MASK) flags |= MATERIAL_FLAG_ALPHA_MASKED;
                if (material->shadingModel == ShadingModel::CLOTH_CHARLIE) flags |= MATERIAL_FLAG_CLOTH_CHARLIE;

                cpuObjects.push_back({
                    .modelMatrix = m,
                    .normalMatrix = glm::mat4(glm::inverseTranspose(glm::mat3(m))),
                    .textureIndices = glm::uvec4(registerTexture(material->baseColorTexture), registerTexture(material->normalTexture), registerTexture(material->metallicRoughnessTexture), registerTexture(material->occlusionTexture)),
                    .baseColor = material->baseColor,
                    .materialFactors = glm::vec4(material->metallic, material->roughness, material->alphaCutoff, 0.0f),
                    .sheenColorStrength = glm::vec4(material->sheenColor, material->sheenStrength),
                    .firstIndex = firstIndex,
                    .indexCount = static_cast<uint32_t>(model.meshHandle->indices().size()),
                    .firstVertex = firstVertex,
                    .flags = flags,
                });
            } else {
                cpuObjects.push_back({
                    .modelMatrix = m,
                    .normalMatrix = glm::mat4(glm::inverseTranspose(glm::mat3(m))),
                    .textureIndices = glm::uvec4(0),
                    .baseColor = {},
                    .materialFactors = {},
                    .sheenColorStrength = {},
                    .firstIndex = 0,
                    .indexCount = 0,
                    .firstVertex = 0,
                    .flags = 0,
                });
            }
        }

        for (auto entity: registry.view<TransformComponent, LightComponent>()) {
            if (const auto *node = registry.try_get<SceneNodeComponent>(entity); node && (node->deleted || !node->visible)) continue;
            if (cpuLights.size() >= MAX_LIGHTS) {
                AT_WARN("PathTracingStage: MAX_LIGHTS reached");
                break;
            }

            auto &transform = registry.get<TransformComponent>(entity);
            auto &light = registry.get<LightComponent>(entity);

            cpuLights.push_back({
                .type = static_cast<uint32_t>(light.type),
                .intensity = light.intensity,
                .range = light.range,
                .innerConeAngle = light.innerConeAngle,
                .color = glm::vec4(light.color, 1.f),
                .outerConeAngle = light.outerConeAngle,
                .position = transform.translation,
                .width = light.width,
                .direction = safeNormalize(light.direction, glm::vec3(0.0f, -1.0f, 0.0f)),
                .height = light.height,
                .rectRight = safeNormalize(light.rectRight, glm::vec3(1.0f, 0.0f, 0.0f)),
                .rectUp = safeNormalize(light.rectUp, glm::vec3(0.0f, 1.0f, 0.0f)),
            });
        }

        objectCount = static_cast<uint32_t>(cpuObjects.size());
        lightCount = static_cast<uint32_t>(cpuLights.size());

        if (geometryChanged) {
            ATLAS_PROFILE_SCOPE("PathTracingStage::fullRebuild");

            if (!tlasInstances.empty()) tlas_ = AccelerationStructure::buildTLAS(device, tlasInstances);
            else tlas_ = AccelerationStructure{};

            if (!allVertices.empty()) {
                const VkDeviceSize vSize = allVertices.size() * sizeof(Mesh::Vertex);
                GPUBuffer vStaging(device, vSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_AUTO, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
                vStaging.uploadData(allVertices.data(), vSize);
                vertexBuffer = std::make_unique<GPUBuffer>(GPUBuffer::simple(device).setSize(vSize).setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT).setMemoryUsage(VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE).build());
                GPUBuffer::copy(device, vStaging.get(), vertexBuffer->get(), vSize, 0, 0);
            }

            if (!allIndices.empty()) {
                const VkDeviceSize iSize = allIndices.size() * sizeof(uint32_t);
                GPUBuffer iStaging(device, iSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_AUTO, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
                iStaging.uploadData(allIndices.data(), iSize);
                indexBuffer = std::make_unique<GPUBuffer>(GPUBuffer::simple(device).setSize(iSize).setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT).setMemoryUsage(VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE).build());
                GPUBuffer::copy(device, iStaging.get(), indexBuffer->get(), iSize, 0, 0);
            }

            if (!cpuObjects.empty()) objectBuffer->uploadData(cpuObjects.data(), cpuObjects.size() * sizeof(PTObjectData));
            if (!cpuLights.empty()) lightBuffer->uploadData(cpuLights.data(), cpuLights.size() * sizeof(PTLight));

            updateDescriptorSet();
            geometryBuilt = true;
            lastGeometrySignature = geoSig;
            lastTransformSignature = xfSig;
            lastTextureReadinessSignature = texSig;
            AT_INFO("PathTracingStage: full rebuild — {} objects, {} lights", objectCount, lightCount);
        } else if (transformChanged) {
            ATLAS_PROFILE_SCOPE("PathTracingStage::transformUpdate");

            if (tlas_.isValid() && !tlasInstances.empty()) AccelerationStructure::updateTLAS(device, tlasInstances, tlas_);

            if (!cpuObjects.empty()) objectBuffer->uploadData(cpuObjects.data(), cpuObjects.size() * sizeof(PTObjectData));
            if (!cpuLights.empty()) lightBuffer->uploadData(cpuLights.data(), cpuLights.size() * sizeof(PTLight));

            lastTransformSignature = xfSig;
        } else {
            lastTextureReadinessSignature = texSig;
        }

        reset();
        sceneBuilt = true;
    }

    void PathTracingStage::record(VkCommandBuffer cmd, VkDescriptorSet globalSet) {
        ATLAS_PROFILE_SCOPE("PathTracingStage::record");
        ATLAS_PROFILE_GPU_ZONE(device.gpuProfilerContext(), cmd, "PathTracingStage");
        if (!outputImage || !accumulationImage) return; {
            ATLAS_PROFILE_GPU_ZONE(device.gpuProfilerContext(), cmd, "PathTracingStage::PrepareImages");
            const bool preserveAccumulation = currentSample > 0;

            std::array<VkImageMemoryBarrier, 2> barriers{};
            barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barriers[0].oldLayout = preserveAccumulation ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED;
            barriers[0].newLayout = VK_IMAGE_LAYOUT_GENERAL;
            barriers[0].srcAccessMask = preserveAccumulation ? VK_ACCESS_SHADER_READ_BIT : 0;
            barriers[0].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[0].image = outputImage->image();
            barriers[0].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

            barriers[1] = barriers[0];
            barriers[1].oldLayout = preserveAccumulation ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED;
            barriers[1].srcAccessMask = preserveAccumulation ? VK_ACCESS_SHADER_WRITE_BIT : 0;
            barriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            barriers[1].image = accumulationImage->image();

            vkCmdPipelineBarrier(cmd, preserveAccumulation ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, 0, 0, nullptr, 0, nullptr, static_cast<uint32_t>(barriers.size()), barriers.data());
        }

        if (!active || !tlas_.isValid()) return; {
            ATLAS_PROFILE_GPU_ZONE(device.gpuProfilerContext(), cmd, "PathTracingStage::TraceRays");
            pipeline->bind(cmd);

            const std::array sets = {globalSet, ptSet};
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipelineLayout, 0, sets.size(), sets.data(), 0, nullptr);

            const PushConstants pc{.sampleIndex = currentSample, .maxBounces = MAX_BOUNCES, .frameIndex = frameIndex, .lightCount = lightCount};
            vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR, 0, sizeof(PushConstants), &pc);

            const auto extent = outputImage->extent();
            vkCmdTraceRaysKHR(cmd, &sbtRaygen, &sbtMiss, &sbtHit, &sbtCallable, extent.width, extent.height, 1);
        } {
            ATLAS_PROFILE_GPU_ZONE(device.gpuProfilerContext(), cmd, "PathTracingStage::ClearDepth");
            VkImageMemoryBarrier pre[2]{};

            pre[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            pre[0].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            pre[0].newLayout = VK_IMAGE_LAYOUT_GENERAL;
            pre[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            pre[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            pre[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            pre[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            pre[0].image = outputImage->image();
            pre[0].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

            pre[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            pre[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            pre[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            pre[1].srcAccessMask = 0;
            pre[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            pre[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            pre[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            pre[1].image = geometryDepth->image();
            pre[1].subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 0, 1, 0, 1};

            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR | VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 2, pre);

            VkClearDepthStencilValue clearDS{1.0f, 0};
            VkImageSubresourceRange dsRange{VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 0, 1, 0, 1};
            vkCmdClearDepthStencilImage(cmd, geometryDepth->image(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearDS, 1, &dsRange);

            VkImageMemoryBarrier post{};
            post.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            post.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            post.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            post.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            post.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            post.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            post.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            post.image = geometryDepth->image();
            post.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 0, 1, 0, 1};

            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &post);
        }

        currentSample++;
        frameIndex++;
    }

    void PathTracingStage::reset() {
        currentSample = 0;
        active = true;
    }

    void PathTracingStage::createDescriptors() {
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
                .addBinding(9, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_MISS_BIT_KHR, 1)
                .setBindingFlags(6, VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT)
                .setLayoutFlags(VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT)
                .build();

        ptPool = DescriptorPool::Builder(device)
                .setMaxSets(1)
                .addPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2)
                .addPoolSize(VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1)
                .addPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4)
                .addPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_TEXTURES + 1)
                .setPoolFlags(VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT)
                .build();

        if (!ptPool->allocateDescriptor(ptSetLayout->getDescriptorSetLayout(), ptSet)) throw std::runtime_error("PathTracingStage: failed to allocate PT descriptor set");

        bindlessTextureSet = ptSet;

        VkSamplerCreateInfo si{.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, .magFilter = VK_FILTER_LINEAR, .minFilter = VK_FILTER_LINEAR, .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR, .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, .maxLod = VK_LOD_CLAMP_NONE};
        if (vkCreateSampler(device.device(), &si, nullptr, &envSampler) != VK_SUCCESS) throw std::runtime_error("PathTracingStage: failed to create envSampler");
    }

    void PathTracingStage::createPipelineLayout() {
        const std::vector<VkDescriptorSetLayout> layouts = {globalSetLayout.getDescriptorSetLayout(), ptSetLayout->getDescriptorSetLayout()};
        VkPushConstantRange pcRange{.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR, .offset = 0, .size = sizeof(PushConstants)};
        VkPipelineLayoutCreateInfo info{.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = static_cast<uint32_t>(layouts.size()), .pSetLayouts = layouts.data(), .pushConstantRangeCount = 1, .pPushConstantRanges = &pcRange};
        if (vkCreatePipelineLayout(device.device(), &info, nullptr, &pipelineLayout) != VK_SUCCESS) throw std::runtime_error("PathTracingStage: failed to create pipeline layout");
    }

    void PathTracingStage::createPipeline() {
        RayTracingPipelineConfigInfo configInfo{};
        configInfo.pipelineLayout = pipelineLayout;
        configInfo.maxRecursionDepth = 2;
        pipeline = std::make_unique<Pipeline>(device, "##engine/shaders/PathTracing.rgen.spv", "##engine/shaders/PathTracing.rmiss.spv", "##engine/shaders/PathTracing.rchit.spv", "##engine/shaders/PathTracing.rahit.spv", "##engine/shaders/PathTracingShadow.rmiss.spv", configInfo);
    }

    void PathTracingStage::buildSBT() {
        const auto &rtProps = device.rayTracingPipelineProperties();
        const uint32_t handleSize = rtProps.shaderGroupHandleSize;
        const uint32_t handleAlignment = rtProps.shaderGroupHandleAlignment;
        const uint32_t baseAlignment = rtProps.shaderGroupBaseAlignment;
        const uint32_t groupCount = pipeline->shaderGroupCount();
        if (groupCount != 4) throw std::runtime_error("PathTracingStage: expected 4 ray tracing shader groups");

        const uint32_t handleSizeAligned = alignUp(handleSize, handleAlignment);
        const uint32_t raygenSize = alignUp(handleSizeAligned, baseAlignment);
        const uint32_t missSize = alignUp(handleSizeAligned * 2, baseAlignment);
        const uint32_t hitSize = alignUp(handleSizeAligned, baseAlignment);
        const uint32_t sbtSize = raygenSize + missSize + hitSize;
        const uint32_t dataSize = groupCount * handleSize;

        std::vector<uint8_t> handles(dataSize);
        if (vkGetRayTracingShaderGroupHandlesKHR(device.device(), pipeline->pipeline(), 0, groupCount, dataSize, handles.data()) != VK_SUCCESS) throw std::runtime_error("PathTracingStage: failed to get shader group handles");

        GPUBuffer stagingBuffer = GPUBuffer::simple(device).setSize(sbtSize).setUsage(VK_BUFFER_USAGE_TRANSFER_SRC_BIT).setMemoryUsage(VMA_MEMORY_USAGE_AUTO).setAllocationFlags(VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT).build();
        stagingBuffer.map();
        uint8_t *pData = static_cast<uint8_t *>(stagingBuffer.getMapped());
        std::memset(pData, 0, sbtSize);
        std::memcpy(pData, handles.data() + 0 * handleSize, handleSize);
        std::memcpy(pData + raygenSize, handles.data() + 1 * handleSize, handleSize);
        std::memcpy(pData + raygenSize + handleSizeAligned, handles.data() + 2 * handleSize, handleSize);
        std::memcpy(pData + raygenSize + missSize, handles.data() + 3 * handleSize, handleSize);
        stagingBuffer.flush(sbtSize);
        stagingBuffer.unmap();

        sbtBuffer = std::make_unique<GPUBuffer>(GPUBuffer::simple(device).setSize(sbtSize).setUsage(VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT).setMemoryUsage(VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE).build());

        VkCommandBuffer cmd = device.beginGraphicsCommands();
        VkBufferCopy region{.size = sbtSize};
        vkCmdCopyBuffer(cmd, stagingBuffer.get(), sbtBuffer->get(), 1, &region);
        device.endGraphicsCommands(cmd);

        VkBufferDeviceAddressInfo addrInfo{.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = sbtBuffer->get()};
        const VkDeviceAddress sbtAddress = vkGetBufferDeviceAddress(device.device(), &addrInfo);

        sbtRaygen = {sbtAddress, raygenSize, raygenSize};
        sbtMiss = {sbtAddress + raygenSize, handleSizeAligned, missSize};
        sbtHit = {sbtAddress + raygenSize + missSize, handleSizeAligned, hitSize};
        sbtCallable = {};
    }

    void PathTracingStage::updateDescriptorSet() {
        if (!outputImage || !accumulationImage || !tlas_.isValid()) return;

        VkDescriptorImageInfo imageInfo{.imageView = outputImage->view(0), .imageLayout = VK_IMAGE_LAYOUT_GENERAL};
        VkDescriptorImageInfo accumulationInfo{.imageView = accumulationImage->view(0), .imageLayout = VK_IMAGE_LAYOUT_GENERAL};
        auto tlasHandle = tlas_.handle();
        VkWriteDescriptorSetAccelerationStructureKHR asInfo{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR, .accelerationStructureCount = 1, .pAccelerationStructures = &tlasHandle};
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

        VkDescriptorImageInfo envInfo = IGPUResource::default_<GPUCubemap>().descriptor();
        if (envReady && envHandle.valid() && envHandle.isReady()) {
            envInfo = envHandle.descriptor();
            envInfo.sampler = envSampler;
            envInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }

        VkWriteDescriptorSet w{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = ptSet, .dstBinding = 9, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &envInfo};
        vkUpdateDescriptorSets(device.device(), 1, &w, 0, nullptr);
    }

    void PathTracingStage::onCameraUpdated(entt::registry &registry, entt::entity entity) {
        if (entity != activeCamera(registry)) return;
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

    entt::entity PathTracingStage::activeCamera(entt::registry &registry) const {
        for (const entt::entity entity: registry.view<CameraComponent, EditorCameraComponent>()) {
            if (const auto *node = registry.try_get<SceneNodeComponent>(entity); node && (node->deleted || !node->visible)) continue;
            return entity;
        }
        for (const entt::entity entity: registry.view<CameraComponent>()) {
            if (const auto *node = registry.try_get<SceneNodeComponent>(entity); node && (node->deleted || !node->visible)) continue;
            if (registry.all_of<EditorCameraComponent>(entity) || registry.all_of<TransientComponent>(entity)) continue;
            return entity;
        }
        return entt::null;
    }

    bool PathTracingStage::cameraDataChanged(const Camera::Data &lhs, const Camera::Data &rhs) {
        constexpr float epsilon = 0.00001f;
        for (int column = 0; column < 4; ++column)
            for (int row = 0; row < 4; ++row)
                if (std::abs(lhs.projection[column][row] - rhs.projection[column][row]) > epsilon || std::abs(lhs.view[column][row] - rhs.view[column][row]) > epsilon) return true;
        return false;
    }

    uint64_t PathTracingStage::geometrySignature(entt::registry &registry, bool &waitingForMeshes) const {
        auto combine = [](uint64_t &s, uint64_t v) { s ^= v + 0x9e3779b97f4a7c15ull + (s << 6) + (s >> 2); };
        auto h = [&]<typename T>(uint64_t &s, const T &v) { combine(s, static_cast<uint64_t>(std::hash<T>{}(v))); };
        auto hf = [&](uint64_t &s, float v) { combine(s, static_cast<uint64_t>(std::hash<float>{}(v))); };
        auto hptr = [&]<typename T>(uint64_t &s, const AssetHandle<T> &handle) { combine(s, static_cast<uint64_t>(std::hash<uintptr_t>{}(reinterpret_cast<uintptr_t>(handle.identity())))); };

        waitingForMeshes = false;
        uint64_t seed = 0;
        uint32_t count = 0;
        for (auto entity: registry.view<TransformComponent, ModelComponent, MaterialComponent>()) {
            if (const auto *node = registry.try_get<SceneNodeComponent>(entity); node && (node->deleted || !node->visible)) continue;
            const auto &model = registry.get<ModelComponent>(entity);
            const auto &materialComponent = registry.get<MaterialComponent>(entity);
            const Material fallbackMaterial{};
            const Material *material = materialComponent.materialHandle.get();
            if (!material) material = &fallbackMaterial;
            if (material->alphaMode == AlphaMode::BLEND || !model.meshHandle.valid()) continue;

            ++count;
            h(seed, entt::to_integral(entity));
            hptr(seed, model.meshHandle);
            hptr(seed, materialComponent.materialHandle);
            h(seed, static_cast<uint32_t>(material->alphaMode));
            h(seed, static_cast<uint32_t>(material->shadingModel));
            hf(seed, material->alphaCutoff);
            hf(seed, material->baseColor.r);
            hf(seed, material->baseColor.g);
            hf(seed, material->baseColor.b);
            hf(seed, material->baseColor.a);
            hf(seed, material->metallic);
            hf(seed, material->roughness);
            hf(seed, material->sheenStrength);
            hf(seed, material->sheenColor.r);
            hf(seed, material->sheenColor.g);
            hf(seed, material->sheenColor.b);
            hptr(seed, material->baseColorTexture);
            hptr(seed, material->normalTexture);
            hptr(seed, material->metallicRoughnessTexture);
            hptr(seed, material->occlusionTexture);

            if (!model.meshHandle.isReady()) waitingForMeshes = true;
        }
        h(seed, count);
        return seed;
    }

    uint64_t PathTracingStage::transformSignature(entt::registry &registry) const {
        auto combine = [](uint64_t &s, uint64_t v) { s ^= v + 0x9e3779b97f4a7c15ull + (s << 6) + (s >> 2); };
        auto hf = [&](uint64_t &s, float v) { combine(s, static_cast<uint64_t>(std::hash<float>{}(v))); };
        auto h = [&]<typename T>(uint64_t &s, const T &v) { combine(s, static_cast<uint64_t>(std::hash<T>{}(v))); };

        uint64_t seed = 0;
        for (auto entity: registry.view<TransformComponent, ModelComponent, MaterialComponent>()) {
            if (const auto *node = registry.try_get<SceneNodeComponent>(entity); node && (node->deleted || !node->visible)) continue;
            const auto &t = registry.get<TransformComponent>(entity);
            h(seed, entt::to_integral(entity));
            hf(seed, t.translation.x);
            hf(seed, t.translation.y);
            hf(seed, t.translation.z);
            hf(seed, t.scale.x);
            hf(seed, t.scale.y);
            hf(seed, t.scale.z);
            hf(seed, t.rotation.x);
            hf(seed, t.rotation.y);
            hf(seed, t.rotation.z);
        }
        for (auto entity: registry.view<TransformComponent, LightComponent>()) {
            if (const auto *node = registry.try_get<SceneNodeComponent>(entity); node && (node->deleted || !node->visible)) continue;
            const auto &t = registry.get<TransformComponent>(entity);
            const auto &l = registry.get<LightComponent>(entity);
            h(seed, entt::to_integral(entity));
            hf(seed, t.translation.x);
            hf(seed, t.translation.y);
            hf(seed, t.translation.z);
            hf(seed, l.intensity);
            hf(seed, l.color.r);
            hf(seed, l.color.g);
            hf(seed, l.color.b);
            hf(seed, l.direction.x);
            hf(seed, l.direction.y);
            hf(seed, l.direction.z);
            hf(seed, l.range);
        }
        return seed;
    }

    uint64_t PathTracingStage::textureReadinessSignature() const {
        auto combine = [](uint64_t &s, uint64_t v) { s ^= v + 0x9e3779b97f4a7c15ull + (s << 6) + (s >> 2); };

        uint64_t seed = 0;
        for (const auto &[handle, slot]: handleToSlot) {
            uint64_t entry = 0;
            combine(entry, static_cast<uint64_t>(std::hash<uintptr_t>{}(reinterpret_cast<uintptr_t>(handle.identity()))));
            combine(entry, static_cast<uint64_t>(std::hash<uint32_t>{}(slot)));
            combine(entry, static_cast<uint64_t>(std::hash<bool>{}(handle.isReady())));
            combine(seed, entry);
        }
        combine(seed, static_cast<uint64_t>(std::hash<size_t>{}(handleToSlot.size())));
        return seed;
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
        VkWriteDescriptorSet write{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = ptSet, .dstBinding = 6, .dstArrayElement = slot, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &info};
        vkUpdateDescriptorSets(device.device(), 1, &write, 0, nullptr);
        return slot;
    }

    uint32_t PathTracingStage::alignUp(uint32_t size, uint32_t alignment) const { return (size + alignment - 1) & ~(alignment - 1); }
}
