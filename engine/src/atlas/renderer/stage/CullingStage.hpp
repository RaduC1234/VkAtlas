#pragma once

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <array>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "RenderStage.hpp"
#include "asset/AssetManager.hpp"
#include "entity/Object.hpp"
#include "renderer/Device.hpp"

namespace Atlas {
    class CullingStage : public RenderStage {
    public:
        static constexpr uint32_t MAX_OBJECTS = 10000;
        static constexpr uint32_t MAX_LIGHTS = 256;
        static constexpr uint32_t MAX_TEXTURES = 1024;

        CullingStage(Device &device, AssetManager &assets);
        ~CullingStage() override = default;

        CullingStage(const CullingStage &) = delete;
        CullingStage &operator=(const CullingStage &) = delete;

        uint32_t lightCount() const { return lightCount_; }

        // Shared camera-resolution policy: editor camera wins, transient cameras lose.
        static entt::entity activeCamera(entt::registry &registry);

        void getDeclaredOutputs(std::vector<Resource::Description> &out) const override;
        void getDeclaredInputs(std::vector<std::string> &out) const override;
        void onResourcesCreated(const Context &ctx) override;
        void onUpdate(entt::registry &registry) override;

        void record(VkCommandBuffer, VkDescriptorSet) override {
        }

    private:
        using RasterDraw = std::pair<AssetHandle<Mesh>, uint32_t>;
        using RasterTextureBinding = std::pair<AssetHandle<Texture>, uint32_t>;
        using RasterDrawData = std::tuple<std::vector<RasterDraw>,std::vector<RasterTextureBinding>,uint32_t>;

        struct GPUObjectData {
            glm::mat4 modelMatrix;
            glm::mat4 normalMatrix;
            glm::uvec4 textureIndices{0};
            glm::vec4 baseColor{1.0f};
            glm::vec4 materialFactors{0.0f, 0.5f, 0.5f, 0.0f};
        };

        struct Light {
            uint32_t type{static_cast<uint32_t>(LightType::SPOT)};
            float intensity{1.0f};
            float range{0.0f}; // also the cluster cull radius; rebuild() guarantees > 0 for positional lights
            float innerConeAngle{0.0f};
            glm::vec3 color{1.0f};
            float outerConeAngle{glm::radians(45.0f)};
            glm::vec3 position{0.0f};
            float width{0.0f};
            glm::vec3 direction{0.0f, -1.0f, 0.0f};
            float height{0.0f};
            glm::vec3 right{1.0f, 0.0f, 0.0f}; // rect frame; the shader derives up = cross(direction, right)
            float _pad{0.0f};
        };

        struct LightBufferLayout {
            uint32_t count{0};
            uint32_t directionalCount{0}; // directional lights are sorted first and skip clustering
            uint32_t _pad[2]{};
            Light lights[MAX_LIGHTS]{};
        };

        void connectSignals(entt::registry &registry);
        void rebuild(entt::registry &registry);
        uint32_t registerTexture(AssetHandle<Texture> handle);
        void markDirty(entt::registry &, entt::entity) {
            dirty_ = true;
        }

        static std::pair<glm::vec3, float> computeMeshBounds(const Mesh &mesh);
        std::pair<glm::vec3, float> meshBounds(const AssetHandle<Mesh> &handle);
        static bool sphereInFrustum(const std::array<glm::vec4, 6> &planes, glm::vec3 center, float radius);
        static float effectiveRange(const LightComponent &light);

        AssetManager &assets_;

        bool signalsConnected_ = false;
        bool dirty_ = true;

        GPUBuffer *objectBuffer_ = nullptr;
        GPUBuffer *lightBuffer_ = nullptr;
        RasterDrawData *drawData_ = nullptr;

        std::vector<RasterDraw> draws_;
        std::vector<GPUObjectData> objectData_;
        std::vector<Light> lightData_;
        uint32_t lightCount_ = 0;
        uint32_t directionalCount_ = 0;

        uint32_t nextTextureSlot_ = 1;
        std::unordered_map<AssetHandle<Texture>, uint32_t> handleToTextureSlot_;
        std::unordered_map<const void *, std::pair<glm::vec3, float>> meshBoundsCache_;
    };
} // namespace Atlas
