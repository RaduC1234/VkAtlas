#include "RuntimeDebugLayer.hpp"

namespace Atlas::Runtime {
    RuntimeDebugLayer::RuntimeDebugLayer(Window &window): Layer("RuntimeDebugLayer"), window() {}

    void RuntimeDebugLayer::onUpdate(float deltaTime) {
        frameTime = deltaTime;
    }
}
