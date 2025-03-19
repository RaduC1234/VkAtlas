#include "Application.hpp"

#include "renderer/Device.hpp"

namespace Atlas {
    Application::Application(ApplicationSpecification specification) : specification(specification) {
        
    }

    void Application::run() {

        while (!window.shouldClose()) {
            glfwPollEvents();
        }
    }
}
