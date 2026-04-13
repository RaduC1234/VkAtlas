//
// Created by Radu on 13-Apr-26.
//

#include "RenderGraph.hpp"

namespace Atlas {
    RenderGraph::Builder::Builder(Device &device): device(device) {
    }

    RenderGraph::Builder & RenderGraph::Builder::addStage(std::unique_ptr<IRenderStage> stage) {
        stages_.push_back(std::move(stage));
        return *this;
    }

    RenderGraph RenderGraph::Builder::build(uint32_t extentWidth, uint32_t extentLenght, Mode renderMode) {
        RenderGraph graph(device, mode_);
        graph.stages_ = std::move(stages_);
        graph.bake(width_, height_);
        return graph;
    }

    void RenderGraph::bake(uint32_t w, uint32_t h) {

    }
}
