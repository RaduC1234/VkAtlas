#include "CullingStage.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/gtc/matrix_inverse.hpp>

#include "core/Log.hpp"
#include "renderer/abstraction/GPUBuffer.hpp"

namespace Atlas {
    CullingStage::CullingStage(Device &device, AssetManager &assets)
        : IRenderStage(Queue::GRAPHICS), device_(device), assets_(assets) {
        textureSetLayout_ = DescriptorSetLayout::Builder(device_)
            .addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, MAX_TEXTURES)
            .setBindingFlags(0, VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT)
            .setLayoutFlags(VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT)
            .build();

        texturePool_ = DescriptorPool::Builder(device_)
            .setMaxSets(1)
            .addPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_TEXTURES)
            .setPoolFlags(VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT)
            .build();

        if (!texturePool_->allocateDescriptor(textureSetLayout_->getDescriptorSetLayout(), bindlessTextureSet_)) {
            throw std::runtime_error("CullingStage: failed to allocate bindless texture set");
        }

        VkDescriptorImageInfo defaultInfo = IGPUResource::default_<GPUTexture>().descriptor();
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = bindlessTextureSet_;
        write.dstBinding = 0;
        write.dstArrayElement = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &defaultInfo;
        vkUpdateDescriptorSets(device_.device(), 1, &write, 0, nullptr);
    }

    void CullingStage::getDeclaredOutputs(std::vector<Resource::Description> &out) const {
        out.push_back(Resource::Description::hostVisibleStorageBuffer(
            "scene_objects", sizeof(GPUObjectData) * MAX_OBJECTS));
        out.push_back(Resource::Description::hostVisibleStorageBuffer(
            "scene_lights", sizeof(Light) * MAX_LIGHTS));
    }

    void CullingStage::getDeclaredInputs(std::vector<std::string> &out) const {}

    void CullingStage::onResourcesCreated(const Context &ctx) {
        objectBuffer_ = &ctx.resources.at("scene_objects").get().asBuffer();
        lightBuffer_  = &ctx.resources.at("scene_lights").get().asBuffer();
    }

    void CullingStage::onUpdate(entt::registry &registry) {
        if (!signalsConnected_) {
            connectSignals(registry);
            signalsConnected_ = true;
        }

        if (dirty_) {
            dirty_ = false;
            rebuild(registry);
        }
    }

    void CullingStage::connectSignals(entt::registry &registry) {
        registry.on_construct<ModelComponent>().connect<&CullingStage::markDirty>(this);
        registry.on_destroy<ModelComponent>().connect<&CullingStage::markDirty>(this);
        registry.on_update<ModelComponent>().connect<&CullingStage::markDirty>(this);

        registry.on_construct<LightComponent>().connect<&CullingStage::markDirty>(this);
        registry.on_destroy<LightComponent>().connect<&CullingStage::markDirty>(this);
        registry.on_update<LightComponent>().connect<&CullingStage::markDirty>(this);

        registry.on_update<TransformComponent>().connect<&CullingStage::markDirty>(this);
        registry.on_update<MaterialComponent>().connect<&CullingStage::markDirty>(this);
    }

    void CullingStage::rebuild(entt::registry &registry) {
        draws_.clear();
        objectData_.clear();
        lightData_.clear();
        lightCount_ = 0;
        bool anyUnready = false;

        for (auto entity: registry.view<TransformComponent, MaterialComponent, ModelComponent>()) {
            auto &transform = registry.get<TransformComponent>(entity);
            auto &material  = registry.get<MaterialComponent>(entity);
            auto &model     = registry.get<ModelComponent>(entity);

            if (!model.meshHandle.valid() || !model.meshHandle.isReady()) {
                if (model.meshHandle.valid()) anyUnready = true;
                continue;
            }

            const uint32_t albedoIdx = registerTexture(material.albedoTexture);
            const uint32_t normalIdx = registerTexture(material.normalMap);
            const uint32_t mrIdx     = registerTexture(material.metallicRoughnessMap);
            const uint32_t aoIdx     = registerTexture(material.ambientOcclusion);

            const glm::mat4 m = transform.mat4();
            const GPUObjectData data{
                .modelMatrix    = m,
                .normalMatrix   = glm::mat4(glm::inverseTranspose(glm::mat3(m))),
                .textureIndices = glm::uvec4(albedoIdx, normalIdx, mrIdx, aoIdx),
                .baseColor      = material.baseColor,
            };

            if (material.baseColor.w >= 1.0f) {
                const uint32_t idx = static_cast<uint32_t>(objectData_.size());
                objectData_.push_back(data);
                draws_.push_back({model.meshHandle, idx});
            }
        }

        for (auto entity: registry.view<TransformComponent, LightComponent>()) {
            auto [transform, light] = registry.get<TransformComponent, LightComponent>(entity);
            lightData_.push_back(Light{
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
            ++lightCount_;
        }

        flushPendingTextureWrites();

        if (!objectData_.empty() && objectBuffer_) {
            objectBuffer_->uploadData(objectData_.data(), sizeof(GPUObjectData) * objectData_.size());
        }
        if (!lightData_.empty() && lightBuffer_) {
            lightBuffer_->uploadData(lightData_.data(), sizeof(Light) * lightData_.size());
        }

        // Retry next frame if any meshes were still uploading to the GPU.
        if (anyUnready) dirty_ = true;
    }

    void CullingStage::flushPendingTextureWrites() {
        if (pendingTextureWrites_.empty()) return;

        // Recompute pImageInfo pointers — pendingTextureInfos_ may have reallocated
        // during the entity loop as new textures were appended, invalidating any
        // &back() pointers captured during registerTexture().
        for (size_t i = 0; i < pendingTextureWrites_.size(); ++i) {
            pendingTextureWrites_[i].pImageInfo = &pendingTextureInfos_[i];
        }
        vkUpdateDescriptorSets(device_.device(),
            static_cast<uint32_t>(pendingTextureWrites_.size()),
            pendingTextureWrites_.data(), 0, nullptr);
        pendingTextureWrites_.clear();
        pendingTextureInfos_.clear();
    }

    uint32_t CullingStage::registerTexture(AssetHandle<Texture> handle) {
        if (!handle.valid()) return 0;

        auto [it, inserted] = handleToTextureSlot_.emplace(handle, nextTextureSlot_);
        if (!inserted) {
            // Already registered — re-register the bindless slot so GPUTexture::updateBindlessSlot
            // can refresh the descriptor if the texture finished uploading since last rebuild.
            handle.registerBindlessSlot(device_.device(), bindlessTextureSet_, 0, it->second);
            return it->second;
        }

        if (nextTextureSlot_ >= MAX_TEXTURES) {
            throw std::runtime_error("CullingStage: exceeded maximum bindless texture count");
        }

        const uint32_t slot = nextTextureSlot_++;
        it->second = slot;

        // Register the slot so GPUTexture::updateBindlessSlot() writes the real descriptor
        // once the upload completes (the immediate write below uses a default if not ready yet).
        handle.registerBindlessSlot(device_.device(), bindlessTextureSet_, 0, slot);

        pendingTextureInfos_.push_back(handle.descriptor());
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = bindlessTextureSet_;
        write.dstBinding = 0;
        write.dstArrayElement = slot;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = nullptr; // fixed up in flushPendingTextureWrites()
        pendingTextureWrites_.push_back(write);

        return slot;
    }
} // namespace Atlas
