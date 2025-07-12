#pragma once

#include "Window.hpp"
#include "renderer/Device.hpp"
#include "renderer/Renderer.hpp"

#include <entt/entt.hpp>

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

        void run();

    private:
        void loadGameObjects();

        ApplicationSpecification specification;

        std::unique_ptr<Window> window = Window::create({specification.pNativeApp, 1200, 800});
        Device device{*window};
        Renderer renderer{*window, device};

        entt::registry registry;
    };
}
