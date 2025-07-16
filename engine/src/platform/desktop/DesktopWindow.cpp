#include "DesktopWindow.hpp"

#include "core/Keyboard.hpp"

#ifdef _WIN32
#include <cassert>
#include <stdexcept>

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
