#include "CullingStage.hpp"

#include <algorithm>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/gtc/matrix_inverse.hpp>

#include "core/Log.hpp"
#include "core/Profiler.hpp"
#include "renderer/abstraction/GPUBuffer.hpp"

namespace Atlas {
    CullingStage::CullingStage(Device &, AssetManager &assets)
        : RenderStage(Queue::GRAPHICS), assets_(assets) {
    }

    void CullingStage::getDeclaredOutputs(std::vector<Resource::Description> &out) const {
        out.push_back(Resource::Description::hostVisibleStorageBuffer("scene_objects", sizeof(GPUObjectData) * MAX_OBJECTS));
        out.push_back(Resource::Description::hostVisibleStorageBuffer("scene_lights", sizeof(LightBufferLayout)));
        out.push_back(Resource::Description::cpuBuffer<RasterDrawData>("scene_draws"));
    }

    void CullingStage::getDeclaredInputs(std::vector<std::string> &out) const {}

    void CullingStage::onResourcesCreated(const Context &ctx) {
        objectBuffer_ = &ctx.resources.at("scene_objects").get().asBuffer();
        lightBuffer_  = &ctx.resources.at("scene_lights").get().asBuffer();
        drawData_ = &ctx.resources.at("scene_draws").get().asCPUBuffer().as<RasterDrawData>();
    }

    void CullingStage::onUpdate(entt::registry &registry) {
        ATLAS_PROFILE_SCOPE("CullingStage::onUpdate");
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

        registry.on_construct<CameraComponent>().connect<&CullingStage::markDirty>(this);
        registry.on_destroy<CameraComponent>().connect<&CullingStage::markDirty>(this);
        registry.on_update<CameraComponent>().connect<&CullingStage::markDirty>(this);

        registry.on_update<SceneNodeComponent>().connect<&CullingStage::markDirty>(this);
        registry.on_update<TransformComponent>().connect<&CullingStage::markDirty>(this);
        registry.on_construct<MaterialComponent>().connect<&CullingStage::markDirty>(this);
        registry.on_destroy<MaterialComponent>().connect<&CullingStage::markDirty>(this);
        registry.on_update<MaterialComponent>().connect<&CullingStage::markDirty>(this);
    }

