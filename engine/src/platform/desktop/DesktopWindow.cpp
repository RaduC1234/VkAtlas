#include "DesktopWindow.hpp"

#ifdef _WIN32
#include <cassert>
#include <stdexcept>

namespace Atlas {
    DesktopWindow::DesktopWindow(const WindowSpecification &properties) {
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

        glfwWindow = glfwCreateWindow(
            properties.width,
            properties.height,
            properties.title.c_str(),
            nullptr,
            nullptr
        );

        glfwSetWindowUserPointer(glfwWindow, this);
    }

    bool DesktopWindow::shouldClose() {
        return glfwWindowShouldClose(this->glfwWindow);
    }

    void DesktopWindow::createWindowSurface(VkInstance instance, VkSurfaceKHR *surface) const {
        if (glfwCreateWindowSurface(instance, glfwWindow, nullptr, surface) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create window surface");
        }
    }

    void DesktopWindow::pollEvents() {
        glfwPollEvents();
    }

    std::vector<const char *> DesktopWindow::getRequiredExtensions() {
        uint32_t glfwExtensionCount = 0;
        const auto glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

        return {glfwExtensions, glfwExtensions + glfwExtensionCount};
    }

    void DesktopWindow::framebufferResizeCallback(GLFWwindow *glfwWindow, uint32_t width, uint32_t height) {
        auto *window = static_cast<DesktopWindow *>(glfwGetWindowUserPointer(glfwWindow));

        window->width = width;
        window->height = height;
    }
}

#endif