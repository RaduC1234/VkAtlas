#pragma once

#include <memory>
#include <string>

#include "Window.hpp"
#include "renderer/Device.hpp"
#include "renderer/Renderer.hpp"

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
        Renderer renderer{window, device};
    };
}
