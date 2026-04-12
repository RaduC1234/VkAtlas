#include "GeometryPass.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include "core/Log.hpp"

namespace Atlas {
    GeometryPass::GeometryPass(Device &device, uint32_t width, uint32_t height, const DescriptorSetLayout &globalSetLayout): device(device) {
        createRenderTargets(width, height);
        createRenderPass();
        createFramebuffer();
        createDescriptors();
        createPipelineLayout(globalSetLayout);
        createPipelines();

        defaultWhiteHandle = AssetManager::get().createDefaultWhiteTexture();
        registerTexture(defaultWhiteHandle);

        createGPUBuffers();
    }

    GeometryPass::~GeometryPass() {
        vkDestroyFramebuffer(device.device(), framebuffer, nullptr);
        vkDestroyRenderPass(device.device(), renderPass, nullptr);
        vkDestroyPipelineLayout(device.device(), pipelineLayout, nullptr);
    }

    void GeometryPass::begin(VkCommandBuffer cmd) {
        std::array<VkClearValue, 2> clears{};
        clears[0].color = {0.0151f, 0.0151f, 0.0151f, 1.0f};
        clears[1].depthStencil = {1.0f, 0};

        VkRenderPassBeginInfo info{};
        info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        info.renderPass = renderPass;
        info.framebuffer = framebuffer;
        info.renderArea = {{0, 0}, extent};
        info.clearValueCount = static_cast<uint32_t>(clears.size());
        info.pClearValues = clears.data();

        vkCmdBeginRenderPass(cmd, &info, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(extent.width);
        viewport.height = static_cast<float>(extent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        VkRect2D scissor{{0, 0}, extent};
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);
    }

    void GeometryPass::end(VkCommandBuffer cmd) {
        vkCmdEndRenderPass(cmd);
    }

    void GeometryPass::barrier(VkCommandBuffer cmd) {
        std::array<VkImageMemoryBarrier, 2> barriers{};

        barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barriers[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barriers[0].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barriers[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[0].image = colorTarget.image();
        barriers[0].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        barriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barriers[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        barriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barriers[1].oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        barriers[1].newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        barriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[1].image = depthTarget.image();
        barriers[1].subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 0, 1, 0, 1};

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr,
                             barriers.size(), barriers.data()
        );
    }

    void GeometryPass::getDeclaredResources(std::vector<PassResource> &out) const {
        auto colorImage = colorTarget.image();
        auto depthImage = depthTarget.image();
        out.push_back(PassResource{&colorImage, PassResource::Type::ColorAttachment, PassResource::Access::Write, "geometry_color"});
        out.push_back(PassResource{&depthImage, PassResource::Type::DepthAttachment, PassResource::Access::Write, "geometry_depth"});
    }

    void GeometryPass::build(entt::registry &registry) {
        // --- Environment (IBL) + Skybox ---
        auto skyboxView = registry.view<SkyboxComponent>();
        if (skyboxView.empty()) {
            AT_WARN("GeometryPass: no skybox entity found, IBL and skybox will be unavailable");
        } else {
            if (skyboxView.size() > 1)
                AT_WARN("GeometryPass: multiple skyboxes detected, using the first one");

            const auto &skybox = registry.get<SkyboxComponent>(*skyboxView.begin());
            const auto irradiance = AssetManager::get().getCubemap(skybox.irradianceHandle != INVALID_ASSET_HANDLE ? skybox.irradianceHandle : defaultWhiteHandle);
            const auto prefilter = AssetManager::get().getCubemap(skybox.prefilterHandle != INVALID_ASSET_HANDLE ? skybox.prefilterHandle : defaultWhiteHandle);
            const auto skyboxCubemap = AssetManager::get().getCubemap(skybox.skyboxHandle != INVALID_ASSET_HANDLE ? skybox.skyboxHandle : defaultWhiteHandle);

            VkDescriptorImageInfo irradianceInfo = {irradiance->getSampler(), irradiance->getImageView(), irradiance->getImageLayout()};
            VkDescriptorImageInfo prefilterInfo = {prefilter->getSampler(), prefilter->getImageView(), prefilter->getImageLayout()};
            VkDescriptorImageInfo skyboxInfo = {skyboxCubemap->getSampler(), skyboxCubemap->getImageView(), skyboxCubemap->getImageLayout()};

            VkWriteDescriptorSet wIrradiance{};
            wIrradiance.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            wIrradiance.dstSet = environmentSet;
            wIrradiance.dstBinding = 0;
            wIrradiance.descriptorCount = 1;
            wIrradiance.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            wIrradiance.pImageInfo = &irradianceInfo;

            VkWriteDescriptorSet wPrefilter = wIrradiance;
            wPrefilter.dstBinding = 1;
            wPrefilter.pImageInfo = &prefilterInfo;

            VkWriteDescriptorSet wSkybox{};
            wSkybox.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            wSkybox.dstSet = skyboxDescriptorSet;
            wSkybox.dstBinding = 0;
            wSkybox.descriptorCount = 1;
            wSkybox.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            wSkybox.pImageInfo = &skyboxInfo;
            boundSkyboxHandle = skybox.skyboxHandle;

            std::vector writes = {wIrradiance, wPrefilter, wSkybox};

            vkUpdateDescriptorSets(device.device(), writes.size(), writes.data(), 0, nullptr);
        }

        auto *opaqueDrawCmds = static_cast<VkDrawIndexedIndirectCommand *>(opaqueIndirectCommandBuffer->getMapped());

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
            const glm::mat4 model4 = transform.mat4();

            const GPUObjectData data{
                .modelMatrix = model4,
                .normalMatrix = glm::mat4(glm::inverseTranspose(glm::mat3(model4))),
                .textureIndices = glm::uvec4(
                    resolveTextureIndex(material.albedoTexture != INVALID_ASSET_HANDLE ? material.albedoTexture : defaultWhiteHandle),
                    resolveTextureIndex(material.normalMap != INVALID_ASSET_HANDLE ? material.normalMap : defaultWhiteHandle),
                    resolveTextureIndex(material.metallicRoughnessMap != INVALID_ASSET_HANDLE ? material.metallicRoughnessMap : defaultWhiteHandle),
                    resolveTextureIndex(material.ambientOcclusion != INVALID_ASSET_HANDLE ? material.ambientOcclusion : defaultWhiteHandle)
                ),
                .baseColor = material.baseColor,
            };

            if (material.baseColor.w >= 1.0f) {
                const auto idx = static_cast<uint32_t>(opaqueObjectData.size());
                opaqueObjectData.emplace(entity, data);
                opaqueDrawCmds[idx] = {
                    .indexCount = alloc.indexCount,
                    .instanceCount = 1,
                    .firstIndex = alloc.firstIndex,
                    .vertexOffset = static_cast<int32_t>(alloc.firstVertex),
                    .firstInstance = idx,
                };
            }
        }

        // --- Lights ---
        for (auto entity: registry.view<TransformComponent, LightComponent>()) {
            auto [transform, light] = registry.get<TransformComponent, LightComponent>(entity);
            lights.emplace(entity, Light{
                               static_cast<uint32_t>(light.type),
                               light.intensity,
                               light.range == 0.0f ? 20.0f : light.range,
                               light.innerConeAngle,
                               light.color,
                               light.outerConeAngle,
                               transform.translation,
                               light.width,
                               light.direction,
                               light.height,
                           });
        }

        // --- Upload SSBOs ---
        if (!opaqueObjectData.empty())
            objectDataBuffer->uploadData(opaqueObjectData.data(),
                                         sizeof(GPUObjectData) * opaqueObjectData.size());

        if (!lights.empty())
            lightsBuffer->uploadData(lights.data(), sizeof(Light) * lights.size());
    }

    // -------------------------------------------------------------------------
    // Record — called every frame between begin() / end()
    // -------------------------------------------------------------------------

    void GeometryPass::record(VkCommandBuffer cmd, VkDescriptorSet globalSet) const {
        // Render opaque geometry
        if (!opaqueObjectData.empty()) {
            opaquePipeline->bind(cmd);

            const VkDescriptorSet sets[] = {
                globalSet, // set 0 — global UBO (owned by RenderSystemV2)
                environmentSet, // set 1 — IBL
                bindlessTextureSet, // set 2 — textures
                objectDataSet, // set 3 — object SSBO
                lightSet, // set 4 — lights SSBO
            };
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipelineLayout, 0, 5, sets, 0, nullptr);

            VkBuffer vb = mergedVertexBuffer->get();
            VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &offset);
            vkCmdBindIndexBuffer(cmd, mergedIndexBuffer->get(), 0, VK_INDEX_TYPE_UINT32);

            vkCmdDrawIndexedIndirect(cmd,
                                     opaqueIndirectCommandBuffer->get(), 0,
                                     static_cast<uint32_t>(opaqueObjectData.size()),
                                     sizeof(VkDrawIndexedIndirectCommand));
        }

