#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <entt/entity/registry.hpp>

#include "Device.hpp"
#include "Renderer.hpp"
#include "stage/RenderStage.hpp"

namespace Atlas {
    class RenderGraph {
    public:
        enum class Mode { MultiPass, MultiSubPass }; // TODO: implement MultiSubpass mode

        class Builder {
        public:
            explicit Builder(Device &device) : device(device) {
            }

            Builder &setExtent(uint32_t w, uint32_t h) {
                width_ = w;
                height_ = h;
                return *this;
            }

            Builder &addStage(std::unique_ptr<RenderStage> stage) {
                stages_.push_back(std::move(stage));
                return *this;
            }

            template<typename T, typename... Args>
            Builder &addStage(Args &&... args) {
                stages_.push_back(std::make_unique<T>(std::forward<Args>(args)...));
                return *this;
            }

            RenderGraph build(Mode mode);

        private:
            Device &device;
            uint32_t width_ = 0;
            uint32_t height_ = 0;
            std::vector<std::unique_ptr<RenderStage> > stages_;
        };

        RenderGraph(const RenderGraph &) = delete;
        RenderGraph &operator=(const RenderGraph &) = delete;
        RenderGraph(RenderGraph &&) = default;
        RenderGraph &operator=(RenderGraph &&) = delete;

        void build(entt::registry &registry);
        void render(FrameContext frameContext, VkDescriptorSet globalSet);

    private:
        friend class Builder;

        struct Barrier {
            std::string resourceName;
            bool isBuffer = false;
            VkImageLayout oldLayout;
            VkImageLayout newLayout;
            VkAccessFlags srcAccess;
            VkAccessFlags dstAccess;
            VkPipelineStageFlags srcStage;
            VkPipelineStageFlags dstStage;
            VkImageAspectFlags aspect;
            const bool requiresOwnershipAcquire = false; // keep this false for now until multiqueue support is added
        };

        struct Node {
            RenderStage *stage = nullptr;
            std::vector<std::string> inputs;
            std::vector<std::string> outputs;
            std::vector<Node *> dependsOn;
            std::vector<Barrier> barriersBeforeExec;
        };

        RenderGraph(Device &device, Mode mode, uint32_t width, uint32_t height);

        void bake();
        void bakeResources();
        void bakeBarriers();

        void emitBarriers(VkCommandBuffer cmd, const Node &node) const;

        static void topoSort(std::vector<Node> &nodes);
        static VkImageLayout writeLayoutFor(RenderStage::Resource::Type type);
        static VkImageLayout readLayoutFor(RenderStage::Resource::Type type);
        static VkAccessFlags writeAccessFor(VkImageLayout layout);
        static VkPipelineStageFlags writeStageFor(VkImageLayout layout);
        static VkImageAspectFlags aspectFor(RenderStage::Resource::Type type);
        static bool formatHasStencil(VkFormat format);

        Device &device;
        Mode mode;
        uint32_t width;
        uint32_t height;

        std::vector<std::unique_ptr<RenderStage> > stages_;
        std::vector<Node> nodes_;
        std::unordered_map<std::string, RenderStage::Resource> ownedResources_;
    };
} // namespace Atlas
