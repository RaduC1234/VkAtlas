#include "CullingStage.hpp"

namespace Atlas {
    CullingStage::CullingStage(Device &device, const DescriptorSetLayout &globalSetLayout) : IRenderStage(Queue::COMPUTE), device(device), globalSetLayout(globalSetLayout) {
    }

    CullingStage::~CullingStage() {
    }

    void CullingStage::getDeclaredOutputs(std::vector<Resource::Description> &out) const {
    }

    void CullingStage::getDeclaredInputs(std::vector<std::string> &out) const {
    }

    void CullingStage::onResourcesCreated(const std::unordered_map<std::string, std::reference_wrapper<Resource>> &resources) {

    }

    void CullingStage::onSceneChanged(entt::registry &registry) {

    }

    void CullingStage::record(VkCommandBuffer cmd, VkDescriptorSet globalSet) {

    }
} // Atlas