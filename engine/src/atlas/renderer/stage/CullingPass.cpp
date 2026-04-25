#include "CullingPass.hpp"

#include <glm/gtc/matrix_inverse.hpp>

#include "IRenderStage.hpp"
#include "asset/AssetManager.hpp"

namespace Atlas {
    CullingPass::CullingPass(Device &device) : IRenderStage(Queue::COMPUTE), device(device) {
        opaqueIndirectCommands.reserve(MAX_OBJECTS);
        defaultWhiteHandle = AssetManager::get().createDefaultWhiteTexture();
    }

    void CullingPass::getDeclaredOutputs(std::vector<Resource::Description> &out) const {
        out.push_back(Resource::Description::vertexBuffer("scene_vertex_buffer", VERTEX_BUDGET));
        out.push_back(Resource::Description::indexBuffer("scene_index_buffer", INDEX_BUDGET));
        out.push_back(Resource::Description::cpuBuffer<std::vector<AssetHandle>>("texture_handles"));

        out.push_back({
            .name = "opaque_indirect_cmds",
            .type = Resource::Type::BUFFER_STORAGE,
            .bufferUsage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            .size = sizeof(VkDrawIndexedIndirectCommand) * MAX_OBJECTS,
            .hostVisible = true,
        });

        /*out.push_back({
            .name = "transparent_indirect_cmds",
            .type = Resource::Type::BUFFER_STORAGE,
            .bufferUsage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            .size = sizeof(VkDrawIndexedIndirectCommand) * MAX_OBJECTS,
            .hostVisible = true,
        });*/

        out.push_back({
            .name = "object_data_buffer",
            .type = Resource::Type::BUFFER_STORAGE,
            .bufferUsage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            .size = sizeof(GPUObjectData) * MAX_OBJECTS,
            .hostVisible = true,
        });

        // cluster_buffer — forward+ light/cluster assignment, produced by a future split pass
        // out.push_back({
        //     .name = "cluster_buffer",
        //     .type = Resource::Type::BUFFER_STORAGE,
        //     .bufferUsage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        //     .size = ...,
        //     .hostVisible = false,
        // });
    }

    void CullingPass::getDeclaredInputs(std::vector<std::string> & /*out*/) const {
    }

    void CullingPass::onResourcesCreated(const std::unordered_map<std::string, std::reference_wrapper<Resource> > &resources) {
        vertexBuffer = &resources.at("scene_vertex_buffer").get().asGPUBuffer();
        indexBuffer = &resources.at("scene_index_buffer").get().asGPUBuffer();

        textureHandles = &resources.at("texture_handles").get().asCPUBuffer().as<std::vector<AssetHandle>>();

        opaqueIndirectCmds = &resources.at("opaque_indirect_cmds").get().asGPUBuffer();
        opaqueIndirectCmds->map();
        std::memset(opaqueIndirectCmds->getMapped(), 0, sizeof(VkDrawIndexedIndirectCommand) * MAX_OBJECTS);

        /*transparentIndirectCmds = &resources.at("transparent_indirect_cmds").get().asGPUBuffer();
        transparentIndirectCmds->map();
        std::memset(transparentIndirectCmds->getMapped(), 0, sizeof(VkDrawIndexedIndirectCommand) * MAX_OBJECTS);*/

        objectDataBuffer = &resources.at("object_data_buffer").get().asGPUBuffer();
        objectDataBuffer->map();
        std::memset(objectDataBuffer->getMapped(), 0, sizeof(GPUObjectData) * MAX_OBJECTS);

        // clusterBuffer = &resources.at("cluster_buffer").get().asGPUBuffer();
    }

