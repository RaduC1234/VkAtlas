#pragma once

#ifdef ATLAS_PLATFORM_DESKTOP

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

        void setCursorMode(CursorMode cursorMode) override;

        void setWindowIcon(const std::string &iconPath) override;

        void setTheme(uint32_t darkMode) override;

        void * getNativeHandle() const override;

    private:
        GLFWwindow* glfwWindow;

        static void framebufferResizeCallback(GLFWwindow *glfwWindow, int width, int height);

        // mouse callbacks
        static void mouseCursorPositionCallback(GLFWwindow *glfwWindow, double xPos, double yPos);
        static void mouseButtonCallback(GLFWwindow *glfwWindow, int button, int action, int mods);
        static void mouseScrollCallback(GLFWwindow *glfwWindow, double xOffset, double yOffset);

        // keyboard callbacks
        static void keyboardKeyCallback(GLFWwindow *glfwWindow, int key, int scancode, int action, int mods);
        static void keyboardTextCallback(GLFWwindow *glfwWindow, unsigned int codepoint);

    };
}

#endif