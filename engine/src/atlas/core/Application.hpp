#pragma once

#include "Window.hpp"
#include "renderer/Device.hpp"
#include "renderer/Model.hpp"
#include "renderer/Pipeline.hpp"
#include "renderer/SwapChain.hpp"

#include <memory>

namespace Atlas {

    struct ApplicationSpecification {
        std::string name = "Atlas Engine";
        void* pNativeApp = nullptr;
    };

    class Application {
    public:
        Application(const ApplicationSpecification& specification);
        ~Application();

        Application(const Application&) = delete;
        Application& operator=(const Application&) = delete;

        void init();
        void run();

    private:
        void loadModels();
        void createPipelineLayout();
        void createPipeline();
        void createCommandBuffers();

        void freeCommandBuffers();

        void drawFrame();
        void recreateSwapChain();
        void recordCommandBuffer(int imageIndex);

        ApplicationSpecification specification;

        std::unique_ptr<Window> window = Window::create({specification.pNativeApp, 1200, 800});
        Device device{*window};
        std::unique_ptr<SwapChain> swapChain;
        std::unique_ptr<Pipeline> pipeline;

        VkPipelineLayout pipelineLayout;
        std::vector<VkCommandBuffer> commandBuffers;
        std::unique_ptr<Model> model;
    };
}
