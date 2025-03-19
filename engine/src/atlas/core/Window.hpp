#pragma once

#include <cstdint>
#include <string>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include "Core.hpp"

namespace Atlas {

    enum : uint32_t {
        ATLAS_PROPERTIES_WINDOW_UNDECORATED = BIT(0),
        ATLAS_PROPERTIES_WINDOW_DECORATED = BIT(1),
        ATLAS_PROPERTIES_WINDOW_RESIZEABLE = BIT(2),
        ATLAS_PROPERTIES_WINDOW_NON_RESIZEABLE = BIT(3),
    };

    struct WindowProperties {
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

        VkExtent2D getExtent() { return {width, height}; }

        bool shouldClose() const { return glfwWindowShouldClose(window); }
        void createWindowSurface(VkInstance instance, VkSurfaceKHR *surface) const;

    private:
        GLFWwindow *window;
        uint32_t width, height;

        static void framebufferResizeCallback(GLFWwindow *windows, uint32_t width, uint32_t height);
    };
}
