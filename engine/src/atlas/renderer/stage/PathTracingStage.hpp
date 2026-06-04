#pragma once
#include <cstdint>
#include <vector>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

#include <entt/signal/sigh.hpp>

#include "RenderStage.hpp"
#include "asset/AssetManager.hpp"
#include "renderer/Camera.hpp"
#include "renderer/abstraction/Descriptors.hpp"
#include "renderer/abstraction/Pipeline.hpp"

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

    class PathTracingStage : public RenderStage {
    public:
        PathTracingStage(Device &device, AssetManager &assets, const DescriptorSetLayout &globalSetLayout);
        ~PathTracingStage();

        void getDeclaredOutputs(std::vector<Resource::Description> &out) const override;
        void getDeclaredInputs(std::vector<std::string> &out) const override;
        void onResourcesCreated(const Context &ctx) override;
        void onUpdate(entt::registry &registry) override;
        void record(VkCommandBuffer cmd, VkDescriptorSet globalSet) override;

        void reset();

    private:
        void createDescriptors();
        void createPipelineLayout();
        void createPipeline();
        void buildSBT();
        void updateDescriptorSet();
        void onCameraUpdated(entt::registry &registry, entt::entity entity);
        void onCameraDestroyed(entt::registry &registry, entt::entity entity);
        entt::entity activeCamera(entt::registry &registry) const;

        uint64_t geometrySignature(entt::registry &registry, bool &waitingForMeshes) const;
        uint64_t transformSignature(entt::registry &registry) const;
        uint64_t textureReadinessSignature() const;
        uint32_t registerTexture(AssetHandle<Texture> handle);
        static bool cameraDataChanged(const Camera::Data &lhs, const Camera::Data &rhs);
        uint32_t alignUp(uint32_t size, uint32_t alignment) const;

        static constexpr uint32_t MAX_TEXTURES = 512;
        static constexpr uint32_t MAX_OBJECTS  = 1024;
        static constexpr uint32_t MAX_LIGHTS   = 32;
        static constexpr uint32_t MAX_BOUNCES  = 6;

        Device &device;
        AssetManager &assets;
        const DescriptorSetLayout &globalSetLayout;

        GPUImage *outputImage   = nullptr;
        GPUImage *geometryDepth = nullptr;
        AccelerationStructure tlas_;

        std::unique_ptr<GPUBuffer> objectBuffer;
        std::unique_ptr<GPUBuffer> lightBuffer;
        std::unique_ptr<GPUBuffer> vertexBuffer;
        std::unique_ptr<GPUBuffer> indexBuffer;
        std::unique_ptr<GPUImage>  accumulationImage;

        std::unique_ptr<DescriptorSetLayout> ptSetLayout;
        std::unique_ptr<DescriptorPool>      ptPool;
        VkDescriptorSet ptSet             = VK_NULL_HANDLE;
        VkDescriptorSet bindlessTextureSet = VK_NULL_HANDLE;
        VkSampler       envSampler        = VK_NULL_HANDLE;
        AssetHandle<Cubemap> envHandle;
        bool envReady = false;

        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        std::unique_ptr<Pipeline> pipeline;

        std::unique_ptr<GPUBuffer>          sbtBuffer;
        VkStridedDeviceAddressRegionKHR sbtRaygen{};
        VkStridedDeviceAddressRegionKHR sbtMiss{};
        VkStridedDeviceAddressRegionKHR sbtHit{};
        VkStridedDeviceAddressRegionKHR sbtCallable{};

        uint32_t currentSample = 0;
        uint32_t frameIndex    = 0;
        uint32_t objectCount   = 0;
        uint32_t lightCount    = 0;
        bool active       = true;
        bool hasCameraData = false;
        bool geometryBuilt = false;
        bool sceneBuilt    = false;

        uint64_t lastGeometrySignature          = 0;
        uint64_t lastTransformSignature         = 0;
        uint64_t lastTextureReadinessSignature  = 0;

        Camera::Data lastCameraData{};
        entt::scoped_connection cameraConstructConnection;
        entt::scoped_connection cameraUpdateConnection;
        entt::scoped_connection cameraDestroyConnection;

        std::unordered_map<AssetHandle<Texture>, uint32_t> handleToSlot;
        uint32_t nextTextureSlot = 1;

        std::vector<PTObjectData> cachedObjects_;
    };
}
