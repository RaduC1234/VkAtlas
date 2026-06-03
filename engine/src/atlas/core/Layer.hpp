#pragma once

#include <string>
#include <utility>

#include "renderer/Renderer.hpp"

namespace Atlas {
    class Layer {
    public:
        explicit Layer(std::string name = "Layer") : name(std::move(name)) {}
        virtual ~Layer() = default;

        virtual void onAttach() {}
        virtual void onDetach() {}
        virtual void onUpdate(float deltaTime) {}
        virtual void onRender(FrameContext frameContext) {}
        virtual void onImGuiRender() {}

        [[nodiscard]] const std::string &getName() const { return name; }

    private:
        std::string name;
    };
}
