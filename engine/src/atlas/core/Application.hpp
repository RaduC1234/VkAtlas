#pragma once

#include "Window.hpp"
#include "renderer/Device.hpp"
#include "renderer/Renderer.hpp"

#include <memory>

#include "scene/Scene.hpp"


namespace Atlas {
    struct ApplicationSpecification {
        std::string name = "Atlas Engine";
        void *pNativeApp = nullptr;
        RenderMode renderMode = RenderMode::XROnly;
    };

    class Application {
    public:
        Application(const ApplicationSpecification &specification);
        ~Application();

        Application(const Application &) = delete;
        Application &operator=(const Application &) = delete;

        void run();

    private:
        ApplicationSpecification specification;

        std::unique_ptr<Window> window;
        std::unique_ptr<Device> device;
        std::unique_ptr<Renderer> renderer;

        std::unique_ptr<Scene> currentScene;
    };
}
