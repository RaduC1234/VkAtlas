#pragma once

#include "Window.hpp"
#include "renderer/Device.hpp"
#include "renderer/Pipeline.hpp"
#include "renderer/SwapChain.hpp"

namespace Atlas {
    struct ApplicationSpecification {
        std::string name = "Atlas Engine";
    };

    class Application {
    public:
        Application(ApplicationSpecification specification);
        ~Application();

        Application(const Application&) = delete;
        Application& operator=(const Application&) = delete;

        void run();

    private:
        void createPipelineLayout();
        void createPipeline();
        void createCommandBuffers();
        void drawFrame();

        ApplicationSpecification specification;

        Window window{{720, 680}};
        Device device{window};
        SwapChain swapChain{device, window.getExtent()};
        std::unique_ptr<Pipeline> pipeline;
        VkPipelineLayout pipelineLayout;
        std::vector<VkCommandBuffer> commandBuffers;
    };
}
