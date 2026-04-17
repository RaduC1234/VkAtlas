#include "RenderGraph.hpp"

namespace Atlas {
    std::unique_ptr<RenderGraph> RenderGraph::Builder::build(Mode mode) {
        assert(width_ > 0 && height_ > 0);
        assert(!stages_.empty());

        Mode resolved = mode;
        /*if (resolved == Mode::Auto) {
            resolved = device.isMobile() ? Mode::MultiSubpass : Mode::MultiPass;
        }*/

        auto graph = std::unique_ptr<RenderGraph>(
            new RenderGraph(device, resolved, width_, height_));
        graph->stages_ = std::move(stages_);
        graph->bake();
        return graph;
    }

    void RenderGraph::bake() {
        nodes_.clear();
        nodes_.reserve(stages_.size());

        for (auto &stage: stages_) {
            Node node;
            node.stage = stage.get();

            std::vector<IRenderStage::Resource> outputs;
            std::vector<std::string> inputs;
            stage->getDeclaredOutputs(outputs);
            stage->getDeclaredInputs(inputs);

            for (auto &r: inputs) { node.inputs.push_back(r); }
            for (auto &r: outputs) { node.outputs.push_back(r.name); }

            nodes_.push_back(std::move(node));
        }

        for (auto &a: nodes_) {
            for (auto &b: nodes_) {
                if (&a != &b) {
                    for (auto &input: a.inputs) {
                        for (auto &output: b.outputs) {
                            if (input == output) {
                                a.dependsOn.push_back(&b);
                            }
                        }
                    }
                }
            }
        }

        topoSort(nodes_);
        bakeResources();
        bakeBarriers();
    }

    void RenderGraph::bakeResources() {
        for (auto &stage: stages_) {
            std::vector<IRenderStage::Resource> outputs;
            stage->getDeclaredOutputs(outputs);

            for (auto &output: outputs) {
                if (ownedResources_.contains(output.name)) {
                    continue;
                }

                resourceTypes_[output.name] = output.type;

                auto builder = GPUImage::Builder(device)
                        .setExtent(width, height)
                        .setFormat(output.format)
                        .setUsage(output.usage)
                        .setDebugName(output.name.c_str());

                if (output.type == IRenderStage::Resource::Type::DEPTH_ATTACHMENT) {
                    builder.addView(VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)
                            .addView(VK_IMAGE_ASPECT_STENCIL_BIT);
                } else {
                    builder.addView(VK_IMAGE_ASPECT_COLOR_BIT);
                }

                ownedResources_.emplace(output.name, builder.build());
            }
        }

        std::unordered_map<std::string, std::reference_wrapper<GPUImage>> refs;
        for (auto &[name, img]: ownedResources_) {
            refs.emplace(name, std::ref(img));
        }

        for (auto &stage: stages_) {
            stage->onResourcesCreated(refs);
        }
    }

    void RenderGraph::build(entt::registry &registry) {
        for (auto &stage: stages_) {
            stage->onSceneChanged(registry);
        }
    }

    void RenderGraph::bakeBarriers() {
        std::unordered_map<std::string, VkImageLayout> currentLayouts;

        for (auto &node: nodes_) {
            std::vector<IRenderStage::Resource> outputs;
            std::vector<std::string> inputs;
            node.stage->getDeclaredOutputs(outputs);
            node.stage->getDeclaredInputs(inputs);

            for (auto &input: inputs) {
                auto it = currentLayouts.find(input);
                if (it == currentLayouts.end()) { continue; }

                VkImageLayout srcLayout = it->second;

                auto typeIt = resourceTypes_.find(input);
                IRenderStage::Resource::Type resType = typeIt != resourceTypes_.end()
                                                           ? typeIt->second
                                                           : IRenderStage::Resource::Type::SHADER_READ;

                VkImageLayout dstLayout = readLayoutFor(resType);
                if (srcLayout == dstLayout) {
                    continue;
                }

                Barrier barrier{};
                barrier.resourceName = input;
                barrier.oldLayout = srcLayout;
                barrier.newLayout = dstLayout;
                barrier.srcAccess = writeAccessFor(srcLayout);
                barrier.dstAccess = VK_ACCESS_SHADER_READ_BIT;
                barrier.srcStage = writeStageFor(srcLayout);
                barrier.dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
                barrier.aspect = aspectFor(resType);

                node.barriersBeforeExec.push_back(barrier);
                currentLayouts[input] = dstLayout;
            }

            for (auto &output: outputs) {
                currentLayouts[output.name] = writeLayoutFor(output.type);
            }
        }
    }

    void RenderGraph::render(VkCommandBuffer cmd, VkDescriptorSet globalSet) {
        for (auto &node: nodes_) {
            emitBarriers(cmd, node);
            node.stage->record(cmd, globalSet);
        }
    }

