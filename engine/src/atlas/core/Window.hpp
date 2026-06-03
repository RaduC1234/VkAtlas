#pragma once

#include <cassert>
#include <string>
#include <memory>
#include <vector>

#include <vulkan/vulkan.h>

#include "Core.hpp"

namespace Atlas {
    class IInputProvider;

    class Window {
    public:
        enum Properties : uint32_t {
            Undecorated = BIT(0),
            Decorated = BIT(1),
            Resizeable = BIT(2),
            NonResizeable = BIT(3),
        };

        enum class CursorMode {
            Disabled, // Hides and grabs the cursor, providing virtual and unlimited cursor movement. This is useful for implementing for example 3D camera controls.
            Normal, // Makes the cursor visible and behaving normally
            Hidden, // Makes the cursor invisible when it is over the content area of the window_ but does not restrict the cursor from leaving.
        };

        enum class Theme {
            Light = 0,
            Dark = 1,
        };

        struct CreateInfo {
            void *pNativeApp = nullptr;
            uint32_t width = 1080;
            uint32_t height = 720;
            std::string title = "Atlas Window";
            std::string iconPath;
            uint32_t properties = Decorated | Resizeable;
            IInputProvider *inputProvider = nullptr;
        };

        virtual ~Window() = default;
        Window &operator=(const Window &) = delete;

        virtual bool shouldClose() = 0;
        virtual void createWindowSurface(VkInstance instance, VkSurfaceKHR *surface) const = 0;
        virtual void pollEvents() = 0;
        virtual void waitEvents() = 0;
        virtual std::vector<const char *> getRequiredExtensions() = 0;

        virtual void setCursorMode(CursorMode cursorMode) { assert(true && "Method not implemented"); }

        virtual void setWindowIcon(const std::string &iconPath) { assert(true && "Method not implemented"); }
        virtual void setTitle(const std::string &title) { assert(true && "Method not implemented"); }

        virtual void setTheme(Theme theme) { assert(true && "Method not implemented"); }
        virtual Theme getTheme() const { assert(true && "Method not implemented"); return Theme::Dark; }

        virtual void setDecorated(bool decorated) { assert(true && "Method not implemented"); }

        bool wasWindowResized() const { return framebufferResized; }
        void resetWindowResizedFlag() { this->framebufferResized = false; }

        VkExtent2D getExtent() const { return {width, height}; }
        int32_t getWidth() const { return width; }
        int32_t getHeight() const { return height; }

        virtual void *getNativeHandle() const = 0;

        static std::unique_ptr<Window> create(const CreateInfo &specification);

    protected:
        uint32_t width{}, height{};
        bool framebufferResized{false};
    };
}
