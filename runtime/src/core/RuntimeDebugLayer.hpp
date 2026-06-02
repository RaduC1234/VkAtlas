#pragma once

#include <core/Layer.hpp>

namespace Atlas::Runtime {
    class RuntimeDebugLayer final : public Layer {
    public:
        RuntimeDebugLayer(Window& window);

        void onUpdate(float deltaTime) override;

    private:
        Window& window;
        float frameTime = 0.0f;
    };
}
