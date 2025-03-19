#pragma once

#include <string>

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

        virtual ~Application() = default;

        void run();

    private:
        ApplicationSpecification specification;

        Window window{{720, 680}};
        Device device{window};
        Pipeline pipeline{
            device,
            "shaders/simple_shader.vert.spv",
            "shaders/simple_shader.frag.spv",
            Pipeline::defaultPipelineConfigInfo()};
        // SwapChain swapChain{device, window.getExtent()};
        //Renderer renderer{window, device};
    };
}
