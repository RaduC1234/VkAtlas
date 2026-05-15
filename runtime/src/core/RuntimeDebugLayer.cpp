#include "../RuntimeDebugLayer.hpp"

#include <imgui.h>

namespace Atlas::Runtime {
    void RuntimeDebugLayer::onUpdate(float deltaTime) {
        frameTime = deltaTime;
    }

    void RuntimeDebugLayer::onImGuiRender() {
        ImGui::Begin("Runtime Debug");
        ImGui::Text("Frame %.3f ms", frameTime * 1000.0f);
        ImGui::Text("FPS %.1f", frameTime > 0.0f ? 1.0f / frameTime : 0.0f);
        ImGui::End();
    }
}
