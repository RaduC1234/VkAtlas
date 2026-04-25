#include "GeometryPass.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include "asset/AssetManager.hpp"
#include "core/Log.hpp"
#include "entity/Object.hpp"
#include "renderer/abstraction/GPUBuffer.hpp"

namespace Atlas {
    GeometryPass::GeometryPass(Device &device, const DescriptorSetLayout &globalSetLayout)
        : IRenderStage(Queue::GRAPHICS), device(device), globalSetLayout(globalSetLayout) {
        createDescriptors();
        createGPUBuffers();
    }

    GeometryPass::~GeometryPass() {
        vkDestroyFramebuffer(device.device(), framebuffer, nullptr);
        vkDestroyRenderPass(device.device(), renderPass, nullptr);
        vkDestroyPipelineLayout(device.device(), pipelineLayout, nullptr);
    }

    void GeometryPass::getDeclaredInputs(std::vector<std::string> &out) const {
        out.push_back("scene_vertex_buffer");
        out.push_back("scene_index_buffer");
        out.push_back("opaque_indirect_cmds");
        out.push_back("object_data_buffer");
        out.push_back("texture_handles");
        // out.push_back("transparent_indirect_cmds");
        // out.push_back("cluster_buffer");
    }

    void GeometryPass::getDeclaredOutputs(std::vector<Resource::Description> &out) const {
        out.push_back(Resource::Description::color("geometry_color"));
        out.push_back(Resource::Description::depth("geometry_depth"));
    }

    void GeometryPass::onResourcesCreated(
        const std::unordered_map<std::string, std::reference_wrapper<Resource> > &resources) {
        colorTarget = &resources.at("geometry_color").get().asGPUImage();
        depthTarget = &resources.at("geometry_depth").get().asGPUImage();
        extent = {colorTarget->extent().width, colorTarget->extent().height};

        sceneVertexBuffer = &resources.at("scene_vertex_buffer").get().asGPUBuffer();
        sceneIndexBuffer = &resources.at("scene_index_buffer").get().asGPUBuffer();
        opaqueIndirectCmds = &resources.at("opaque_indirect_cmds").get().asGPUBuffer();
        textureHandles = &resources.at("texture_handles").get().asCPUBuffer().as<std::vector<AssetHandle> >();

        auto objInfo = resources.at("object_data_buffer").get().asGPUBuffer().descriptorInfo();
        DescriptorWriter(*objectDataSetLayout, *pool)
                .writeBuffer(0, &objInfo)
                .overwrite(objectDataSet);

        createRenderPass();
        createFramebuffer();
        createPipelineLayout();
        createPipelines();
    }

