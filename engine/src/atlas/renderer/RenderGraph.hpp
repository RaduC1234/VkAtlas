#pragma once
#include "Device.hpp"
#include "stage/IRenderStage.hpp"

namespace Atlas {
    class RenderGraph {
    public:
        enum class Mode {
            MultiPass,
            MultiSubPass,
            Auto
        };

        class Builder {
        public:
            Builder(Device &device);

            Builder &addStage(std::unique_ptr<IRenderStage> stage);

            template<typename T, typename... Args>
            Builder &addStage(Args &&... args) {
                stages_.push_back(std::make_unique<T>(std::forward<Args>(args)...));
                return *this;
            }

            [[nodiscard]] RenderGraph build(uint32_t extentWidth, uint32_t extentLenght, Mode renderMode = Mode::Auto);

        private:
            Device &device;
            Mode mode_ = Mode::MultiPass;
            uint32_t width_ = 0;
            uint32_t height_ = 0;
            std::vector<std::unique_ptr<IRenderStage> > stages_;
        };

        RenderGraph(const RenderGraph&)            = delete;
        RenderGraph& operator=(const RenderGraph&) = delete;
        RenderGraph(RenderGraph&&)                 = default;
        RenderGraph& operator=(RenderGraph&&)      = default;

        void render(VkCommandBuffer cmd, VkDescriptorSet globalSet);

    private:
        friend class Builder;

        RenderGraph(Device &device, Mode mode) : device(device), mode(mode) {
        }

        void bake(uint32_t w, uint32_t h);

        Mode mode;
        Device &device;
        std::vector<std::unique_ptr<IRenderStage> > stages_;
    };
}
