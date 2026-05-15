#pragma once

#include "core/Layer.hpp"

#include <cstddef>
#include <memory>
#include <vector>

namespace Atlas {
    class LayerStack {
    public:
        ~LayerStack();

        LayerStack() = default;
        LayerStack(const LayerStack &) = delete;
        LayerStack &operator=(const LayerStack &) = delete;

        Layer &pushLayer(std::unique_ptr<Layer> layer);
        Layer &pushOverlay(std::unique_ptr<Layer> layer);
        void clear();

        auto begin() { return layers.begin(); }
        auto end() { return layers.end(); }
        auto begin() const { return layers.begin(); }
        auto end() const { return layers.end(); }

    private:
        std::vector<std::unique_ptr<Layer>> layers;
        size_t layerInsertIndex = 0;
    };
}