    void GeometryPass::onSceneChanged(entt::registry &registry) {
        lights.clear();
        opaqueDrawCount = static_cast<uint32_t>(textureHandles ? 0 : 0); // reset, filled below

        // Rebuild bindless texture descriptors from what CullingPass filled into texture_handles.
        // Index in the vector == slot in the bindless array, matching textureIndices in GPUObjectData.
        if (textureHandles) {
            opaqueDrawCount = 0;
            for (uint32_t i = 0; i < static_cast<uint32_t>(textureHandles->size()); i++) {
                const auto tex = AssetManager::get().getTexture((*textureHandles)[i]);
                if (!tex) continue;

                const VkDescriptorImageInfo imageInfo{
                    .sampler = tex->getSampler(),
                    .imageView = tex->getImageView(),
                    .imageLayout = tex->getImageLayout(),
                };

                VkWriteDescriptorSet write{};
                write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                write.dstSet = bindlessTextureSet;
                write.dstBinding = 0;
                write.dstArrayElement = i;
                write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                write.descriptorCount = 1;
                write.pImageInfo = &imageInfo;
                vkUpdateDescriptorSets(device.device(), 1, &write, 0, nullptr);
            }
        }

        // Count opaque draws from the registry (CullingPass already wrote indirect cmds,
        // we just need the count for vkCmdDrawIndexedIndirect).
        opaqueDrawCount = 0;
        for (auto entity: registry.view<TransformComponent, MaterialComponent, ModelComponent>()) {
            const auto &material = registry.get<MaterialComponent>(entity);
            if (material.baseColor.w >= 1.0f) {
                opaqueDrawCount++;
            }
        }

        // Skybox
        auto skyboxView = registry.view<SkyboxComponent>();
        if (skyboxView.empty()) {
            AT_WARN("GeometryPass: no skybox entity found, IBL and skybox will be unavailable");
        } else {
            if (skyboxView.size() > 1) {
                AT_WARN("GeometryPass: multiple skyboxes detected, using the first one");
            }

            const auto &skybox = registry.get<SkyboxComponent>(*skyboxView.begin());

            const auto defaultHandle = AssetManager::get().createDefaultWhiteTexture();
            const auto irradiance = AssetManager::get().getCubemap(skybox.irradianceHandle != INVALID_ASSET_HANDLE ? skybox.irradianceHandle : defaultHandle);
            const auto prefilter = AssetManager::get().getCubemap(skybox.prefilterHandle != INVALID_ASSET_HANDLE ? skybox.prefilterHandle : defaultHandle);
            const auto skyboxCubemap = AssetManager::get().getCubemap(skybox.skyboxHandle != INVALID_ASSET_HANDLE ? skybox.skyboxHandle : defaultHandle);

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

            VkWriteDescriptorSet writes[] = {wIrradiance, wPrefilter, wSkybox};
            vkUpdateDescriptorSets(device.device(), std::size(writes), writes, 0, nullptr);
        }

        // Lights
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

        if (!lights.empty()) {
            lightsBuffer->uploadData(lights.data(), sizeof(Light) * lights.size());
        }
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

    void GeometryPass::record(VkCommandBuffer cmd, VkDescriptorSet globalSet) {
        begin(cmd);

        if (opaqueDrawCount > 0) {
            opaquePipeline->bind(cmd);

            const VkDescriptorSet sets[] = {
                globalSet,
                environmentSet,
                bindlessTextureSet,
                objectDataSet,
                lightSet,
            };
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
                                    0, std::size(sets), sets, 0, nullptr);

            VkBuffer vb = sceneVertexBuffer->get();
            VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &offset);
            vkCmdBindIndexBuffer(cmd, sceneIndexBuffer->get(), 0, VK_INDEX_TYPE_UINT32);

            vkCmdDrawIndexedIndirect(cmd,
                                     opaqueIndirectCmds->get(), 0,
                                     opaqueDrawCount,
                                     sizeof(VkDrawIndexedIndirectCommand));
        }

