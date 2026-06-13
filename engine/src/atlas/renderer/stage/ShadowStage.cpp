#include "ShadowStage.hpp"

#include <array>
#include <cmath>
#include <stdexcept>

#include "CullingStage.hpp"
#include "core/Profiler.hpp"
#include "renderer/Camera.hpp"
#include "renderer/abstraction/GPUBuffer.hpp"
#include "renderer/resources/GPUMesh.hpp"

namespace Atlas {
    ShadowStage::ShadowStage(Device &device) : RenderStage(Queue::GRAPHICS), device(device) {
        objectDataSetLayout = DescriptorSetLayout::Builder(device)
                .addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 1)
                .build();

        pool = DescriptorPool::Builder(device)
                .setMaxSets(1)
                .addPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1)
                .build();

        if (!pool->allocateDescriptor(objectDataSetLayout->getDescriptorSetLayout(), objectDataSet)) {
            throw std::runtime_error("ShadowStage: failed to allocate object data set");
        }
    }

    ShadowStage::~ShadowStage() {
        vkDestroyFramebuffer(device.device(), framebuffer, nullptr);
        vkDestroyRenderPass(device.device(), renderPass, nullptr);
        vkDestroyPipelineLayout(device.device(), pipelineLayout, nullptr);
    }

    void ShadowStage::getDeclaredInputs(std::vector<std::string> &out) const {
        out.push_back("scene_objects");
        out.push_back("scene_draws");
    }

    void ShadowStage::getDeclaredOutputs(std::vector<Resource::Description> &out) const {
        auto map = Resource::Description::depth("shadow_map", VK_FORMAT_D32_SFLOAT);
        map.width = SHADOW_MAP_SIZE;
        map.height = SHADOW_MAP_SIZE;
        out.push_back(std::move(map));

        out.push_back(Resource::Description::hostVisibleStorageBuffer("shadow_data", sizeof(ShadowData)));
    }

    void ShadowStage::onResourcesCreated(const Context &ctx) {
        shadowMap = &ctx.resources.at("shadow_map").get().asImage();
        shadowDataBuffer = &ctx.resources.at("shadow_data").get().asBuffer();
        drawData = &ctx.resources.at("scene_draws").get().asCPUBuffer().as<RasterDrawData>();

        auto objInfo = ctx.resources.at("scene_objects").get().asBuffer().descriptorInfo();
        DescriptorWriter(*objectDataSetLayout, *pool)
                .writeBuffer(0, &objInfo)
                .overwrite(objectDataSet);

        createRenderPass();
        createFramebuffer();
        createPipeline();

        shadowDataBuffer->uploadData(&shadowData, sizeof(ShadowData));
    }

    void ShadowStage::onUpdate(entt::registry &registry) {
        ATLAS_PROFILE_SCOPE("ShadowStage::onUpdate");
        shadowData = {};

        const entt::entity cameraEntity = CullingStage::activeCamera(registry);

        // First visible directional light; matches the CullingStage partition order,
        // so it lands at lightIndex 0 in the light buffer.
        const LightComponent *directional = nullptr;
        for (auto entity: registry.view<TransformComponent, LightComponent>()) {
            if (const auto *node = registry.try_get<SceneNodeComponent>(entity); node && (node->deleted || !node->visible)) {
                continue;
            }
            // Same selection rule as CullingStage's partition, so this light is lights[0].
            const auto &light = registry.get<LightComponent>(entity);
            if (light.type == LightType::DIRECTIONAL) {
                directional = &light;
                break;
            }
        }

        if (cameraEntity != entt::null && directional != nullptr && glm::length(directional->direction) > 0.0f) {
            const Camera::Data cameraData = registry.get<CameraComponent>(cameraEntity).camera.getData();
            const glm::vec3 lightDir = glm::normalize(directional->direction);

            // Camera frustum corners in world space (Vulkan NDC, depth 0..1).
            const glm::mat4 invViewProj = glm::inverse(cameraData.viewProjection);
            std::array<glm::vec3, 8> corners{};
            for (int i = 0; i < 8; ++i) {
                const glm::vec4 p = invViewProj * glm::vec4(
                                        i & 1 ? 1.0f : -1.0f,
                                        i & 2 ? 1.0f : -1.0f,
                                        i & 4 ? 1.0f : 0.0f,
                                        1.0f);
                corners[i] = glm::vec3(p) / p.w;
            }

            // Truncate the frustum at SHADOW_DISTANCE so the map's texels aren't
            // spent on geometry too far away to read shadows from.
            const float t = glm::clamp((SHADOW_DISTANCE - cameraData.nearPlane) /
                                       glm::max(cameraData.farPlane - cameraData.nearPlane, 1e-4f), 0.0f, 1.0f);
            for (int i = 0; i < 4; ++i) {
                corners[i + 4] = corners[i] + (corners[i + 4] - corners[i]) * t;
            }

            glm::vec3 center{0.0f};
            for (const auto &corner: corners) center += corner;
            center /= 8.0f;

            // Bounding sphere keeps the ortho window size orientation-independent,
            // which makes texel snapping effective against shimmer.
            float radius = 0.0f;
            for (const auto &corner: corners) radius = glm::max(radius, glm::length(corner - center));
            radius = glm::max(radius, 1.0f);

            const glm::vec3 up = glm::abs(glm::dot(lightDir, glm::vec3(0.0f, -1.0f, 0.0f))) > 0.99f
                                     ? glm::vec3(1.0f, 0.0f, 0.0f)
                                     : glm::vec3(0.0f, -1.0f, 0.0f);

            Camera lightCamera;
            lightCamera.setViewDirection(center - lightDir * (2.0f * radius), lightDir, up);
            const glm::mat4 lightView = lightCamera.getViewMatrix();

            glm::vec3 centerLS = glm::vec3(lightView * glm::vec4(center, 1.0f));
            const float worldPerTexel = (2.0f * radius) / static_cast<float>(SHADOW_MAP_SIZE);
            centerLS.x = std::floor(centerLS.x / worldPerTexel) * worldPerTexel;
            centerLS.y = std::floor(centerLS.y / worldPerTexel) * worldPerTexel;

            // Pull the near plane toward the light so casters outside the frustum
            // still land in the map; corners sit at z in [radius, 3·radius].
            lightCamera.setOrthographicProjection(
                centerLS.x - radius, centerLS.x + radius,
                centerLS.y - radius, centerLS.y + radius,
                0.1f, 4.0f * radius);

            shadowData.lightViewProj = lightCamera.getProjectionMatrix() * lightView;
            shadowData.lightIndex = 0;
            shadowData.enabled = 1;
        }

        if (shadowDataBuffer) {
            shadowDataBuffer->uploadData(&shadowData, sizeof(ShadowData));
        }
    }

    void ShadowStage::record(VkCommandBuffer cmd, VkDescriptorSet) {
        ATLAS_PROFILE_SCOPE("ShadowStage::record");
        ATLAS_PROFILE_GPU_ZONE(device.gpuProfilerContext(), cmd, "ShadowStage");

        // Always run the pass so the map is cleared and transitioned to its
        // read layout even when there is nothing to cast shadows.
        VkClearValue clear{};
        clear.depthStencil = {1.0f, 0};

        VkRenderPassBeginInfo info{};
        info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        info.renderPass = renderPass;
        info.framebuffer = framebuffer;
        info.renderArea = {{0, 0}, {SHADOW_MAP_SIZE, SHADOW_MAP_SIZE}};
        info.clearValueCount = 1;
        info.pClearValues = &clear;

        vkCmdBeginRenderPass(cmd, &info, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport{};
        viewport.width = static_cast<float>(SHADOW_MAP_SIZE);
        viewport.height = static_cast<float>(SHADOW_MAP_SIZE);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        VkRect2D scissor{{0, 0}, {SHADOW_MAP_SIZE, SHADOW_MAP_SIZE}};
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        const auto &draws = std::get<0>(*drawData);
        if (shadowData.enabled != 0 && !draws.empty()) {
            ATLAS_PROFILE_GPU_ZONE(device.gpuProfilerContext(), cmd, "ShadowStage::Draws");
            pipeline->bind(cmd);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
                                    0, 1, &objectDataSet, 0, nullptr);
            vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                               sizeof(glm::mat4), &shadowData.lightViewProj);

            const void *boundMesh = nullptr;
            for (const auto &[mesh, objectIndex]: draws) {
                if (mesh.identity() != boundMesh) {
                    mesh.bind(cmd);
                    boundMesh = mesh.identity();
                }
                vkCmdDrawIndexed(cmd,
                                 static_cast<uint32_t>(mesh->indices().size()),
                                 1, 0, 0,
                                 objectIndex);
            }
        }

        vkCmdEndRenderPass(cmd);
    }

    void ShadowStage::createRenderPass() {
        VkAttachmentDescription depth{};
        depth.format = shadowMap->format();
        depth.samples = VK_SAMPLE_COUNT_1_BIT;
        depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        VkAttachmentReference depthRef{0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.pDepthStencilAttachment = &depthRef;

        std::array<VkSubpassDependency, 2> deps{};
        deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        deps[0].dstSubpass = 0;
        deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        deps[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        deps[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        // Make the map visible to the geometry pass's fragment shader; the graph
        // emits no barrier because the layouts already match.
        deps[1].srcSubpass = 0;
        deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        deps[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        deps[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        VkRenderPassCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        info.attachmentCount = 1;
        info.pAttachments = &depth;
        info.subpassCount = 1;
        info.pSubpasses = &subpass;
        info.dependencyCount = static_cast<uint32_t>(deps.size());
        info.pDependencies = deps.data();

        if (vkCreateRenderPass(device.device(), &info, nullptr, &renderPass) != VK_SUCCESS) {
            throw std::runtime_error("ShadowStage: failed to create render pass");
        }
    }

    void ShadowStage::createFramebuffer() {
        const VkImageView view = shadowMap->view(0);

        VkFramebufferCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        info.renderPass = renderPass;
        info.attachmentCount = 1;
        info.pAttachments = &view;
        info.width = SHADOW_MAP_SIZE;
        info.height = SHADOW_MAP_SIZE;
        info.layers = 1;

        if (vkCreateFramebuffer(device.device(), &info, nullptr, &framebuffer) != VK_SUCCESS) {
            throw std::runtime_error("ShadowStage: failed to create framebuffer");
        }
    }

    void ShadowStage::createPipeline() {
        const VkDescriptorSetLayout setLayout = objectDataSetLayout->getDescriptorSetLayout();

        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pushRange.offset = 0;
        pushRange.size = sizeof(glm::mat4);

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &setLayout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;

        if (vkCreatePipelineLayout(device.device(), &layoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
            throw std::runtime_error("ShadowStage: failed to create pipeline layout");
        }

        GraphicsPipelineConfigInfo cfg{};
        Pipeline::defaultGraphicsPipelineConfigInfo(cfg);
        cfg.bindingDescriptions = GPUMesh::Vertex::getBindingDescriptions();
        cfg.attributeDescriptions = GPUMesh::Vertex::getAttributeDescriptions();
        cfg.renderPass = renderPass;
        cfg.pipelineLayout = pipelineLayout;
        cfg.colorBlendInfo.attachmentCount = 0; // depth-only pass

        // Rasterizer depth bias against acne; PCF in the geometry pass softens the rest.
        cfg.rasterizationInfo.depthBiasEnable = VK_TRUE;
        cfg.rasterizationInfo.depthBiasConstantFactor = 1.25f;
        cfg.rasterizationInfo.depthBiasSlopeFactor = 1.75f;

        pipeline = std::make_unique<Pipeline>(
            device,
            "##engine/shaders/Shadow.vert.spv",
            "##engine/shaders/Shadow.frag.spv",
            cfg
        );
    }
} // namespace Atlas
