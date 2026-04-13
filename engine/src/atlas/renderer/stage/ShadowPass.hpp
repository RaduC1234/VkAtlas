#pragma once
#include <entt/entity/registry.hpp>

#include "IRenderStage.hpp"

namespace Atlas {
    class ShadowPass : IRenderStage {
    public:
        ~ShadowPass() override;
        void begin(VkCommandBuffer cmd) override;
        void end(VkCommandBuffer cmd) override;
        void barrier(VkCommandBuffer cmd) override;
        void getDeclaredOutputs(std::vector<StageResource> &out) const override;

        void build(entt::registry &registry);
        void record(VkCommandBuffer cmd, VkDescriptorSet globalSet) const;

    };
} // Atlas
