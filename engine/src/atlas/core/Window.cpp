#include "Window.hpp"

#include <cassert>
#include <stdexcept>

namespace Atlas {
    Window::Window(const WindowProperties &properties) {
        glfwInit();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

        assert(
            !(properties.properties & ATLAS_PROPERTIES_WINDOW_DECORATED &&
            properties.properties & ATLAS_PROPERTIES_WINDOW_UNDECORATED) &&
            "An window cannot be decorated and undecorated at the same time"
        );

        assert(
            !(properties.properties & ATLAS_PROPERTIES_WINDOW_RESIZEABLE &&
            properties.properties & ATLAS_PROPERTIES_WINDOW_NON_RESIZEABLE) &&
            "An window cannot be resizable and non-resizable at the same time"
        );

        if (properties.properties & ATLAS_PROPERTIES_WINDOW_DECORATED) {
        }

        window = glfwCreateWindow(
            properties.width,
            properties.height,
            properties.title.c_str(),
            nullptr,
            nullptr
        );

        glfwSetWindowUserPointer(window, this);
    }

    Window::~Window() {
        glfwDestroyWindow(window);
        glfwTerminate();
    }

    void Window::createWindowSurface(VkInstance instance, VkSurfaceKHR *surface) const {
        if (glfwCreateWindowSurface(instance, window, nullptr, surface) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create window surface");
        }
    }

    void Window::framebufferResizeCallback(GLFWwindow *glfwWindow, uint32_t width, uint32_t height) {
        auto* window = static_cast<Window *>(glfwGetWindowUserPointer(glfwWindow));

        window->width = width;
        window->height = height;
    }
} // Atlas
