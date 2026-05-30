#include "GeometryStage.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <cstddef>
#include <memory>

#include <glm/glm.hpp>

#include "asset/AssetManager.hpp"
#include "core/Log.hpp"
#include "entity/Object.hpp"
#include "renderer/abstraction/GPUBuffer.hpp"

namespace Atlas {
    GeometryStage::GeometryStage(Device &device, AssetManager &assets, const DescriptorSetLayout &globalSetLayout)
        : RenderStage(Queue::GRAPHICS), device(device), assets(assets), globalSetLayout(globalSetLayout) {
        createDescriptors();
    }

    GeometryStage::~GeometryStage() {
        vkDestroyFramebuffer(device.device(), framebuffer, nullptr);
        vkDestroyRenderPass(device.device(), renderPass, nullptr);
        vkDestroyPipelineLayout(device.device(), pipelineLayout, nullptr);
    }

    void GeometryStage::getDeclaredInputs(std::vector<std::string> &out) const {
        out.push_back("scene_objects");
        out.push_back("scene_lights");
        out.push_back("scene_draws");
    }

    void GeometryStage::getDeclaredOutputs(std::vector<Resource::Description> &out) const {
        out.push_back(Resource::Description::color("geometry_color", VK_FORMAT_R32G32B32A32_SFLOAT));
        out.push_back(Resource::Description::depth("geometry_depth", VK_FORMAT_D24_UNORM_S8_UINT));
    }

    void GeometryStage::onResourcesCreated(const Context &ctx) {
        colorTarget = &ctx.resources.at("geometry_color").get().asImage();
        depthTarget = &ctx.resources.at("geometry_depth").get().asImage();
        drawData = &ctx.resources.at("scene_draws").get().asCPUBuffer().as<RasterDrawData>();
        extent = {colorTarget->extent().width, colorTarget->extent().height};

        auto objInfo = ctx.resources.at("scene_objects").get().asBuffer().descriptorInfo();
        auto lightInfo = ctx.resources.at("scene_lights").get().asBuffer().descriptorInfo();

        DescriptorWriter(*objectDataSetLayout, *pool)
                .writeBuffer(0, &objInfo)
                .overwrite(objectDataSet);

        DescriptorWriter(*lightSetLayout, *pool)
                .writeBuffer(0, &lightInfo)
                .overwrite(lightSet);

        createRenderPass();
        createFramebuffer();
        createPipelineLayout();
        createPipelines();

        loadLookupTextures();
    }

    void GeometryStage::onUpdate(entt::registry &registry) {
        updateTextureDescriptors();
        updateLookupTextureDescriptors();

        auto skyboxView = registry.view<SkyboxComponent>();
        entt::entity activeSkybox = entt::null;
        uint32_t activeSkyboxCount = 0;
        for (const entt::entity entity: skyboxView) {
            if (const auto *node = registry.try_get<SceneNodeComponent>(entity); node && (node->deleted || !node->visible)) {
                continue;
            }

            if (activeSkybox == entt::null) {
                activeSkybox = entity;
            }
            ++activeSkyboxCount;
        }

        drawSkybox = activeSkybox != entt::null;

        if (activeSkybox == entt::null) {
            if (skyboxView.empty()) {
                AT_WARN("GeometryStage: no skybox entity found, using the default environment");
            }
            if (boundIrradianceHandle.valid() || boundPrefilterHandle.valid() || boundSkyboxHandle.valid()) {
                updateSkyboxDescriptors({});
            }
        } else {
            if (activeSkyboxCount > 1) {
                AT_WARN("GeometryStage: multiple skyboxes detected, using the first one");
            }
            const auto &skybox = registry.get<SkyboxComponent>(activeSkybox);
            updateSkyboxDescriptors(skybox);
        }
    }

