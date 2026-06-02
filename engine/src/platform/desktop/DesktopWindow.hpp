#pragma once

#ifdef ATLAS_PLATFORM_DESKTOP

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include "core/Window.hpp"

namespace Atlas {

    struct CaptionBar {
        void *hwnd    = nullptr;
        int   hovered = -1;  // 0=min, 1=max/restore, 2=close
        int   pressed = -1;

        void create(void *ownerHwnd);
        void destroy();
        void reposition(void *ownerHwnd) const;
        void invalidate() const;
    };

    class DesktopWindow : public Window {
    public:
        DesktopWindow(const CreateInfo &properties);
        ~DesktopWindow() override;

        bool shouldClose() override;
        void createWindowSurface(VkInstance instance, VkSurfaceKHR *surface) const override;
        void pollEvents() override;
        void waitEvents() override;

        std::vector<const char *> getRequiredExtensions() override;

        void setCursorMode(CursorMode cursorMode) override;
        void setWindowIcon(const std::string &iconPath) override;
        void setTitle(const std::string &title) override;
        void setTheme(Theme theme) override;
        Theme getTheme() const override { return theme; }
        void *getNativeHandle() const override;

        GLFWwindow *glfwWindow         = nullptr;
        void       *originalWindowProc = nullptr;
        bool        customTitleBar     = false;
        CaptionBar  captionBar;
        Theme theme;

        void installCustomTitleBar();
        void removeCustomTitleBar();

        static void framebufferResizeCallback(GLFWwindow *w, int width, int height);
        static void mouseCursorPositionCallback(GLFWwindow *w, double xPos, double yPos);
        static void mouseButtonCallback(GLFWwindow *w, int button, int action, int mods);
        static void mouseScrollCallback(GLFWwindow *w, double xOffset, double yOffset);
        static void keyboardKeyCallback(GLFWwindow *w, int key, int scancode, int action, int mods);
        static void keyboardTextCallback(GLFWwindow *w, unsigned int codepoint);
    };
}

#endif
