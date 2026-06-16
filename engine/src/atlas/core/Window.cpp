#include "Window.hpp"

#include "desktop/DesktopWindow.hpp"

namespace Atlas {
    std::unique_ptr<Window> Window::create(const CreateInfo &specification) {
        return std::make_unique<DesktopWindow>(specification);
    }
} // Atlas
