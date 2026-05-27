#include "CullingStage.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/gtc/matrix_inverse.hpp>

#include "core/Log.hpp"
#include "renderer/abstraction/GPUBuffer.hpp"

namespace Atlas {
    CullingStage::CullingStage(Device &, AssetManager &assets)
        : IRenderStage(Queue::GRAPHICS), assets_(assets) {
    }

    void CullingStage::getDeclaredOutputs(std::vector<Resource::Description> &out) const {
        out.push_back(Resource::Description::hostVisibleStorageBuffer(
            "scene_objects", sizeof(GPUObjectData) * MAX_OBJECTS));
        out.push_back(Resource::Description::hostVisibleStorageBuffer(
            "scene_lights", sizeof(LightBufferLayout)));
        out.push_back(Resource::Description::cpuBuffer<RasterDrawData>("scene_draws"));
    }

    void CullingStage::getDeclaredInputs(std::vector<std::string> &out) const {}

    void CullingStage::onResourcesCreated(const Context &ctx) {
        objectBuffer_ = &ctx.resources.at("scene_objects").get().asBuffer();
        lightBuffer_  = &ctx.resources.at("scene_lights").get().asBuffer();
        drawData_ = &ctx.resources.at("scene_draws").get().asCPUBuffer().as<RasterDrawData>();
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

        registry.on_update<SceneNodeComponent>().connect<&CullingStage::markDirty>(this);
        registry.on_update<TransformComponent>().connect<&CullingStage::markDirty>(this);
        registry.on_construct<MaterialComponent>().connect<&CullingStage::markDirty>(this);
        registry.on_destroy<MaterialComponent>().connect<&CullingStage::markDirty>(this);
        registry.on_update<MaterialComponent>().connect<&CullingStage::markDirty>(this);
    }

    void CullingStage::rebuild(entt::registry &registry) {
        draws_.clear();
        objectData_.clear();
        lightData_.clear();
        lightCount_ = 0;
        bool anyUnready = false;

        for (auto entity: registry.view<TransformComponent, MaterialComponent, ModelComponent>()) {
            if (const auto *node = registry.try_get<SceneNodeComponent>(entity); node && (node->deleted || !node->visible)) {
                continue;
            }

            auto &transform = registry.get<TransformComponent>(entity);
            auto &materialComponent = registry.get<MaterialComponent>(entity);
            auto &model     = registry.get<ModelComponent>(entity);

            if (!model.meshHandle.valid() || !model.meshHandle.isReady()) {
                if (model.meshHandle.valid()) anyUnready = true;
                continue;
            }

            const Material fallbackMaterial{};
            const Material *material = materialComponent.materialHandle.get();
            if (!material) {
                material = &fallbackMaterial;
            }

            const uint32_t albedoIdx = registerTexture(material->baseColorTexture);
            const uint32_t normalIdx = registerTexture(material->normalTexture);
            const uint32_t mrIdx     = registerTexture(material->metallicRoughnessTexture);
            const uint32_t aoIdx     = registerTexture(material->occlusionTexture);

            const glm::mat4 m = transform.mat4();
            const GPUObjectData data{
                .modelMatrix    = m,
                .normalMatrix   = glm::mat4(glm::inverseTranspose(glm::mat3(m))),
                .textureIndices = glm::uvec4(albedoIdx, normalIdx, mrIdx, aoIdx),
                .baseColor      = material->baseColor,
            };

            if (material->baseColor.w >= 1.0f && material->alphaMode != AlphaMode::BLEND) {
                const uint32_t idx = static_cast<uint32_t>(objectData_.size());
                objectData_.push_back(data);
                draws_.push_back({model.meshHandle, idx});
            }
        }

        for (auto entity: registry.view<TransformComponent, LightComponent>()) {
            if (const auto *node = registry.try_get<SceneNodeComponent>(entity); node && (node->deleted || !node->visible)) {
                continue;
            }

            if (lightData_.size() >= MAX_LIGHTS) {
                AT_WARN("CullingStage: MAX_LIGHTS reached");
                break;
            }

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
        }
        lightCount_ = static_cast<uint32_t>(lightData_.size());

        if (drawData_) {
            auto &[draws, textures, textureRevision] = *drawData_;
            draws = draws_;
            textures.clear();
            textures.reserve(handleToTextureSlot_.size());
            for (const auto &[handle, slot]: handleToTextureSlot_) {
                textures.push_back({handle, slot});
            }
            ++textureRevision;
        }

        if (!objectData_.empty() && objectBuffer_) {
            objectBuffer_->uploadData(objectData_.data(), sizeof(GPUObjectData) * objectData_.size());
        }
        if (lightBuffer_) {
            LightBufferLayout lightBufferData{};
            lightBufferData.count = lightCount_;
            for (size_t i = 0; i < lightData_.size(); ++i) {
                lightBufferData.lights[i] = lightData_[i];
            }
            lightBuffer_->uploadData(&lightBufferData, sizeof(LightBufferLayout));
        }

        // Retry next frame if any meshes were still uploading to the GPU.
        if (anyUnready) dirty_ = true;
    }

    uint32_t CullingStage::registerTexture(AssetHandle<Texture> handle) {
        if (!handle.valid()) return 0;

        auto [it, inserted] = handleToTextureSlot_.emplace(handle, nextTextureSlot_);
        if (!inserted) {
            return it->second;
        }

        if (nextTextureSlot_ >= MAX_TEXTURES) {
            throw std::runtime_error("CullingStage: exceeded maximum bindless texture count");
        }

        const uint32_t slot = nextTextureSlot_++;
        it->second = slot;

        return slot;
    }
} // namespace Atlas
