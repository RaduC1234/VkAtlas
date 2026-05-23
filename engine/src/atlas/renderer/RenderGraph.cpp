#include "RenderGraph.hpp"

#include "Renderer.hpp"

namespace Atlas {
    RenderGraph RenderGraph::Builder::build(Mode mode) {
        assert(width_ > 0 && height_ > 0);
        assert(!stages_.empty());

        RenderGraph graph(device, mode, width_, height_);
        graph.stages_ = std::move(stages_);
        graph.bake();
        return graph;
    }

    RenderGraph::RenderGraph(Device &device, Mode mode, uint32_t width, uint32_t height) : device(device), mode(mode), width(width), height(height) {
    }

    void RenderGraph::bake() {
        nodes_.clear();
        nodes_.reserve(stages_.size());

        for (auto &stage: stages_) {
            Node node;
            node.stage = stage.get();

            std::vector<IRenderStage::Resource::Description> outputs;
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
            std::vector<IRenderStage::Resource::Description> outputs;
            stage->getDeclaredOutputs(outputs);

            for (auto &output: outputs) {
                if (ownedResources_.contains(output.name)) {
                    continue;
                }

                if (output.kind() == IRenderStage::Resource::Kind::GPU_IMAGE) {
                    const uint32_t w = output.width ? output.width : width;
                    const uint32_t h = output.height ? output.height : height;

                    auto builder = GPUImage::Builder(device)
                            .setExtent(w, h)
                            .setFormat(output.format)
                            .setUsage(output.imageUsage)
                            .setDebugName(output.name);

                    if (output.type == IRenderStage::Resource::Type::ATTACHMENT_DEPTH) {
                        builder.addView(VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)
                                .addView(VK_IMAGE_ASPECT_STENCIL_BIT);
                    } else {
                        builder.addView(VK_IMAGE_ASPECT_COLOR_BIT);
                    }
                    ownedResources_.emplace(output.name, IRenderStage::Resource{output.type, std::move(builder.build())});
                }

                if (output.kind() == IRenderStage::Resource::Kind::GPU_BUFFER) {
                    GPUBuffer buf = output.hostVisible
                        ? GPUBuffer::Builder(device)
                              .setSize(output.size)
                              .setUsage(output.bufferUsage)
                              .setMemoryUsage(VMA_MEMORY_USAGE_AUTO)
                              .setAllocationFlags(VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                                                  VMA_ALLOCATION_CREATE_MAPPED_BIT)
                              .setMapped()
                              .build()
                        : GPUBuffer::Builder(device)
                              .setSize(output.size)
                              .setUsage(output.bufferUsage)
                              .setMemoryUsage(VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)
                              .build();

                    ownedResources_.emplace(output.name, IRenderStage::Resource{output.type, std::move(buf)});
                }

                if (output.kind() == IRenderStage::Resource::Kind::CPU_BUFFER) {
                    CPUBuffer buf = CPUBuffer::Builder()
                        .setSize(output.size)
                        .setInitialData(output.cpuInitial)
                        .build();

                    ownedResources_.emplace(output.name, IRenderStage::Resource{output.type, std::move(buf)});
                }
            }
        }

        std::unordered_map<std::string, std::reference_wrapper<IRenderStage::Resource> > refs;
        for (auto &[name, img]: ownedResources_) {
            refs.emplace(name, std::ref(img));
        }

        // Compute finalLayouts and lastWrittenBy by simulating the barrier pass
        std::unordered_map<std::string, VkImageLayout> finalLayouts;
        std::unordered_map<std::string, IRenderStage::Queue> lastWrittenBy;
        for (auto &node: nodes_) {
            std::vector<IRenderStage::Resource::Description> stageOutputs;
            node.stage->getDeclaredOutputs(stageOutputs);
            for (auto &output: stageOutputs) {
                finalLayouts[output.name] = writeLayoutFor(output.type);
                lastWrittenBy[output.name] = node.stage->queue();
            }
        }

        IRenderStage::Context ctx{refs, finalLayouts, lastWrittenBy};
        for (auto &stage: stages_) {
            stage->onResourcesCreated(ctx);
        }
    }

