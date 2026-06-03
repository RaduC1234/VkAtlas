#include "DesktopWindow.hpp"
#include "DesktopInputProvider.hpp"

#ifdef ATLAS_PLATFORM_DESKTOP

#include <cassert>
#include <stdexcept>

#include "stb_image.h"

#ifdef ATLAS_PLATFORM_WINDOWS
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")
#endif

#ifdef ATLAS_PLATFORM_LINUX
#include <cstdlib>
#endif

namespace Atlas {

    DesktopWindow::DesktopWindow(const CreateInfo &properties) {
        width  = properties.width;
        height = properties.height;

#ifdef ATLAS_PLATFORM_LINUX
        ::setenv("GTK_THEME", "Adwaita:dark", 0);
#endif

        glfwInit();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

        assert(!(properties.properties & Decorated && properties.properties & Undecorated) &&
               "A window cannot be decorated and undecorated at the same time");
        assert(!(properties.properties & Resizeable && properties.properties & NonResizeable) &&
               "A window cannot be resizable and non-resizable at the same time");

        bool wantDecorated = !(properties.properties & Undecorated);
        glfwWindowHint(GLFW_DECORATED, wantDecorated ? GLFW_TRUE : GLFW_FALSE);
        glfwWindowHint(GLFW_RESIZABLE, (properties.properties & NonResizeable) ? GLFW_FALSE : GLFW_TRUE);

        glfwWindow = glfwCreateWindow(
            static_cast<int>(properties.width),
            static_cast<int>(properties.height),
            properties.title.c_str(),
            nullptr,
            nullptr);

        glfwSetWindowUserPointer(glfwWindow, this);
        glfwSetFramebufferSizeCallback(glfwWindow, framebufferResizeCallback);
        glfwSetCursorPosCallback(glfwWindow, mouseCursorPositionCallback);
        glfwSetMouseButtonCallback(glfwWindow, mouseButtonCallback);
        glfwSetScrollCallback(glfwWindow, mouseScrollCallback);
        glfwSetKeyCallback(glfwWindow, keyboardKeyCallback);
        glfwSetCharCallback(glfwWindow, keyboardTextCallback);

        Keyboard::setProvider(&DesktopInputProvider::instance());
        Mouse::setProvider(&DesktopInputProvider::instance());

        if (!properties.iconPath.empty()) {
            setWindowIcon(properties.iconPath);
        }

        setTheme(Theme::Dark);
    }

    bool DesktopWindow::shouldClose() {
        return glfwWindowShouldClose(glfwWindow);
    }

    void DesktopWindow::createWindowSurface(VkInstance instance, VkSurfaceKHR *surface) const {
        if (glfwCreateWindowSurface(instance, glfwWindow, nullptr, surface) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create window surface");
        }
    }

    void DesktopWindow::pollEvents() {
        glfwPollEvents();
    }

    void DesktopWindow::waitEvents() {
        glfwWaitEvents();
    }

    std::vector<const char *> DesktopWindow::getRequiredExtensions() {
        uint32_t count = 0;
        const char **ext = glfwGetRequiredInstanceExtensions(&count);
        return {ext, ext + count};
    }

    void DesktopWindow::setCursorMode(CursorMode cursorMode) {
        if (cursorMode == CursorMode::Disabled) {
            glfwSetInputMode(glfwWindow, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        } else if (cursorMode == CursorMode::Hidden) {
            glfwSetInputMode(glfwWindow, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
        } else {
            glfwSetInputMode(glfwWindow, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }
    }

    void DesktopWindow::setWindowIcon(const std::string &iconPath) {
        if (iconPath.empty()) {
            return;
        }

        int w = 0, h = 0, ch = 0;
        unsigned char *pixels = stbi_load(iconPath.c_str(), &w, &h, &ch, 4);
        if (!pixels) {
            const char *reason = stbi_failure_reason();
            throw std::runtime_error(
                std::string("Failed to load window icon '") + iconPath +
                "': " + (reason ? reason : "unknown"));
        }

        GLFWimage img{w, h, pixels};
        glfwSetWindowIcon(glfwWindow, 1, &img);
        stbi_image_free(pixels);
    }

    void DesktopWindow::setTitle(const std::string &title) {
        glfwSetWindowTitle(glfwWindow, title.c_str());
    }

    void DesktopWindow::setDecorated(bool decorated) {
        glfwSetWindowAttrib(glfwWindow, GLFW_DECORATED, decorated ? GLFW_TRUE : GLFW_FALSE);
    }

    void DesktopWindow::setTheme(Theme newTheme) {
        theme = newTheme;

#ifdef ATLAS_PLATFORM_WINDOWS
        HWND hwnd = glfwGetWin32Window(glfwWindow);
        if (hwnd) {
            BOOL dark = (theme == Theme::Dark) ? TRUE : FALSE;
            if (FAILED(DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark)))) {
                DwmSetWindowAttribute(hwnd, 19, &dark, sizeof(dark));
            }
        }
#endif

#ifdef ATLAS_PLATFORM_LINUX
        ::setenv("GTK_THEME", theme == Theme::Dark ? "Adwaita:dark" : "Adwaita", 1);
#endif
    }

    void *DesktopWindow::getNativeHandle() const {
        return glfwWindow;
    }

    void DesktopWindow::framebufferResizeCallback(GLFWwindow *w, int width, int height) {
        auto *win = static_cast<DesktopWindow *>(glfwGetWindowUserPointer(w));
        win->width = width;
        win->height = height;
        win->framebufferResized = true;
    }

    void DesktopWindow::mouseCursorPositionCallback(GLFWwindow *, double x, double y) {
        Mouse::xPos = x;
        Mouse::yPos = y;
        Mouse::dragging = Mouse::buttonPressed[0] || Mouse::buttonPressed[1] || Mouse::buttonPressed[2];
    }

    void DesktopWindow::mouseButtonCallback(GLFWwindow *, int button, int action, int) {
        if (button >= static_cast<int>(Mouse::buttonPressed.size())) {
            return;
        }

        if (action == GLFW_PRESS) {
            Mouse::buttonPressed[button] = true;
        }

        if (action == GLFW_RELEASE) {
            Mouse::buttonPressed[button] = false;
        }
    }

    void DesktopWindow::mouseScrollCallback(GLFWwindow *, double xOffset, double yOffset) {
        Mouse::scrollXOffset = xOffset;
        Mouse::scrollYOffset = yOffset;
    }

    void DesktopWindow::keyboardKeyCallback(GLFWwindow *, int key, int, int action, int) {
        if (key >= static_cast<int>(Keyboard::keyPressed.size())) {
            return;
        }

        if (action == GLFW_PRESS || action == GLFW_REPEAT) {
            Keyboard::keyPressed[key] = true;
        } else if (action == GLFW_RELEASE) {
            Keyboard::keyPressed[key] = false;
        }
    }

    void DesktopWindow::keyboardTextCallback(GLFWwindow *, unsigned int) {
    }
}

#endif
