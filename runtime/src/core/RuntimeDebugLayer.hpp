#pragma once

#include <core/Layer.hpp>

namespace Atlas::Runtime {
    class RuntimeDebugLayer final : public Layer {
    public:
        RuntimeDebugLayer() : Layer("RuntimeDebugLayer") {}

        void onUpdate(float deltaTime);
        void onImGuiRender();

    private:
        float frameTime = 0.0f;
    };
}
