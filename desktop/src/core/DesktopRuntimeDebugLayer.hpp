#pragma once

#include <core/Layer.hpp>

#include <string>

namespace Atlas::Runtime {
    class DesktopRuntimeDebugLayer final : public Layer {
    public:
        DesktopRuntimeDebugLayer(Window& window, std::string baseTitle);

        void onUpdate(float deltaTime) override;

    private:
        Window& window;
        float frameTime = 0.0f;
        float elapsed = 0.0f;
        uint32_t frames = 0;
        std::string baseTitle;
    };
}
