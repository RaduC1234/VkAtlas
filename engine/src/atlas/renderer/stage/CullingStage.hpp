#pragma once

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>

#include "IRenderStage.hpp"
#include "asset/AssetManager.hpp"
#include "entity/Object.hpp"
#include "renderer/Device.hpp"
#include "renderer/abstraction/Descriptors.hpp"

namespace Atlas {
    class CullingStage : public IRenderStage {
    public:
        static constexpr uint32_t MAX_OBJECTS = 10000;
        static constexpr uint32_t MAX_LIGHTS = 32;
        static constexpr uint32_t MAX_TEXTURES = 1024;

        struct Draw {
            AssetHandle<Mesh> mesh;
            uint32_t objectIndex = 0;
        };

        CullingStage(Device &device, AssetManager &assets);
        ~CullingStage() override = default;

        CullingStage(const CullingStage &) = delete;
        CullingStage &operator=(const CullingStage &) = delete;

        const std::vector<Draw> &draws() const { return draws_; }
        uint32_t lightCount() const { return lightCount_; }
        VkDescriptorSet bindlessTextureSet() const { return bindlessTextureSet_; }
        const DescriptorSetLayout &textureSetLayout() const { return *textureSetLayout_; }

        void getDeclaredOutputs(std::vector<Resource::Description> &out) const override;
        void getDeclaredInputs(std::vector<std::string> &out) const override;
        void onResourcesCreated(const Context &ctx) override;
        void onUpdate(entt::registry &registry) override;

        void record(VkCommandBuffer, VkDescriptorSet) override {
        }

    private:
        struct GPUObjectData {
            glm::mat4 modelMatrix;
            glm::mat4 normalMatrix;
            glm::uvec4 textureIndices{0};
            glm::vec4 baseColor{1.0f};
        };

        struct Light {
            uint32_t type{static_cast<uint32_t>(LightType::SPOT)};
            float intensity{1.0f};
            float range{0.0f};
            float innerConeAngle{0.0f};
            glm::vec3 color{1.0f};
            float outerConeAngle{glm::radians(45.0f)};
            glm::vec3 position{0.0f};
            float width{0.0f};
            glm::vec3 direction{0.0f, -1.0f, 0.0f};
            float height{0.0f};
        };

        void connectSignals(entt::registry &registry);
        void rebuild(entt::registry &registry);
        uint32_t registerTexture(AssetHandle<Texture> handle);
        void flushPendingTextureWrites();
        void markDirty(entt::registry &reg, entt::entity e) {
            if (reg.any_of<ModelComponent, LightComponent>(e)) dirty_ = true;
        }

        Device &device_;
        AssetManager &assets_;

        bool signalsConnected_ = false;
        bool dirty_ = true;

        GPUBuffer *objectBuffer_ = nullptr;
        GPUBuffer *lightBuffer_ = nullptr;

        std::vector<Draw> draws_;
        std::vector<GPUObjectData> objectData_;
        std::vector<Light> lightData_;
        uint32_t lightCount_ = 0;

        std::unique_ptr<DescriptorPool> texturePool_;
        std::unique_ptr<DescriptorSetLayout> textureSetLayout_;
        VkDescriptorSet bindlessTextureSet_ = VK_NULL_HANDLE;
        uint32_t nextTextureSlot_ = 1;
        std::unordered_map<AssetHandle<Texture>, uint32_t> handleToTextureSlot_;

        std::vector<VkDescriptorImageInfo> pendingTextureInfos_;
        std::vector<VkWriteDescriptorSet>  pendingTextureWrites_;
    };
} // namespace Atlas