    void GeometryStage::begin(VkCommandBuffer cmd) {
        std::array<VkClearValue, 2> clears{};
        clears[0].color = {0.050876f, 0.050876f, 0.050876f, 1.0f};
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

    void GeometryStage::end(VkCommandBuffer cmd) {
        vkCmdEndRenderPass(cmd);
    }

    void GeometryStage::record(VkCommandBuffer cmd, VkDescriptorSet globalSet) {
        begin(cmd);

        const auto &draws = std::get<0>(*drawData);
        if (!draws.empty()) {
            opaquePipeline->bind(cmd);

            const VkDescriptorSet sets[] = {
                globalSet,
                environmentSet,
                textureSet,
                objectDataSet,
                lightSet,
            };
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
                                    0, std::size(sets), sets, 0, nullptr);

            for (const auto &[mesh, objectIndex]: draws) {
                mesh.bind(cmd);
                vkCmdDrawIndexed(cmd,
                                 static_cast<uint32_t>(mesh->indices().size()),
                                 1, 0, 0,
                                 objectIndex);
            }
        }

        if (drawSkybox && skyboxPipeline) {
            skyboxPipeline->bind(cmd);
            const VkDescriptorSet sets[] = {globalSet, skyboxDescriptorSet};
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &sets[0], 0, nullptr);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 5, 1, &sets[1], 0, nullptr);
            vkCmdDraw(cmd, 36, 1, 0, 0);
        }

        end(cmd);
    }

    void GeometryStage::createRenderPass() {
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
        dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                           VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                           VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
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
            throw std::runtime_error("GeometryStage: failed to create render pass");
        }
    }

    void GeometryStage::createFramebuffer() {
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
            throw std::runtime_error("GeometryStage: failed to create framebuffer");
        }
    }

    void GeometryStage::createDescriptors() {
        environmentSetLayout = DescriptorSetLayout::Builder(device)
                .addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1) // irradiance
                .addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1) // prefilter
                .addBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1) // ltcMat
                .addBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1) // ltcAmp
                .addBinding(4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1) // BRDF LUT
                .setBindingFlags(0, VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT)
                .setBindingFlags(1, VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT)
                .setBindingFlags(2, VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT)
                .setBindingFlags(3, VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT)
                .setBindingFlags(4, VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT)
                .setLayoutFlags(VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT)
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
                .setBindingFlags(0, VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT)
                .setLayoutFlags(VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT)
                .build();

        pool = DescriptorPool::Builder(device)
                .setMaxSets(4)
                .addPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 6) // 5 IBL + 1 skybox
                .addPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2) // objects + lights
                .setPoolFlags(VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT)
                .build();

        texturePool = DescriptorPool::Builder(device)
                .setMaxSets(1)
                .addPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_TEXTURES)
                .setPoolFlags(VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT)
                .build();

        if (!pool->allocateDescriptor(environmentSetLayout->getDescriptorSetLayout(), environmentSet)) {
            throw std::runtime_error("GeometryStage: failed to allocate environment set");
        }
        if (!texturePool->allocateDescriptor(textureSetLayout->getDescriptorSetLayout(), textureSet)) {
            throw std::runtime_error("GeometryStage: failed to allocate texture set");
        }
        if (!pool->allocateDescriptor(objectDataSetLayout->getDescriptorSetLayout(), objectDataSet)) {
            throw std::runtime_error("GeometryStage: failed to allocate object data set");
        }
        if (!pool->allocateDescriptor(lightSetLayout->getDescriptorSetLayout(), lightSet)) {
            throw std::runtime_error("GeometryStage: failed to allocate light set");
        }
        if (!pool->allocateDescriptor(skyboxSetLayout->getDescriptorSetLayout(), skyboxDescriptorSet)) {
            throw std::runtime_error("GeometryStage: failed to allocate skybox descriptor set");
        }

        VkDescriptorImageInfo irradianceDesc = IGPUResource::default_<GPUCubemap>().descriptor();
        VkDescriptorImageInfo prefilterDesc = IGPUResource::default_<GPUCubemap>().descriptor();
        VkDescriptorImageInfo skyboxDesc = IGPUResource::default_<GPUCubemap>().descriptor();
        VkDescriptorImageInfo matDesc = IGPUResource::default_<GPUTexture>().descriptor();
        VkDescriptorImageInfo ampDesc = IGPUResource::default_<GPUTexture>().descriptor();
        VkDescriptorImageInfo brdfDesc = IGPUResource::default_<GPUTexture>().descriptor();

        VkWriteDescriptorSet wIrradiance{};
        wIrradiance.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        wIrradiance.dstSet = environmentSet;
        wIrradiance.dstBinding = 0;
        wIrradiance.descriptorCount = 1;
        wIrradiance.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        wIrradiance.pImageInfo = &irradianceDesc;

        VkWriteDescriptorSet wPrefilter = wIrradiance;
        wPrefilter.dstBinding = 1;
        wPrefilter.pImageInfo = &prefilterDesc;

        VkWriteDescriptorSet wSkybox = wIrradiance;
        wSkybox.dstSet = skyboxDescriptorSet;
        wSkybox.dstBinding = 0;
        wSkybox.pImageInfo = &skyboxDesc;

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

        VkWriteDescriptorSet writes[] = {wIrradiance, wPrefilter, wSkybox, wMat, wAmp, wBRDF};
        vkUpdateDescriptorSets(device.device(), std::size(writes), writes, 0, nullptr);

        VkDescriptorImageInfo defaultTextureInfo = IGPUResource::default_<GPUTexture>().descriptor();
        VkWriteDescriptorSet textureWrite{};
        textureWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        textureWrite.dstSet = textureSet;
        textureWrite.dstBinding = 0;
        textureWrite.dstArrayElement = 0;
        textureWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        textureWrite.descriptorCount = 1;
        textureWrite.pImageInfo = &defaultTextureInfo;
        vkUpdateDescriptorSets(device.device(), 1, &textureWrite, 0, nullptr);
    }

    AssetHandle<Texture> GeometryStage::loadRawLookupTexture(
        const std::string &path,
        const uint32_t width,
        const uint32_t height,
        const VkFormat format) {
        std::vector<std::byte> bytes = AssetManager::loadFileAs<std::byte>(path);
        const size_t expectedSize = static_cast<size_t>(width) * height * 4u * sizeof(float);
        if (bytes.size() != expectedSize) {
            AT_ERROR("GeometryStage: invalid lookup texture '{}' size: got {} bytes, expected {}",
                     path, bytes.size(), expectedSize);
            return AssetHandle<Texture>::invalid();
        }

        return assets.store<Texture>(
            std::make_shared<Texture>(
                bytes,
                width,
                height,
                format,
                VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE),
            path);
    }

    void GeometryStage::loadLookupTextures() {
        constexpr uint32_t ltcLutSize = 64;

        ltcMatLUT = loadRawLookupTexture(
            "##engine/ltc_mat.lut.bin",
            ltcLutSize,
            ltcLutSize,
            VK_FORMAT_R32G32B32A32_SFLOAT);
        ltcAmpLUT = loadRawLookupTexture(
            "##engine/ltc_amp.lut.bin",
            ltcLutSize,
            ltcLutSize,
            VK_FORMAT_R32G32B32A32_SFLOAT);
        brdfLUT = assets.store<Texture>("##engine/brdf.lut.hdr");
    }

    void GeometryStage::createPipelineLayout() {
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
            throw std::runtime_error("GeometryStage: failed to create pipeline layout");
        }
    }

    void GeometryStage::createPipelines() {
        assert(pipelineLayout != VK_NULL_HANDLE);
        assert(renderPass != VK_NULL_HANDLE);

        GraphicsPipelineConfigInfo cfg{};
        Pipeline::defaultGraphicsPipelineConfigInfo(cfg);
        cfg.bindingDescriptions = GPUMesh::Vertex::getBindingDescriptions();
        cfg.attributeDescriptions = GPUMesh::Vertex::getAttributeDescriptions();
        cfg.renderPass = renderPass;
        cfg.pipelineLayout = pipelineLayout;
        cfg.depthStencilInfo = makeStencilWrite(1);

        opaquePipeline = std::make_unique<Pipeline>(
            device,
            "##engine/shaders/Geometry.vert.spv",
            "##engine/shaders/Geometry.frag.spv",
            cfg
        );

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
            "##engine/shaders/Skybox.vert.spv",
            "##engine/shaders/Skybox.frag.spv",
            cfg2
        );
    }

    void GeometryStage::updateTextureDescriptors() {
        if (!drawData || std::get<2>(*drawData) == boundTextureRevision) {
            return;
        }

        const auto &[draws, textures, textureRevision] = *drawData;
        (void) draws;

        std::vector<VkDescriptorImageInfo> imageInfos;
        std::vector<VkWriteDescriptorSet> writes;
        imageInfos.reserve(textures.size());
        writes.reserve(textures.size());

        for (const auto &[texture, slot]: textures) {
            if (!texture.valid() || slot == 0 || slot >= MAX_TEXTURES) {
                continue;
            }

            texture.registerBindlessSlot(device.device(), textureSet, 0, slot);
            imageInfos.push_back(texture.descriptor());

            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = textureSet;
            write.dstBinding = 0;
            write.dstArrayElement = slot;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.descriptorCount = 1;
            writes.push_back(write);
        }

        for (size_t i = 0; i < writes.size(); ++i) {
            writes[i].pImageInfo = &imageInfos[i];
        }

        if (!writes.empty()) {
            vkUpdateDescriptorSets(device.device(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        }

        boundTextureRevision = textureRevision;
    }

    void GeometryStage::updateLookupTextureDescriptors() {
        const bool ltcMatReady = ltcMatLUT.valid() && ltcMatLUT.isReady();
        const bool ltcAmpReady = ltcAmpLUT.valid() && ltcAmpLUT.isReady();
        const bool brdfReady = brdfLUT.valid() && brdfLUT.isReady();

        if (ltcMatReady == boundLtcMatReady &&
            ltcAmpReady == boundLtcAmpReady &&
            brdfReady == boundBrdfReady) {
            return;
        }

        VkDescriptorImageInfo matInfo = ltcMatLUT.valid()
                                            ? ltcMatLUT.descriptor()
                                            : IGPUResource::default_<GPUTexture>().descriptor();
        VkDescriptorImageInfo ampInfo = ltcAmpLUT.valid()
                                            ? ltcAmpLUT.descriptor()
                                            : IGPUResource::default_<GPUTexture>().descriptor();
        VkDescriptorImageInfo brdfInfo = brdfLUT.valid()
                                             ? brdfLUT.descriptor()
                                             : IGPUResource::default_<GPUTexture>().descriptor();

        VkWriteDescriptorSet wMat{};
        wMat.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        wMat.dstSet = environmentSet;
        wMat.dstBinding = 2;
        wMat.descriptorCount = 1;
        wMat.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        wMat.pImageInfo = &matInfo;

        VkWriteDescriptorSet wAmp = wMat;
        wAmp.dstBinding = 3;
        wAmp.pImageInfo = &ampInfo;

        VkWriteDescriptorSet wBRDF = wMat;
        wBRDF.dstBinding = 4;
        wBRDF.pImageInfo = &brdfInfo;

        const VkWriteDescriptorSet writes[] = {wMat, wAmp, wBRDF};
        vkUpdateDescriptorSets(device.device(), std::size(writes), writes, 0, nullptr);

        boundLtcMatReady = ltcMatReady;
        boundLtcAmpReady = ltcAmpReady;
        boundBrdfReady = brdfReady;
    }

    void GeometryStage::updateSkyboxDescriptors(const SkyboxComponent &skybox) {
        const bool irradianceReady = skybox.irradianceHandle.valid() && skybox.irradianceHandle.isReady();
        const bool prefilterReady = skybox.prefilterHandle.valid() && skybox.prefilterHandle.isReady();
        const bool skyboxReady = skybox.skyboxHandle.valid() && skybox.skyboxHandle.isReady();

        const bool updateEnvironment =
                skybox.irradianceHandle != boundIrradianceHandle ||
                irradianceReady != boundIrradianceReady ||
                skybox.prefilterHandle != boundPrefilterHandle ||
                prefilterReady != boundPrefilterReady;
        const bool updateSkybox =
                skybox.skyboxHandle != boundSkyboxHandle ||
                skyboxReady != boundSkyboxReady;

        VkDescriptorImageInfo irradianceInfo{};
        VkDescriptorImageInfo prefilterInfo{};
        VkDescriptorImageInfo skyboxInfo{};
        VkWriteDescriptorSet writes[3]{};
        uint32_t writeCount = 0;

        if (updateEnvironment) {
            irradianceInfo = skybox.irradianceHandle.valid() ? skybox.irradianceHandle.descriptor() : IGPUResource::default_<GPUCubemap>().descriptor();
            prefilterInfo = skybox.prefilterHandle.valid() ? skybox.prefilterHandle.descriptor() : IGPUResource::default_<GPUCubemap>().descriptor();

            VkWriteDescriptorSet wI{};
            wI.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            wI.dstSet = environmentSet;
            wI.dstBinding = 0;
            wI.descriptorCount = 1;
            wI.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            wI.pImageInfo = &irradianceInfo;

            VkWriteDescriptorSet wP = wI;
            wP.dstBinding = 1;
            wP.pImageInfo = &prefilterInfo;

            writes[writeCount++] = wI;
            writes[writeCount++] = wP;
        }

        if (updateSkybox) {
            skyboxInfo = skybox.skyboxHandle.valid()
                             ? skybox.skyboxHandle.descriptor()
                             : IGPUResource::default_<GPUCubemap>().descriptor();

            VkWriteDescriptorSet wS{};
            wS.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            wS.dstSet = skyboxDescriptorSet;
            wS.dstBinding = 0;
            wS.descriptorCount = 1;
            wS.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            wS.pImageInfo = &skyboxInfo;

            writes[writeCount++] = wS;
        }

        if (writeCount > 0) {
            vkUpdateDescriptorSets(device.device(), writeCount, writes, 0, nullptr);
        }

        boundIrradianceHandle = skybox.irradianceHandle;
        boundIrradianceReady = irradianceReady;
        boundPrefilterHandle = skybox.prefilterHandle;
        boundPrefilterReady = prefilterReady;
        boundSkyboxHandle = skybox.skyboxHandle;
        boundSkyboxReady = skyboxReady;
    }

    VkPipelineDepthStencilStateCreateInfo GeometryStage::makeStencilWrite(uint8_t ref) {
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
