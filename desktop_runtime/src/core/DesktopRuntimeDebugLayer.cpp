#include "DesktopRuntimeDebugLayer.hpp"

namespace Atlas::Runtime {
    DesktopRuntimeDebugLayer::DesktopRuntimeDebugLayer(Window &window): Layer("RuntimeDebugLayer"), window() {}

    void DesktopRuntimeDebugLayer::onUpdate(float deltaTime) {
        frameTime = deltaTime;
    }
}
