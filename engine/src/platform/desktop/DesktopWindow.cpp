#include "DesktopWindow.hpp"

#include "core/Keyboard.hpp"

#ifdef _WIN32
#include <cassert>
#include <stdexcept>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// Win32 API for dark mode
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <dwmapi.h>

#pragma comment(lib, "dwmapi.lib")

namespace Atlas {
    DesktopWindow::DesktopWindow(const WindowSpecification &properties) {
        this->width = properties.width;
        this->height = properties.height;

        glfwInit();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

        assert(
            !(properties.properties & WINDOW_PROPERTIES_DECORATED &&
                properties.properties & WINDOW_PROPERTIES_UNDECORATED) &&
            "An window cannot be decorated and undecorated at the same time"
        );

        assert(
            !(properties.properties & WINDOW_PROPERTIES_RESIZEABLE &&
                properties.properties & WINDOW_PROPERTIES_NON_RESIZEABLE) &&
            "An window cannot be resizable and non-resizable at the same time"
        );

        if (properties.properties & WINDOW_PROPERTIES_DECORATED) {
        }

        glfwWindow = glfwCreateWindow(
            properties.width,
            properties.height,
            properties.title.c_str(),
            nullptr,
            nullptr
        );

        glfwSetWindowUserPointer(glfwWindow, this);

        glfwSetFramebufferSizeCallback(glfwWindow, framebufferResizeCallback);

        glfwSetCursorPosCallback(glfwWindow, mouseCursorPositionCallback);
        glfwSetMouseButtonCallback(glfwWindow, mouseButtonCallback);


        glfwSetKeyCallback(glfwWindow, keyboardKeyCallback);
        glfwSetCharCallback(glfwWindow, keyboardTextCallback);

        if (!properties.iconPath.empty()) {
            DesktopWindow::setWindowIcon(properties.iconPath);
        }
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

    void DesktopWindow::waitEvents() {
        glfwWaitEvents();
    }

    std::vector<const char *> DesktopWindow::getRequiredExtensions() {
        uint32_t glfwExtensionCount = 0;
        const auto glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

        return {glfwExtensions, glfwExtensions + glfwExtensionCount};
    }

    void DesktopWindow::setCursorMode(const CursorMode cursorMode) {
        switch (cursorMode) {
            case WINDOW_CURSOR_DISABLED:
                glfwSetInputMode(this->glfwWindow, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                break;
            case WINDOW_CURSOR_NORMAL:
                glfwSetInputMode(this->glfwWindow, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                break;
            case ATLAS_WINDOW_CURSOR_HIDDEN:
                glfwSetInputMode(this->glfwWindow, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
                break;
        }
    }

    void DesktopWindow::setWindowIcon(const std::string &iconPath) {
        // No-op when empty
        if (iconPath.empty()) {
            return;
        }

        int iconWidth = 0, iconHeight = 0, iconChannels = 0;
        // Request 4 channels (RGBA) because GLFW expects 4-byte RGBA pixels
        unsigned char *pixels = stbi_load(iconPath.c_str(), &iconWidth, &iconHeight, &iconChannels, 4);

        if (!pixels) {
            const char *reason = stbi_failure_reason();
            throw std::runtime_error(std::string("Failed to load window icon '") + iconPath + "': " + (reason ? reason : "unknown"));
        }

        GLFWimage image;
        image.width = iconWidth;
        image.height = iconHeight;
        image.pixels = pixels;

        // GLFW copies the pixels internally, so we can free after the call
        glfwSetWindowIcon(this->glfwWindow, 1, &image);

        stbi_image_free(pixels);
    }

    void DesktopWindow::setTheme(uint32_t enabled) {
        HWND hwnd = glfwGetWin32Window(this->glfwWindow);

        if (hwnd) {
            // DWMWA_USE_IMMERSIVE_DARK_MODE is available on Windows 11 Build 22000+
            // For Windows 10, we use the undocumented attribute 19
            BOOL useDarkMode = enabled ? TRUE : FALSE;

            // Try Windows 11 method first (DWMWA_USE_IMMERSIVE_DARK_MODE = 20)
            HRESULT hr = DwmSetWindowAttribute(hwnd, 20, &useDarkMode, sizeof(useDarkMode));

            // If that fails, try Windows 10 undocumented attribute (19)
            if (FAILED(hr)) {
                DwmSetWindowAttribute(hwnd, 19, &useDarkMode, sizeof(useDarkMode));
            }
        }
    }

    void * DesktopWindow::getNativeHandle() const {
        return glfwWindow;
    }

    void DesktopWindow::framebufferResizeCallback(GLFWwindow *glfwWindow, int width, int height) {
        auto *window = static_cast<DesktopWindow *>(glfwGetWindowUserPointer(glfwWindow));

        window->width = width;
        window->height = height;
        window->framebufferResized = true;
    }

    void DesktopWindow::mouseCursorPositionCallback(GLFWwindow *glfwWindow, double xPos, double yPos) {
        Mouse::xPos = xPos;
        Mouse::yPos = yPos;
        Mouse::dragging = Mouse::buttonPressed[0] || Mouse::buttonPressed[1] || Mouse::buttonPressed[2];
    }

    void DesktopWindow::mouseButtonCallback(GLFWwindow *glfwWindow, int button, int action, int mods) {
        switch (action) {
            case GLFW_PRESS: {
                if (button < Mouse::buttonPressed.size()) {
                    Mouse::buttonPressed[button] = true;
                }
                break;
            }
            case GLFW_RELEASE: {
                if (button < Mouse::buttonPressed.size()) {
                    Mouse::buttonPressed[button] = false;
                }
                break;
            }
            default: {
                //...; }
            }
        }
    }

    void DesktopWindow::mouseScrollCallback(GLFWwindow *glfwWindow, double xOffset, double yOffset) {
        Mouse::scrollXOffset = xOffset;
        Mouse::scrollYOffset = yOffset;
    }

    void DesktopWindow::keyboardKeyCallback(GLFWwindow *glfwWindow, int key, int scancode, int action, int mods) {
        if (key > Keyboard::keyPressed.size()) {
            return;
        }

        if (action == GLFW_PRESS || action == GLFW_REPEAT) {
            Keyboard::keyPressed[key] = true;
        } else if (action == GLFW_RELEASE) {
            Keyboard::keyPressed[key] = false;
        }
    }

    void DesktopWindow::keyboardTextCallback(GLFWwindow *glfwWindow, unsigned int codepoint) {
    }
}

#endif