        if (boundSkyboxHandle != INVALID_ASSET_HANDLE && skyboxPipeline) {
            skyboxPipeline->bind(cmd);

            const VkDescriptorSet sets[] = {globalSet, skyboxDescriptorSet};
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &sets[0], 0, nullptr);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 5, 1, &sets[1], 0, nullptr);

            vkCmdDraw(cmd, 36, 1, 0, 0);
        }

        // transparent pass — reserved for a future revision
        // if (transparentDrawCount > 0) { ... }

        end(cmd);
    }

    void GeometryPass::createRenderPass() {
        VkAttachmentDescription color{};
        color.format = colorTarget->format();
        color.samples = VK_SAMPLE_COUNT_1_BIT;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        color.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentDescription depth{};
        depth.format = depthTarget->format();
        depth.samples = VK_SAMPLE_COUNT_1_BIT;
        depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
        depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;
        subpass.pDepthStencilAttachment = &depthRef;

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

        if (vkCreateRenderPass(device.device(), &info, nullptr, &renderPass) != VK_SUCCESS) {
            throw std::runtime_error("GeometryPass: failed to create render pass");
        }
    }

    void GeometryPass::createFramebuffer() {
        const std::array views = {colorTarget->view(0), depthTarget->view(0)};

        VkFramebufferCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        info.renderPass = renderPass;
        info.attachmentCount = static_cast<uint32_t>(views.size());
        info.pAttachments = views.data();
        info.width = extent.width;
        info.height = extent.height;
        info.layers = 1;

        if (vkCreateFramebuffer(device.device(), &info, nullptr, &framebuffer) != VK_SUCCESS) {
            throw std::runtime_error("GeometryPass: failed to create framebuffer");
        }
    }

    void GeometryPass::createDescriptors() {
        environmentSetLayout = DescriptorSetLayout::Builder(device)
                .addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1) // irradiance
                .addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1) // prefilter
                .addBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1) // ltcMat
                .addBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1) // ltcAmp
                .addBinding(4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1) // BRDF LUT
                .build();

        textureSetLayout = DescriptorSetLayout::Builder(device)
                .addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, MAX_TEXTURES)
                .setBindingFlags(0, VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT)
                .setLayoutFlags(VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT)
                .build();

        objectDataSetLayout = DescriptorSetLayout::Builder(device)
                .addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 1)
                .build();

        lightSetLayout = DescriptorSetLayout::Builder(device)
                .addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT, 1)
                .build();

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

        auto ltcMatHandle = AssetManager::get().loadTexture("engine/ltc_mat.bin", VK_FORMAT_R32G32B32A32_SFLOAT, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
        auto ltcAmpHandle = AssetManager::get().loadTexture("engine/ltc_amp.bin", VK_FORMAT_R32G32B32A32_SFLOAT, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
        auto brdfHandle = AssetManager::get().loadTexture("engine/brdf_lut.hdr", VK_FORMAT_R32G32B32A32_SFLOAT, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);

        auto ltcMatTexture = AssetManager::get().getTexture(ltcMatHandle);
        auto ltcAmpTexture = AssetManager::get().getTexture(ltcAmpHandle);
        auto brdfTexture = AssetManager::get().getTexture(brdfHandle);

        VkDescriptorImageInfo matDesc = {ltcMatTexture->getSampler(), ltcMatTexture->getImageView(), ltcMatTexture->getImageLayout()};
        VkDescriptorImageInfo ampDesc = {ltcAmpTexture->getSampler(), ltcAmpTexture->getImageView(), ltcAmpTexture->getImageLayout()};
        VkDescriptorImageInfo brdfDesc = {brdfTexture->getSampler(), brdfTexture->getImageView(), brdfTexture->getImageLayout()};

        VkWriteDescriptorSet wMat{};
        wMat.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        wMat.dstSet = environmentSet;
        wMat.dstBinding = 2;
        wMat.descriptorCount = 1;
        wMat.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        wMat.pImageInfo = &matDesc;

        VkWriteDescriptorSet wAmp = wMat;
        wAmp.dstBinding = 3;
        wAmp.pImageInfo = &ampDesc;
        VkWriteDescriptorSet wBRDF = wMat;
        wBRDF.dstBinding = 4;
        wBRDF.pImageInfo = &brdfDesc;

        VkWriteDescriptorSet wArray[] = {wMat, wAmp, wBRDF};
        vkUpdateDescriptorSets(device.device(), std::size(wArray), wArray, 0, nullptr);
    }

    void GeometryPass::createPipelineLayout() {
        const std::vector layouts = {
            globalSetLayout.getDescriptorSetLayout(),
            environmentSetLayout->getDescriptorSetLayout(),
            textureSetLayout->getDescriptorSetLayout(),
            objectDataSetLayout->getDescriptorSetLayout(),
            lightSetLayout->getDescriptorSetLayout(),
            skyboxSetLayout->getDescriptorSetLayout(),
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
        cfg.depthStencilInfo = makeStencilWrite(1);

        opaquePipeline = std::make_unique<Pipeline>(
            device,
            "shaders/Geometry.vert.spv",
            "shaders/Geometry.frag.spv",
            cfg);

        GraphicsPipelineConfigInfo cfg2{};
        Pipeline::defaultGraphicsPipelineConfigInfo(cfg2);
        cfg2.bindingDescriptions.clear();
        cfg2.attributeDescriptions.clear();
        cfg2.renderPass = renderPass;
        cfg2.pipelineLayout = pipelineLayout;
        cfg2.depthStencilInfo.depthWriteEnable = VK_FALSE;
        cfg2.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

        skyboxPipeline = std::make_unique<Pipeline>(
            device,
            "shaders/Skybox.vert.spv",
            "shaders/Skybox.frag.spv",
            cfg2);
    }

    void GeometryPass::createGPUBuffers() {
        // Lights buffer — GeometryPass still owns this, lights are not a CullingPass concern
        lightsBuffer = std::make_unique<GPUBuffer>(GPUBuffer::Builder(device)
            .setSize(sizeof(Light) * MAX_LIGHTS)
            .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
            .setMemoryUsage(VMA_MEMORY_USAGE_AUTO)
            .setAllocationFlags(VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT)
            .build());
        lightsBuffer->map();
        std::memset(lightsBuffer->getMapped(), 0, sizeof(Light) * MAX_LIGHTS);

        auto lightInfo = lightsBuffer->descriptorInfo();
        DescriptorWriter(*lightSetLayout, *pool)
                .writeBuffer(0, &lightInfo)
                .overwrite(lightSet);
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