    void RenderGraph::build(entt::registry &registry) {
        for (auto &stage: stages_) {
            stage->onUpdate(registry);
        }
    }

    void RenderGraph::bakeBarriers() {
        std::unordered_map<std::string, VkImageLayout> currentLayouts;
        std::unordered_map<std::string, IRenderStage::Queue> lastWrittenBy;

        for (auto &node: nodes_) {
            std::vector<IRenderStage::Resource::Description> outputs;
            std::vector<std::string> inputs;
            node.stage->getDeclaredOutputs(outputs);
            node.stage->getDeclaredInputs(inputs);

            const bool isCompute = node.stage->queue() == IRenderStage::Queue::COMPUTE;

            for (auto &input: inputs) {
                auto resIt = ownedResources_.find(input);
                if (resIt == ownedResources_.end()) {
                    continue;
                }

                if (resIt->second.kind() == IRenderStage::Resource::Kind::GPU_BUFFER) {
                    if (resIt->second.type() == IRenderStage::Resource::Type::BUFFER_VERTEX || resIt->second.type() == IRenderStage::Resource::Type::BUFFER_INDEX) {
                        continue; // Vertex and index buffers are only read by the GPU
                    }

                    if (resIt->second.type() == IRenderStage::Resource::Type::BUFFER_STORAGE) {
                        Barrier barrier{};
                        barrier.resourceName = input;
                        barrier.isBuffer = true;
                        barrier.srcAccess = VK_ACCESS_SHADER_WRITE_BIT; // Assume the worst case: previous stage wrote to it.
                        barrier.dstAccess = VK_ACCESS_SHADER_READ_BIT;
                        barrier.srcStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
                        barrier.dstStage = isCompute
                                               ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
                                               : VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

                        node.barriersBeforeExec.push_back(barrier);
                        continue;
                    }

                    throw std::runtime_error("Unsupported buffer resource type for input: " + input);
                }

                auto layoutIt = currentLayouts.find(input);
                if (layoutIt == currentLayouts.end()) {
                    continue;
                }

                VkImageLayout srcLayout = layoutIt->second;
                auto resType = resIt->second.type(); // no more resourceTypes_ lookup

                VkImageLayout dstLayout = readLayoutFor(resType);
                if (srcLayout == dstLayout) { continue; }

                Barrier barrier{};
                barrier.resourceName = input;
                barrier.oldLayout = srcLayout;
                barrier.newLayout = dstLayout;
                barrier.srcAccess = writeAccessFor(srcLayout);
                barrier.dstAccess = VK_ACCESS_SHADER_READ_BIT;
                barrier.srcStage = writeStageFor(srcLayout);
                barrier.dstStage = isCompute ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
                barrier.aspect = aspectFor(resType);

                node.barriersBeforeExec.push_back(barrier);
                currentLayouts[input] = dstLayout;
            }

            for (auto &output: outputs) {
                currentLayouts[output.name] = writeLayoutFor(output.type);
                lastWrittenBy[output.name] = node.stage->queue();
            }
        }
    }

    void RenderGraph::render(const FrameContext frameContext, VkDescriptorSet globalSet) {
        for (auto &node: nodes_) {
            // Everything runs on the graphics command buffer.
            // The graphics queue family supports compute operations, so compute
            // stages record vkCmdDispatch into the same command buffer without
            // any queue ownership transfers.
            VkCommandBuffer cmd = frameContext.graphicsCommandBuffer;

            emitBarriers(cmd, node);
            node.stage->record(cmd, globalSet);
        }
    }