        // Render skybox after geometry
        if (boundSkyboxHandle != INVALID_ASSET_HANDLE && skyboxPipeline) {
            skyboxPipeline->bind(cmd);

            const VkDescriptorSet sets[] = {
                globalSet, // set 0 — global UBO
                skyboxDescriptorSet, // set 5 — skybox cubemap
            };
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipelineLayout, 0, 1, &sets[0], 0, nullptr);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipelineLayout, 5, 1, &sets[1], 0, nullptr);

            vkCmdDraw(cmd, 36, 1, 0, 0);
        }
    }

    void GeometryPass::createRenderTargets(uint32_t width, uint32_t height) {
        extent = {width, height};

        colorTarget = GPUImage::Builder(device)
                .setExtent(width, height)
                .setFormat(VK_FORMAT_R16G16B16A16_SFLOAT)
                .setUsage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)
                .addView(VK_IMAGE_ASPECT_COLOR_BIT)
                .build();

        depthTarget = GPUImage::Builder(device)
                .setExtent(width, height)
                .setFormat(VK_FORMAT_D32_SFLOAT_S8_UINT)
                .setUsage(VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)
                .addView(VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)
                .addView(VK_IMAGE_ASPECT_STENCIL_BIT)
                .build();
    }

    void GeometryPass::createRenderPass() {
        // Color attachment — HDR offscreen
        VkAttachmentDescription color{};
        color.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        color.samples = VK_SAMPLE_COUNT_1_BIT;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        color.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; // ready for post process

        VkAttachmentDescription depth{};
        depth.format         = depthTarget.format();
        depth.samples        = VK_SAMPLE_COUNT_1_BIT;
        depth.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        depth.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
        depth.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        depth.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;
        subpass.pDepthStencilAttachment = &depthRef;

        // Dependency: ensure post process waits for color write to finish
        VkSubpassDependency dep{};
        dep.srcSubpass = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass = 0;
        dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dep.srcAccessMask = 0;
        dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        const std::array<VkAttachmentDescription, 2> attachments = {color, depth};

        VkRenderPassCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        info.attachmentCount = static_cast<uint32_t>(attachments.size());
        info.pAttachments = attachments.data();
        info.subpassCount = 1;
        info.pSubpasses = &subpass;
        info.dependencyCount = 1;
        info.pDependencies = &dep;

        if (vkCreateRenderPass(device.device(), &info, nullptr, &renderPass) != VK_SUCCESS)
            throw std::runtime_error("GeometryPass: failed to create render pass");
    }

    void GeometryPass::createFramebuffer() {
        const std::array<VkImageView, 2> views = {colorTarget.view(0), depthTarget.view(0)};

        VkFramebufferCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        info.renderPass = renderPass;
        info.attachmentCount = static_cast<uint32_t>(views.size());
        info.pAttachments = views.data();
        info.width = extent.width;
        info.height = extent.height;
        info.layers = 1;

        if (vkCreateFramebuffer(device.device(), &info, nullptr, &framebuffer) != VK_SUCCESS)
            throw std::runtime_error("GeometryPass: failed to create framebuffer");
    }

    // -------------------------------------------------------------------------
    // Private — descriptors
    // -------------------------------------------------------------------------

    void GeometryPass::createDescriptors() {
        // set 1 — environment (irradiance + prefilter)
        environmentSetLayout = DescriptorSetLayout::Builder(device)
                .addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1)
                .addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1)
                .build();

        // set 2 — bindless textures
        textureSetLayout = DescriptorSetLayout::Builder(device)
                .addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, MAX_TEXTURES)
                .setBindingFlags(0, VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT)
                .setLayoutFlags(VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT)
                .build();

        // set 3 — object SSBO
        objectDataSetLayout = DescriptorSetLayout::Builder(device)
                .addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 1)
                .build();

        // set 4 — lights SSBO
        lightSetLayout = DescriptorSetLayout::Builder(device)
                .addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT, 1)
                .build();

        // set 5 — skybox cubemap
        skyboxSetLayout = DescriptorSetLayout::Builder(device)
                .addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
                .build();

        pool = DescriptorPool::Builder(device)
                .setMaxSets(6)
                .addPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_TEXTURES + 3)
                .addPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2)
                .setPoolFlags(VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT)
                .build();

        if (!pool->allocateDescriptor(environmentSetLayout->getDescriptorSetLayout(), environmentSet)) {
            throw std::runtime_error("GeometryPass: failed to allocate environment set");
        }

        if (!pool->allocateDescriptor(textureSetLayout->getDescriptorSetLayout(), bindlessTextureSet)) {
            throw std::runtime_error("GeometryPass: failed to allocate bindless texture set");
        }

        if (!pool->allocateDescriptor(objectDataSetLayout->getDescriptorSetLayout(), objectDataSet)) {
            throw std::runtime_error("GeometryPass: failed to allocate object data set");
        }

        if (!pool->allocateDescriptor(lightSetLayout->getDescriptorSetLayout(), lightSet)) {
            throw std::runtime_error("GeometryPass: failed to allocate light set");
        }

        if (!pool->allocateDescriptor(skyboxSetLayout->getDescriptorSetLayout(), skyboxDescriptorSet)) {
            throw std::runtime_error("GeometryPass: failed to allocate skybox descriptor set");
        }
    }

    void GeometryPass::createPipelineLayout(const DescriptorSetLayout &globalSetLayout) {
        const std::vector layouts = {
            globalSetLayout.getDescriptorSetLayout(), // set 0
            environmentSetLayout->getDescriptorSetLayout(), // set 1
            textureSetLayout->getDescriptorSetLayout(), // set 2
            objectDataSetLayout->getDescriptorSetLayout(), // set 3
            lightSetLayout->getDescriptorSetLayout(), // set 4
            skyboxSetLayout->getDescriptorSetLayout(), // set 5
        };

        VkPipelineLayoutCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        info.setLayoutCount = static_cast<uint32_t>(layouts.size());
        info.pSetLayouts = layouts.data();

        if (vkCreatePipelineLayout(device.device(), &info, nullptr, &pipelineLayout) != VK_SUCCESS) {
            throw std::runtime_error("GeometryPass: failed to create pipeline layout");
        }
    }

    void GeometryPass::createPipelines() {
        assert(pipelineLayout != VK_NULL_HANDLE);
        assert(renderPass != VK_NULL_HANDLE);

        GraphicsPipelineConfigInfo cfg{};
        Pipeline::defaultGraphicsPipelineConfigInfo(cfg);
        cfg.bindingDescriptions = Mesh::Vertex::getBindingDescriptions();
        cfg.attributeDescriptions = Mesh::Vertex::getAttributeDescriptions();
        cfg.renderPass = renderPass;
        cfg.pipelineLayout = pipelineLayout;

        cfg.depthStencilInfo = makeStencilWrite(1); // 1 - general layer

        opaquePipeline = std::make_unique<Pipeline>(
            device,
            "shaders/RenderSystemV2.studio.vert.spv",
            "shaders/RenderSystemV2.real_time.frag.spv",
            cfg
        );

        GraphicsPipelineConfigInfo cfg2{};
        Pipeline::defaultGraphicsPipelineConfigInfo(cfg2);

        // Skybox renders without vertex buffers — hardcoded in shader
        cfg2.bindingDescriptions.clear();
        cfg2.attributeDescriptions.clear();

        cfg2.renderPass = renderPass;
        cfg2.pipelineLayout = pipelineLayout;

        cfg.depthStencilInfo = makeStencilWrite(0); // 0 - skybox layer
        cfg2.depthStencilInfo.depthWriteEnable = VK_FALSE; // Depth testing: write disabled, compare LESS_OR_EQUAL so it renders behind geometry
        cfg2.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

        skyboxPipeline = std::make_unique<Pipeline>(
            device,
            "shaders/Skybox.vert.spv",
            "shaders/Skybox.frag.spv",
            cfg2
        );
    }

    // -------------------------------------------------------------------------
    // Private — GPU buffers
    // -------------------------------------------------------------------------

    void GeometryPass::createGPUBuffers() {
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

        opaqueIndirectCommandBuffer = std::make_unique<Buffer>(
            device,
            sizeof(VkDrawIndexedIndirectCommand) * MAX_OBJECTS,
            VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
            VMA_MEMORY_USAGE_AUTO,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
            | VMA_ALLOCATION_CREATE_MAPPED_BIT
        );
        opaqueIndirectCommandBuffer->map();

        objectDataBuffer = std::make_unique<Buffer>(
            device, sizeof(GPUObjectData) * MAX_OBJECTS,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VMA_MEMORY_USAGE_AUTO,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
        );
        auto objInfo = objectDataBuffer->descriptorInfo();
        DescriptorWriter(*objectDataSetLayout, *pool)
                .writeBuffer(0, &objInfo)
                .overwrite(objectDataSet);

        lightsBuffer = std::make_unique<Buffer>(
            device, sizeof(Light) * MAX_LIGHTS,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VMA_MEMORY_USAGE_AUTO,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
            | VMA_ALLOCATION_CREATE_MAPPED_BIT
        );
        lightsBuffer->map();
        std::memset(lightsBuffer->getMapped(), 0, sizeof(Light) * MAX_LIGHTS);

        auto lightInfo = lightsBuffer->descriptorInfo();
        DescriptorWriter(*lightSetLayout, *pool)
                .writeBuffer(0, &lightInfo)
                .overwrite(lightSet);
    }

    uint32_t GeometryPass::registerTexture(AssetHandle handle) {
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

    void GeometryPass::registerMesh(AssetHandle handle) {
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

    uint32_t GeometryPass::resolveTextureIndex(AssetHandle handle) const {
        if (handle == INVALID_ASSET_HANDLE) return 0;
        const auto it = handleToTextureSlot.find(handle);
        return it != handleToTextureSlot.end() ? it->second : 0;
    }

    VkPipelineDepthStencilStateCreateInfo GeometryPass::makeStencilWrite(uint8_t ref) {
        VkStencilOpState stencilOp{};
        stencilOp.failOp = VK_STENCIL_OP_KEEP;
        stencilOp.passOp = VK_STENCIL_OP_REPLACE;
        stencilOp.depthFailOp = VK_STENCIL_OP_KEEP;
        stencilOp.compareOp = VK_COMPARE_OP_ALWAYS;
        stencilOp.compareMask = 0xFF;
        stencilOp.writeMask = 0xFF;
        stencilOp.reference = ref;

        VkPipelineDepthStencilStateCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        info.depthTestEnable = VK_TRUE;
        info.depthWriteEnable = VK_TRUE;
        info.depthCompareOp = VK_COMPARE_OP_LESS;
        info.stencilTestEnable = VK_TRUE;
        info.front = stencilOp;
        info.back = stencilOp;
        return info;
    }
} // namespace Atlas
