#pragma once

#include <core/Layer.hpp>

namespace Atlas::Runtime {
    class DesktopRuntimeDebugLayer final : public Layer {
    public:
        DesktopRuntimeDebugLayer(Window& window);

        void onUpdate(float deltaTime) override;

    private:
        Window& window;
        float frameTime = 0.0f;
    };
}