    void CullingStage::rebuild(entt::registry &registry) {
        ATLAS_PROFILE_SCOPE("CullingStage::rebuild");
        draws_.clear();
        objectData_.clear();
        lightData_.clear();
        lightCount_ = 0;
        directionalCount_ = 0;
        bool anyUnready = false;

        // Build frustum from the active camera.
        std::array<glm::vec4, 6> frustumPlanes{};
        bool hasFrustum = false;
        if (const entt::entity cam = activeCamera(registry); cam != entt::null) {
            const Camera::Data cameraData = registry.get<CameraComponent>(cam).camera.getData();
            for (int i = 0; i < 6; ++i) frustumPlanes[i] = cameraData.frustumPlanes[i];
            hasFrustum = true;
        }

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

            const glm::mat4 m = transform.mat4();

            // Frustum cull: skip objects whose bounding sphere is entirely outside the frustum.
            if (hasFrustum) {
                const auto [localCenter, localRadius] = meshBounds(model.meshHandle);
                const glm::vec3 worldCenter = glm::vec3(m * glm::vec4(localCenter, 1.0f));
                const float maxScale = glm::max(glm::length(glm::vec3(m[0])),
                                                glm::max(glm::length(glm::vec3(m[1])),
                                                         glm::length(glm::vec3(m[2]))));
                if (!sphereInFrustum(frustumPlanes, worldCenter, localRadius * maxScale)) continue;
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

            const GPUObjectData data{
                .modelMatrix    = m,
                .normalMatrix   = glm::mat4(glm::inverseTranspose(glm::mat3(m))),
                .textureIndices = glm::uvec4(albedoIdx, normalIdx, mrIdx, aoIdx),
                .baseColor      = material->baseColor,
                .materialFactors = glm::vec4(material->metallic, material->roughness, material->alphaCutoff, 0.0f),
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
            const bool directional = light.type == LightType::DIRECTIONAL;
            const float range = effectiveRange(light);

            // Positional lights outside the frustum can't touch any cluster.
            if (!directional && hasFrustum && !sphereInFrustum(frustumPlanes, transform.translation, range)) {
                continue;
            }

            // Keep the rect frame orthonormal so the shader can derive up = cross(direction, right).
            glm::vec3 direction = light.direction;
            if (glm::length(direction) > 0.0f) direction = glm::normalize(direction);
            glm::vec3 right = light.rectRight - direction * glm::dot(light.rectRight, direction);
            right = glm::length(right) > 1e-4f ? glm::normalize(right) : glm::vec3(1.0f, 0.0f, 0.0f);

            lightData_.push_back(Light{
                static_cast<uint32_t>(light.type),
                light.intensity,
                range,
                light.innerConeAngle,
                light.color,
                light.outerConeAngle,
                transform.translation,
                light.width,
                direction,
                light.height,
                right,
            });
        }

        // Directional lights first: the geometry pass evaluates them globally while
        // the cluster pass only bins what comes after directionalCount.
        const auto positionalBegin = std::stable_partition(lightData_.begin(), lightData_.end(), [](const Light &light) {
            return light.type == static_cast<uint32_t>(LightType::DIRECTIONAL);
        });
        directionalCount_ = static_cast<uint32_t>(std::distance(lightData_.begin(), positionalBegin));
        lightCount_ = static_cast<uint32_t>(lightData_.size());

        // Group draws by mesh so the geometry pass can skip redundant vertex/index binds.
        std::stable_sort(draws_.begin(), draws_.end(), [](const RasterDraw &a, const RasterDraw &b) {
            return a.first.identity() < b.first.identity();
        });

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
            lightBufferData.directionalCount = directionalCount_;
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

    entt::entity CullingStage::activeCamera(entt::registry &registry) {
        // Prefer the editor camera.
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

    std::pair<glm::vec3, float> CullingStage::computeMeshBounds(const Mesh &mesh) {
        const auto &verts = mesh.vertices();
        if (verts.empty()) return {glm::vec3(0.0f), 0.0f};

        glm::vec3 center{0.0f};
        for (const auto &v: verts) center += v.position;
        center /= static_cast<float>(verts.size());

        float radius = 0.0f;
        for (const auto &v: verts) radius = glm::max(radius, glm::length(v.position - center));

        return {center, radius};
    }

    std::pair<glm::vec3, float> CullingStage::meshBounds(const AssetHandle<Mesh> &handle) {
        const void *key = handle.identity();
        const auto it = meshBoundsCache_.find(key);
        if (it != meshBoundsCache_.end()) return it->second;

        // Conservative fallback if the CPU asset is somehow unavailable.
        if (!handle.get()) {
            const auto fallback = std::make_pair(glm::vec3(0.0f), 1.0f);
            meshBoundsCache_.emplace(key, fallback);
            return fallback;
        }

        auto bounds = computeMeshBounds(*handle.get());
        meshBoundsCache_.emplace(key, bounds);
        return bounds;
    }

    bool CullingStage::sphereInFrustum(const std::array<glm::vec4, 6> &planes, glm::vec3 center, float radius) {
        for (const auto &plane: planes) {
            if (glm::dot(glm::vec3(plane), center) + plane.w < -radius) return false;
        }
        return true;
    }

    float CullingStage::effectiveRange(const LightComponent &light) {
        if (light.type == LightType::RECT) {
            // Rect attenuation is area / d² with no window, so cull where the
            // unshadowed irradiance drops below ~1% of the light's intensity.
            const float area = glm::max(light.width * light.height, 1e-4f);
            return glm::sqrt(area * glm::max(light.intensity, 0.0f) * 100.0f) + 1.0f;
        }
        return light.range == 0.0f ? 20.0f : light.range;
    }
} // namespace Atlas
