#pragma once

#ifdef _WIN32

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include "core/Window.hpp"

namespace Atlas {
    class DesktopWindow : public Window {
    public:
        DesktopWindow(const WindowSpecification& properties);

        bool shouldClose() override;
        void createWindowSurface(VkInstance instance, VkSurfaceKHR*surface) const override;
        void pollEvents() override;
        void waitEvents() override;

        std::vector<const char *> getRequiredExtensions() override;

    private:
        GLFWwindow* glfwWindow;

        static void framebufferResizeCallback(GLFWwindow *glfwWindow, int width, int height);
    };
}

#endif