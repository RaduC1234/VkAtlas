#include "Window.hpp"

#include "desktop/DesktopWindow.hpp"
#include "android/AndroidWindow.hpp"

namespace Atlas {
    std::unique_ptr<Window> Window::create(const WindowSpecification &specification) {
#ifdef _WIN32
        return std::make_unique<DesktopWindow>(specification);
#elif defined(__ANDROID__)
        return std::make_unique<AndroidWindow>(specification);
#endif
    }
} // Atlas