    void RenderGraph::emitBarriers(VkCommandBuffer cmd, const Node &node) const {
        if (node.barriersBeforeExec.empty()) { return; }

        std::vector<VkImageMemoryBarrier> vkBarriers;
        vkBarriers.reserve(node.barriersBeforeExec.size());

        VkPipelineStageFlags srcStages = 0;
        VkPipelineStageFlags dstStages = 0;

        for (auto &b: node.barriersBeforeExec) {
            auto it = ownedResources_.find(b.resourceName);
            if (it == ownedResources_.end()) { continue; }

            VkImageMemoryBarrier vkb{};
            vkb.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            vkb.oldLayout = b.oldLayout;
            vkb.newLayout = b.newLayout;
            vkb.srcAccessMask = b.srcAccess;
            vkb.dstAccessMask = b.dstAccess;
            vkb.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            vkb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            vkb.image = it->second.image();
            vkb.subresourceRange = {b.aspect, 0, 1, 0, 1};

            vkBarriers.push_back(vkb);
            srcStages |= b.srcStage;
            dstStages |= b.dstStage;
        }

        if (!vkBarriers.empty()) {
            vkCmdPipelineBarrier(cmd,
                                 srcStages, dstStages,
                                 0, 0, nullptr, 0, nullptr,
                                 static_cast<uint32_t>(vkBarriers.size()),
                                 vkBarriers.data());
        }
    }

    void RenderGraph::topoSort(std::vector<Node> &nodes) {
        const uint32_t n = static_cast<uint32_t>(nodes.size());

        std::vector<std::vector<uint32_t> > depIndices(n);
        for (uint32_t i = 0; i < n; i++) {
            for (auto *dep: nodes[i].dependsOn) {
                for (uint32_t j = 0; j < n; j++) {
                    if (dep == &nodes[j]) {
                        depIndices[i].push_back(j);
                        break;
                    }
                }
            }
        }

        std::vector<uint32_t> inDegree(n, 0);
        for (uint32_t i = 0; i < n; i++) {
            inDegree[i] = static_cast<uint32_t>(depIndices[i].size());
        }

        std::queue<uint32_t> ready;
        for (uint32_t i = 0; i < n; i++) {
            if (inDegree[i] == 0) { ready.push(i); }
        }

        std::vector<uint32_t> order;
        order.reserve(n);

        while (!ready.empty()) {
            uint32_t current = ready.front();
            ready.pop();
            order.push_back(current);

            for (uint32_t i = 0; i < n; i++) {
                for (uint32_t dep: depIndices[i]) {
                    if (dep == current) {
                        if (--inDegree[i] == 0) { ready.push(i); }
                    }
                }
            }
        }

        if (order.size() != n) {
            throw std::runtime_error("RenderGraph: cycle detected");
        }

        std::vector<Node> sorted;
        sorted.reserve(n);
        for (uint32_t i: order) {
            sorted.push_back(std::move(nodes[i]));
        }

        nodes = std::move(sorted);

        for (auto &node: nodes) {
            node.dependsOn.clear();
        }
    }

    VkImageLayout RenderGraph::writeLayoutFor(IRenderStage::Resource::Type type) {
        switch (type) {
            case IRenderStage::Resource::Type::COLOR_ATTACHMENT: return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            case IRenderStage::Resource::Type::DEPTH_ATTACHMENT: return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            case IRenderStage::Resource::Type::SHADER_WRITE: return VK_IMAGE_LAYOUT_GENERAL;
            default: return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
    }

    VkImageLayout RenderGraph::readLayoutFor(IRenderStage::Resource::Type type) {
        switch (type) {
            case IRenderStage::Resource::Type::DEPTH_ATTACHMENT:
            case IRenderStage::Resource::Type::SHADER_READ:
                return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            default:
                return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
    }

    VkAccessFlags RenderGraph::writeAccessFor(VkImageLayout layout) {
        switch (layout) {
            case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL: return VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
            case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL: return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            case VK_IMAGE_LAYOUT_GENERAL: return VK_ACCESS_SHADER_WRITE_BIT;
            default: return VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        }
    }

    VkPipelineStageFlags RenderGraph::writeStageFor(VkImageLayout layout) {
        switch (layout) {
            case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL: return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
            case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL: return VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            case VK_IMAGE_LAYOUT_GENERAL: return VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
            default: return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        }
    }

    VkImageAspectFlags RenderGraph::aspectFor(IRenderStage::Resource::Type type) {
        switch (type) {
            case IRenderStage::Resource::Type::DEPTH_ATTACHMENT: return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
            default: return VK_IMAGE_ASPECT_COLOR_BIT;
        }
    }
} // namespace Atlas
