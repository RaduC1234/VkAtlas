#pragma once

#include "Window.hpp"
#include "renderer/Device.hpp"
#include "renderer/Renderer.hpp"

#include <memory>

#include "scene/IScene.hpp"


namespace Atlas {
    struct ApplicationSpecification {
        std::string name = "Atlas Engine";
        void *pNativeApp = nullptr;
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
        Renderer renderer{
            {
                .enableRaytracing = true
            }
        };

        std::unique_ptr<IScene> currentScene;
    };
}