    void CullingPass::onSceneChanged(entt::registry &registry) {
        opaqueIndirectCommands.clear();
        transparentIndirectCommands.clear();

        textureHandles->clear();
        textureSlots.clear();

        textureHandles->push_back(defaultWhiteHandle);
        textureSlots[defaultWhiteHandle] = 0;

        std::vector<GPUObjectData> opaqueObjectData;
        opaqueObjectData.reserve(MAX_OBJECTS);

        uint32_t opaqueIndex = 0;
        uint32_t transparentIndex = 0;

        for (auto entity: registry.view<ModelComponent, TransformComponent, MaterialComponent>()) {
            auto &model = registry.get<ModelComponent>(entity);
            auto &transform = registry.get<TransformComponent>(entity);
            auto &material = registry.get<MaterialComponent>(entity);

            registerMesh(model.meshHandle);

            const auto it = meshAllocations.find(model.meshHandle);
            if (it == meshAllocations.end()) continue;
            const MeshAllocation &alloc = it->second;

            const bool isTransparent = material.baseColor.w < 1.0f;

            if (isTransparent) {
               continue;
            } else {
                const glm::mat4 model4 = transform.mat4();

                // register textures and get their indices
                const uint32_t albedoIdx    = registerTexture(material.albedoTexture);
                const uint32_t normalIdx    = registerTexture(material.normalMap);
                const uint32_t mrIdx        = registerTexture(material.metallicRoughnessMap);
                const uint32_t aoIdx        = registerTexture(material.ambientOcclusion);

                opaqueObjectData.push_back({
                    .modelMatrix = model4,
                    .normalMatrix = glm::mat4(glm::inverseTranspose(glm::mat3(model4))),
                    .textureIndices = glm::uvec4(albedoIdx, normalIdx, mrIdx, aoIdx),
                    .baseColor = material.baseColor,
                });

                opaqueIndirectCommands.push_back({
                    alloc.indexCount,
                    1,
                    alloc.firstIndex,
                    static_cast<int32_t>(alloc.firstVertex),
                    opaqueIndex++,
                });
            }

            if (opaqueIndirectCommands.size() >= MAX_OBJECTS) {
                break;
            }
        }

        const size_t usedBytes = sizeof(VkDrawIndexedIndirectCommand) * opaqueIndirectCommands.size();
        const size_t totalBytes = sizeof(VkDrawIndexedIndirectCommand) * MAX_OBJECTS;

        if (!opaqueIndirectCommands.empty()) {
            opaqueIndirectCmds->uploadData(opaqueIndirectCommands.data(), usedBytes);
        }

        if (usedBytes < totalBytes)
            std::memset(static_cast<uint8_t *>(opaqueIndirectCmds->getMapped()) + usedBytes, 0, totalBytes - usedBytes);

        if (!opaqueObjectData.empty()) {
            objectDataBuffer->uploadData(opaqueObjectData.data(), sizeof(GPUObjectData) * opaqueObjectData.size());
        }
    }

    void CullingPass::record(VkCommandBuffer /*cmd*/, VkDescriptorSet /*globalSet*/) {
    }

    void CullingPass::registerMesh(AssetHandle handle) {
        if (handle == INVALID_ASSET_HANDLE || meshAllocations.contains(handle)) return;

        const auto mesh = AssetManager::get().getMesh(handle);
        if (!mesh) return;

        const auto &vertices = mesh->getVertices();
        const auto &indices = mesh->getIndices();

        if (nextVertex + vertices.size() > VERTEX_BUDGET / sizeof(Mesh::Vertex))
            throw std::runtime_error("CullingPass: vertex buffer out of space");
        if (nextIndex + indices.size() > INDEX_BUDGET / sizeof(uint32_t))
            throw std::runtime_error("CullingPass: index buffer out of space");

        const MeshAllocation alloc{
            nextVertex, static_cast<uint32_t>(vertices.size()),
            nextIndex, static_cast<uint32_t>(indices.size()),
        };
        meshAllocations[handle] = alloc;

        const VkDeviceSize vSize = vertices.size() * sizeof(Mesh::Vertex);
        auto vStaging = GPUBuffer::Builder(device)
                .setSize(vSize)
                .setUsage(VK_BUFFER_USAGE_TRANSFER_SRC_BIT)
                .setMemoryUsage(VMA_MEMORY_USAGE_AUTO)
                .setAllocationFlags(VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT)
                .build();
        vStaging.uploadData(vertices.data(), vSize);
        GPUBuffer::copy(device, vStaging.get(), vertexBuffer->get(), vSize, 0, nextVertex * sizeof(Mesh::Vertex));

        const VkDeviceSize iSize = indices.size() * sizeof(uint32_t);
        auto iStaging = GPUBuffer::Builder(device)
                .setSize(iSize)
                .setUsage(VK_BUFFER_USAGE_TRANSFER_SRC_BIT)
                .setMemoryUsage(VMA_MEMORY_USAGE_AUTO)
                .setAllocationFlags(VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT)
                .build();
        iStaging.uploadData(indices.data(), iSize);
        GPUBuffer::copy(device, iStaging.get(), indexBuffer->get(), iSize, 0, nextIndex * sizeof(uint32_t));

        nextVertex += alloc.vertexCount;
        nextIndex += alloc.indexCount;
    }

    uint32_t CullingPass::registerTexture(AssetHandle handle) {
        if (handle == INVALID_ASSET_HANDLE)
            handle = defaultWhiteHandle;

        auto [it, inserted] = textureSlots.emplace(handle, static_cast<uint32_t>(textureHandles->size()));
        if (!inserted)
            return it->second; // already registered this frame

        textureHandles->push_back(handle);
        return it->second;
    }
} // namespace Atlas
