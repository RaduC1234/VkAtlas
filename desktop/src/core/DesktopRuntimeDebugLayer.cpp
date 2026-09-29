#include "DesktopRuntimeDebugLayer.hpp"

#include <cstdio>

namespace Atlas::Runtime {
    DesktopRuntimeDebugLayer::DesktopRuntimeDebugLayer(Window &window, std::string baseTitle)
        : Layer("RuntimeDebugLayer"), window(window), baseTitle(std::move(baseTitle)) {}

    void DesktopRuntimeDebugLayer::onUpdate(float deltaTime) {
        frameTime = deltaTime;
        elapsed += deltaTime;
        ++frames;

        if (elapsed < 0.25f)
            return;

        const float fps = static_cast<float>(frames) / elapsed;
        char buf[128]{};
        std::snprintf(buf, sizeof(buf), "%s - %.0f FPS", baseTitle.c_str(), fps);
        window.setTitle(buf);

        elapsed = 0.0f;
        frames = 0;
    }
}
