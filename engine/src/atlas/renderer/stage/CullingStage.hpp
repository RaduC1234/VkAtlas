#pragma once
#include "IRenderStage.hpp"
#include "../Device.hpp"
#include "../abstraction/Descriptors.hpp"

namespace Atlas {
    class CullingStage : public IRenderStage {
    public:
        CullingStage(Device& device, const DescriptorSetLayout& globalSetLayout);
        ~CullingStage() override;

        CullingStage(const CullingStage&) = delete;
        CullingStage& operator=(const CullingStage&) = delete;

        void getDeclaredOutputs(std::vector<Resource::Description> &out) const override;
        void getDeclaredInputs(std::vector<std::string> &out) const override;
        void onResourcesCreated(const std::unordered_map<std::string, std::reference_wrapper<Resource>> &resources) override;

        void onSceneChanged(entt::registry &registry) override;
        void record(VkCommandBuffer cmd, VkDescriptorSet globalSet) override;

    private:
        Device& device;
        const DescriptorSetLayout &globalSetLayout;
    };
} // Atlas

