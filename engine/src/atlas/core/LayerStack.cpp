#include "core/LayerStack.hpp"

namespace Atlas {
    LayerStack::~LayerStack() {
        clear();
    }

    Layer &LayerStack::pushLayer(std::unique_ptr<Layer> layer) {
        auto insertAt = layers.begin() + static_cast<std::ptrdiff_t>(layerInsertIndex);
        auto it = layers.insert(insertAt, std::move(layer));
        ++layerInsertIndex;
        (*it)->onAttach();
        return **it;
    }

    Layer &LayerStack::pushOverlay(std::unique_ptr<Layer> layer) {
        layers.emplace_back(std::move(layer));
        layers.back()->onAttach();
        return *layers.back();
    }

    void LayerStack::clear() {
        for (auto it = layers.rbegin(); it != layers.rend(); ++it) {
            (*it)->onDetach();
        }

        layers.clear();
        layerInsertIndex = 0;
    }
}
