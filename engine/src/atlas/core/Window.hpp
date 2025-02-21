#pragma once

#include <cstdint>
#include <string>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

namespace Atlas {
    struct WindowProperties {
        enum : uint32_t {
            ATLAS_PROPERTIES_WINDOW_UNDECORATED,
            ATLAS_PROPERTIES_WINDOW_DECORATED,
            ATLAS_PROPERTIES_WINDOW_RESIZEABLE,
            ATLAS_PROPERTIES_WINDOW_NON_RESIZEABLE,
        };

        uint32_t width = 1080;
        uint32_t height = 720;
        std::string title = "Atlas Window";
        uint32_t properties = ATLAS_PROPERTIES_WINDOW_DECORATED | ATLAS_PROPERTIES_WINDOW_RESIZEABLE;
    };

    class Window {
    public:
        Window(const WindowProperties &properties);
        ~Window();

        Window(const Window &) = delete;
        Window &operator=(const Window &) = delete;

        bool shouldClose() const { return glfwWindowShouldClose(window); }

        void createWindowSurface(VkInstance instance, VkSurfaceKHR *surface) const;

    private:
        GLFWwindow *window;

        static void framebufferResizeCallback(GLFWwindow *windows, uint32_t width, uint32_t height);
    };
}
