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
        Application(ApplicationSpecification specification);
        ~Application();

        Application(const Application&) = delete;
        Application& operator=(const Application&) = delete;

        void run();

    private:
        void loadModels();
        void createPipelineLayout();
        void createPipeline();
        void createCommandBuffers();
        void drawFrame();

        ApplicationSpecification specification;

        std::unique_ptr<Window> window = Window::create({specification.pNativeApp, 720, 680});
        Device device{*window};
        SwapChain swapChain{device, window->getExtent()};

        std::unique_ptr<Pipeline> pipeline;
        VkPipelineLayout pipelineLayout;
        std::vector<VkCommandBuffer> commandBuffers;
        std::unique_ptr<Model> model;
    };
}