    void RenderGraph::emitBarriers(VkCommandBuffer cmd, const Node &node) const {
        std::vector<VkImageMemoryBarrier> imageBarriers;
        std::vector<VkBufferMemoryBarrier> bufferBarriers;
        VkPipelineStageFlags srcStages = 0;
        VkPipelineStageFlags dstStages = 0;

        for (auto &b: node.barriersBeforeExec) {
            auto it = ownedResources_.find(b.resourceName);
            if (it == ownedResources_.end()) {
                continue;
            }

            srcStages |= b.srcStage;
            dstStages |= b.dstStage;

            if (b.isBuffer) {
                VkBufferMemoryBarrier vkb{};
                vkb.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
                vkb.srcAccessMask = b.srcAccess;
                vkb.dstAccessMask = b.dstAccess;
                vkb.srcQueueFamilyIndex = b.requiresOwnershipAcquire ? device.queueFamilyIndices().computeFamily.value() : VK_QUEUE_FAMILY_IGNORED;
                vkb.dstQueueFamilyIndex = b.requiresOwnershipAcquire ? device.queueFamilyIndices().graphicsFamily.value() : VK_QUEUE_FAMILY_IGNORED;
                vkb.buffer = it->second.asBuffer().get();
                vkb.offset = 0;
                vkb.size = VK_WHOLE_SIZE;

                bufferBarriers.push_back(vkb);
            } else {
                VkImageMemoryBarrier vkb{};
                vkb.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                vkb.oldLayout = b.oldLayout;
                vkb.newLayout = b.newLayout;
                vkb.srcAccessMask = b.srcAccess;
                vkb.dstAccessMask = b.dstAccess;
                vkb.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                vkb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                vkb.image = it->second.asImage().image();
                vkb.subresourceRange = {b.aspect, 0, 1, 0, 1};

                imageBarriers.push_back(vkb);
            }
        }

        if (imageBarriers.empty() && bufferBarriers.empty()) {
            return;
        }

        if (srcStages == 0) {
            srcStages = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        }
        if (dstStages == 0) {
            dstStages = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        }

        vkCmdPipelineBarrier(cmd,
                             srcStages, dstStages, 0,
                             0, nullptr,
                             static_cast<uint32_t>(bufferBarriers.size()), bufferBarriers.data(),
                             static_cast<uint32_t>(imageBarriers.size()), imageBarriers.data());
    }


    void RenderGraph::topoSort(std::vector<Node> &nodes) {
        const auto n = static_cast<uint32_t>(nodes.size());

        std::vector<std::vector<uint32_t> > depIndices(n);
        for (uint32_t i = 0; i < n; i++) {
            for (const auto *dep: nodes[i].dependsOn) {
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
            if (inDegree[i] == 0) {
                ready.push(i);
            }
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
            case IRenderStage::Resource::Type::ATTACHMENT_COLOR: return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            case IRenderStage::Resource::Type::ATTACHMENT_DEPTH: return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            case IRenderStage::Resource::Type::SHADER_WRITE: return VK_IMAGE_LAYOUT_GENERAL;
            default: return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
    }

    VkImageLayout RenderGraph::readLayoutFor(IRenderStage::Resource::Type type) {
        switch (type) {
            case IRenderStage::Resource::Type::ATTACHMENT_DEPTH:
            case IRenderStage::Resource::Type::SHADER_READ:
                return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            case IRenderStage::Resource::Type::SHADER_WRITE:
                // Storage images written by compute stay in GENERAL.
                // Consumers (e.g. OutputStage) transition from GENERAL themselves.
                return VK_IMAGE_LAYOUT_GENERAL;
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
            case VK_IMAGE_LAYOUT_GENERAL: return VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR; // Could be compute or ray tracing wait for both
            default: return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        }
    }

    VkImageAspectFlags RenderGraph::aspectFor(IRenderStage::Resource::Type type) {
        switch (type) {
            case IRenderStage::Resource::Type::ATTACHMENT_DEPTH: return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
            default: return VK_IMAGE_ASPECT_COLOR_BIT;
        }
    }
} // namespace Atlas
