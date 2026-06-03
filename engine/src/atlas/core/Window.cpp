#include "Window.hpp"

#include "desktop/DesktopWindow.hpp"
#include "android/AndroidWindow.hpp"

namespace Atlas {
    std::unique_ptr<Window> Window::create(const CreateInfo &specification) {
#if defined(__ANDROID__)
        return std::make_unique<AndroidWindow>(specification);
#elif defined(_WIN32) || defined(__linux__)
        return std::make_unique<DesktopWindow>(specification);
#endif
    }
} // Atlas
