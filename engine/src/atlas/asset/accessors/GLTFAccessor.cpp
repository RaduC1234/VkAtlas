#include "GLTFAccessor.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <sstream>
#include <utility>

#include <stb_image.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include "core/Log.hpp"
#include "entity/Object.hpp"

namespace Atlas {
    constexpr float GLTF_EPSILON = 1.0e-8f;

    struct GLTFAccessorView {
        const unsigned char *data = nullptr;
        size_t count = 0;
        size_t stride = 0;
        int componentType = -1;
        int type = -1;
        bool normalized = false;
    };

    bool gltfHasLength(const glm::vec3 &value) {
        return glm::dot(value, value) > GLTF_EPSILON;
    }

    glm::vec3 gltfSafeNormal(const glm::vec3 &normal) {
        return gltfHasLength(normal) ? glm::normalize(normal) : glm::vec3(0.0f, 1.0f, 0.0f);
    }

    glm::vec4 gltfFallbackTangent(const glm::vec3 &normal) {
        const glm::vec3 n = gltfSafeNormal(normal);
        const glm::vec3 helper = std::abs(n.y) < 0.999f
                                     ? glm::vec3(0.0f, 1.0f, 0.0f)
                                     : glm::vec3(1.0f, 0.0f, 0.0f);
        return glm::vec4(glm::normalize(glm::cross(helper, n)), 1.0f);
    }

    std::string gltfLowercaseExtension(const std::filesystem::path &path) {
        std::string ext = path.extension().string();
        std::ranges::transform(ext, ext.begin(), [](const unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        return ext;
    }

    std::vector<std::byte> gltfToBytes(const std::string &text) {
        std::vector<std::byte> bytes;
        bytes.reserve(text.size());
        for (const char ch: text) {
            bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));
        }
        return bytes;
    }

    GLTFAccessorView gltfGetAccessorView(const tinygltf::Model &model, const int accessorIndex) {
        GLTFAccessorView result{};

        if (accessorIndex < 0 || accessorIndex >= static_cast<int>(model.accessors.size())) {
            return result;
        }

        const tinygltf::Accessor &accessor = model.accessors[accessorIndex];
        if (accessor.bufferView < 0 || accessor.bufferView >= static_cast<int>(model.bufferViews.size())) {
            return result;
        }

        const tinygltf::BufferView &view = model.bufferViews[accessor.bufferView];
        if (view.buffer < 0 || view.buffer >= static_cast<int>(model.buffers.size())) {
            return result;
        }

        const tinygltf::Buffer &buffer = model.buffers[view.buffer];
        const size_t byteOffset = view.byteOffset + accessor.byteOffset;
        if (byteOffset >= buffer.data.size()) {
            return result;
        }

        const int stride = accessor.ByteStride(view);
        if (stride <= 0) {
            return result;
        }

        result.data = buffer.data.data() + byteOffset;
        result.count = accessor.count;
        result.stride = static_cast<size_t>(stride);
        result.componentType = accessor.componentType;
        result.type = accessor.type;
        result.normalized = accessor.normalized;
        return result;
    }

    float gltfReadComponent(const unsigned char *ptr, const int componentType, const bool normalized) {
        switch (componentType) {
            case TINYGLTF_COMPONENT_TYPE_BYTE: {
                int8_t value = 0;
                std::memcpy(&value, ptr, sizeof(value));
                return normalized ? std::max(static_cast<float>(value) / 127.0f, -1.0f) : static_cast<float>(value);
            }
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: {
                uint8_t value = 0;
                std::memcpy(&value, ptr, sizeof(value));
                return normalized ? static_cast<float>(value) / 255.0f : static_cast<float>(value);
            }
            case TINYGLTF_COMPONENT_TYPE_SHORT: {
                int16_t value = 0;
                std::memcpy(&value, ptr, sizeof(value));
                return normalized ? std::max(static_cast<float>(value) / 32767.0f, -1.0f) : static_cast<float>(value);
            }
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
                uint16_t value = 0;
                std::memcpy(&value, ptr, sizeof(value));
                return normalized ? static_cast<float>(value) / 65535.0f : static_cast<float>(value);
            }
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: {
                uint32_t value = 0;
                std::memcpy(&value, ptr, sizeof(value));
                return static_cast<float>(value);
            }
            case TINYGLTF_COMPONENT_TYPE_FLOAT: {
                float value = 0.0f;
                std::memcpy(&value, ptr, sizeof(value));
                return value;
            }
            default:
                return 0.0f;
        }
    }

    float gltfReadFloat(const GLTFAccessorView &view, const size_t index, const size_t component) {
        if (!view.data || index >= view.count) {
            return 0.0f;
        }

        const int componentSize = tinygltf::GetComponentSizeInBytes(static_cast<uint32_t>(view.componentType));
        if (componentSize <= 0) {
            return 0.0f;
        }

        return gltfReadComponent(view.data + index * view.stride + component * static_cast<size_t>(componentSize), view.componentType, view.normalized);
    }

    glm::vec2 gltfReadVec2(const GLTFAccessorView &view, const size_t index, const glm::vec2 fallback = glm::vec2(0.0f)) {
        if (!view.data) {
            return fallback;
        }

        return glm::vec2(
            gltfReadFloat(view, index, 0),
            gltfReadFloat(view, index, 1));
    }

    glm::vec3 gltfReadVec3(const GLTFAccessorView &view, const size_t index, const glm::vec3 fallback = glm::vec3(0.0f)) {
        if (!view.data) {
            return fallback;
        }

        return glm::vec3(
            gltfReadFloat(view, index, 0),
            gltfReadFloat(view, index, 1),
            gltfReadFloat(view, index, 2));
    }

    glm::vec4 gltfReadVec4(const GLTFAccessorView &view, const size_t index, const glm::vec4 fallback = glm::vec4(0.0f)) {
        if (!view.data) {
            return fallback;
        }

        const int components = tinygltf::GetNumComponentsInType(static_cast<uint32_t>(view.type));
        return glm::vec4(
            gltfReadFloat(view, index, 0),
            gltfReadFloat(view, index, 1),
            gltfReadFloat(view, index, 2),
            components >= 4 ? gltfReadFloat(view, index, 3) : fallback.w);
    }

    uint32_t gltfReadIndex(const GLTFAccessorView &view, const size_t index) {
        if (!view.data || index >= view.count) {
            return 0;
        }

        const unsigned char *ptr = view.data + index * view.stride;

        switch (view.componentType) {
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: {
                uint8_t value = 0;
                std::memcpy(&value, ptr, sizeof(value));
                return static_cast<uint32_t>(value);
            }
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
                uint16_t value = 0;
                std::memcpy(&value, ptr, sizeof(value));
                return static_cast<uint32_t>(value);
            }
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: {
                uint32_t value = 0;
                std::memcpy(&value, ptr, sizeof(value));
                return value;
            }
            default:
                return 0;
        }
    }

    void gltfFinalizeVertexFrames(std::vector<Mesh::Vertex> &vertices, const std::vector<uint32_t> &indices) {
        std::vector<glm::vec3> normalSums(vertices.size(), glm::vec3(0.0f));
        std::vector<glm::vec3> tangentSums(vertices.size(), glm::vec3(0.0f));
        std::vector<glm::vec3> bitangentSums(vertices.size(), glm::vec3(0.0f));

        for (size_t i = 0; i + 2 < indices.size(); i += 3) {
            const uint32_t i0 = indices[i + 0];
            const uint32_t i1 = indices[i + 1];
            const uint32_t i2 = indices[i + 2];
            if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size()) {
                continue;
            }

            const glm::vec3 p0 = vertices[i0].position;
            const glm::vec3 p1 = vertices[i1].position;
            const glm::vec3 p2 = vertices[i2].position;
            const glm::vec3 edge1 = p1 - p0;
            const glm::vec3 edge2 = p2 - p0;

            const glm::vec3 faceNormal = glm::cross(edge1, edge2);
            if (gltfHasLength(faceNormal)) {
                const glm::vec3 n = glm::normalize(faceNormal);
                normalSums[i0] += n;
                normalSums[i1] += n;
                normalSums[i2] += n;
            }

            const glm::vec2 uv0 = vertices[i0].uv;
            const glm::vec2 uv1 = vertices[i1].uv;
            const glm::vec2 uv2 = vertices[i2].uv;
            const glm::vec2 deltaUv1 = uv1 - uv0;
            const glm::vec2 deltaUv2 = uv2 - uv0;
            const float determinant = deltaUv1.x * deltaUv2.y - deltaUv1.y * deltaUv2.x;

            if (std::abs(determinant) > GLTF_EPSILON) {
                const float invDet = 1.0f / determinant;
                const glm::vec3 tangent = (edge1 * deltaUv2.y - edge2 * deltaUv1.y) * invDet;
                const glm::vec3 bitangent = (edge2 * deltaUv1.x - edge1 * deltaUv2.x) * invDet;

                tangentSums[i0] += tangent;
                tangentSums[i1] += tangent;
                tangentSums[i2] += tangent;
                bitangentSums[i0] += bitangent;
                bitangentSums[i1] += bitangent;
                bitangentSums[i2] += bitangent;
            }
        }

        for (size_t i = 0; i < vertices.size(); ++i) {
            auto &vertex = vertices[i];
            vertex.normal = gltfHasLength(vertex.normal) ? glm::normalize(vertex.normal) : gltfSafeNormal(normalSums[i]);

            if (gltfHasLength(glm::vec3(vertex.tangent))) {
                vertex.tangent = glm::vec4(glm::normalize(glm::vec3(vertex.tangent)), vertex.tangent.w);
                continue;
            }

            glm::vec3 tangent = tangentSums[i];
            if (gltfHasLength(tangent)) {
                tangent = tangent - vertex.normal * glm::dot(vertex.normal, tangent);
                if (gltfHasLength(tangent)) {
                    tangent = glm::normalize(tangent);
                    const float handedness = glm::dot(glm::cross(vertex.normal, tangent), bitangentSums[i]) < 0.0f ? -1.0f : 1.0f;
                    vertex.tangent = glm::vec4(tangent, handedness);
                    continue;
                }
            }

            vertex.tangent = gltfFallbackTangent(vertex.normal);
        }
    }

    std::vector<std::byte> gltfImageToRgba(const tinygltf::Image &image) {
        if (image.image.empty() || image.width <= 0 || image.height <= 0 || image.bits != 8) {
            return {};
        }

        const size_t pixelCount = static_cast<size_t>(image.width) * static_cast<size_t>(image.height);
        std::vector<std::byte> rgba(pixelCount * 4);
        const int componentCount = std::max(1, image.component);

        for (size_t pixel = 0; pixel < pixelCount; ++pixel) {
            const size_t src = pixel * static_cast<size_t>(componentCount);
            const size_t dst = pixel * 4;

            const unsigned char r = image.image[src + 0];
            const unsigned char g = componentCount > 1 ? image.image[src + 1] : r;
            const unsigned char b = componentCount > 2 ? image.image[src + 2] : r;
            const unsigned char a = componentCount > 3 ? image.image[src + 3] : 255;

            rgba[dst + 0] = static_cast<std::byte>(r);
            rgba[dst + 1] = static_cast<std::byte>(g);
            rgba[dst + 2] = static_cast<std::byte>(b);
            rgba[dst + 3] = static_cast<std::byte>(a);
        }

        return rgba;
    }

    std::vector<std::byte> gltfDecodeImageToRgba(const tinygltf::Image &image, int &width, int &height) {
        width = 0;
        height = 0;

        if (image.image.empty() || image.image.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
            return {};
        }

        if (!image.as_is) {
            width = image.width;
            height = image.height;
            return gltfImageToRgba(image);
        }

        int componentCount = 0;
        unsigned char *decoded = stbi_load_from_memory(
            image.image.data(),
            static_cast<int>(image.image.size()),
            &width,
            &height,
            &componentCount,
            4);

        if (!decoded || width <= 0 || height <= 0) {
            if (decoded) {
                stbi_image_free(decoded);
            }
            return {};
        }

        const size_t byteCount = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
        std::vector<std::byte> rgba(byteCount);
        std::memcpy(rgba.data(), decoded, byteCount);
        stbi_image_free(decoded);
        return rgba;
    }

    void gltfAttachToParent(entt::registry &registry, const entt::entity parentEntity, const entt::entity entity) {
        if (parentEntity == entt::null || !registry.valid(parentEntity)) {
            return;
        }

        if (auto *parent = registry.try_get<SceneNodeComponent>(parentEntity)) {
            parent->children.push_back(entity);
        }
    }

    void gltfApplyWorldTransform(entt::registry &registry, const entt::entity entity, const glm::mat4 &worldTransform) {
        glm::vec3 scale;
        glm::quat rotation;
        glm::vec3 translation;
        glm::vec3 skew;
        glm::vec4 perspective;
        glm::decompose(worldTransform, scale, rotation, translation, skew, perspective);

        auto &transform = registry.emplace<TransformComponent>(entity);
        transform.translation = translation;
        transform.rotation = glm::eulerAngles(rotation);
        transform.scale = scale;
    }

    void gltfApplyMaterial(
        MaterialComponent &material,
        const tinygltf::Model &model,
        const tinygltf::Primitive &primitive,
        const std::vector<AssetHandle<Texture>> &imageHandles) {
        if (primitive.material < 0 || primitive.material >= static_cast<int>(model.materials.size())) {
            return;
        }

        const tinygltf::Material &gltfMaterial = model.materials[primitive.material];
        const auto &pbr = gltfMaterial.pbrMetallicRoughness;

        if (pbr.baseColorFactor.size() == 4) {
            material.baseColor = glm::vec4(
                static_cast<float>(pbr.baseColorFactor[0]),
                static_cast<float>(pbr.baseColorFactor[1]),
                static_cast<float>(pbr.baseColorFactor[2]),
                static_cast<float>(pbr.baseColorFactor[3]));
        }

        material.alphaMasked = gltfMaterial.alphaMode == "MASK";
        material.transparent = gltfMaterial.alphaMode == "BLEND" || material.baseColor.a < 1.0f;

        const auto resolve = [&model, &imageHandles](const int textureIndex) {
            if (textureIndex < 0 || textureIndex >= static_cast<int>(model.textures.size())) {
                return AssetHandle<Texture>::invalid();
            }

            const int imageIndex = model.textures[textureIndex].source;
            if (imageIndex < 0 || imageIndex >= static_cast<int>(imageHandles.size())) {
                return AssetHandle<Texture>::invalid();
            }

            return imageHandles[imageIndex];
        };

        material.albedoTexture = resolve(pbr.baseColorTexture.index);
        material.normalMap = resolve(gltfMaterial.normalTexture.index);
        material.metallicRoughnessMap = resolve(pbr.metallicRoughnessTexture.index);
        material.ambientOcclusion = resolve(gltfMaterial.occlusionTexture.index);
    }

    GLTFAccessor::GLTFAccessor(AssetManager &assets, ExecutorService &service) : assets(assets), executor(service) {
    }

    GLTFAccessor::~GLTFAccessor() {
        std::vector<std::future<void>> jobs;
        {
            std::lock_guard lock(textureJobsMutex);
            jobs = std::move(textureJobs);
        }

        for (auto &job: jobs) {
            if (job.valid()) {
                job.wait();
            }
        }
    }

    std::vector<entt::entity> GLTFAccessor::importAsset(
        const std::string &path,
        entt::registry &registry,
        entt::entity parentEntity) {
        const std::filesystem::path fullPath = assets.rootPath() / path;
        const std::string ext = gltfLowercaseExtension(fullPath);

        tinygltf::TinyGLTF loader;
        loader.SetImagesAsIs(true);

        tinygltf::Model model;
        std::string err;
        std::string warn;

        const bool loaded = ext == ".glb"
                                ? loader.LoadBinaryFromFile(&model, &err, &warn, fullPath.string())
                                : loader.LoadASCIIFromFile(&model, &err, &warn, fullPath.string());

        if (!warn.empty()) {
            AT_WARN("GLTFAccessor: {}: {}", path, warn);
        }

        if (!loaded) {
            AT_ERROR("GLTFAccessor: failed to load {}: {}", path, err);
            return {};
        }

        std::vector<AssetHandle<Texture>> imageHandles(model.images.size());
        for (size_t imageIndex = 0; imageIndex < model.images.size(); ++imageIndex) {
            imageHandles[imageIndex] = assets.createTexturePlaceholder();
        }

        std::vector<std::vector<AssetHandle<Mesh>>> meshHandles(model.meshes.size());
        for (size_t meshIndex = 0; meshIndex < model.meshes.size(); ++meshIndex) {
            const tinygltf::Mesh &mesh = model.meshes[meshIndex];
            auto &primitiveHandles = meshHandles[meshIndex];
            primitiveHandles.reserve(mesh.primitives.size());

            for (size_t primitiveIndex = 0; primitiveIndex < mesh.primitives.size(); ++primitiveIndex) {
                const tinygltf::Primitive &primitive = mesh.primitives[primitiveIndex];
                primitiveHandles.push_back(AssetHandle<Mesh>::invalid());

                if (primitive.mode != TINYGLTF_MODE_TRIANGLES) {
                    AT_WARN("GLTFAccessor: skipping non-triangle primitive in mesh {}", meshIndex);
                    continue;
                }

                const auto positionIt = primitive.attributes.find("POSITION");
                if (positionIt == primitive.attributes.end()) {
                    AT_WARN("GLTFAccessor: skipping primitive without POSITION in mesh {}", meshIndex);
                    continue;
                }

                const GLTFAccessorView positions = gltfGetAccessorView(model, positionIt->second);
                if (!positions.data || positions.type != TINYGLTF_TYPE_VEC3) {
                    AT_WARN("GLTFAccessor: skipping primitive with invalid POSITION in mesh {}", meshIndex);
                    continue;
                }

                GLTFAccessorView normals{};
                GLTFAccessorView texcoords{};
                GLTFAccessorView colors{};
                GLTFAccessorView tangents{};

                if (const auto it = primitive.attributes.find("NORMAL"); it != primitive.attributes.end()) {
                    normals = gltfGetAccessorView(model, it->second);
                }
                if (const auto it = primitive.attributes.find("TEXCOORD_0"); it != primitive.attributes.end()) {
                    texcoords = gltfGetAccessorView(model, it->second);
                }
                if (const auto it = primitive.attributes.find("COLOR_0"); it != primitive.attributes.end()) {
                    colors = gltfGetAccessorView(model, it->second);
                }
                if (const auto it = primitive.attributes.find("TANGENT"); it != primitive.attributes.end()) {
                    tangents = gltfGetAccessorView(model, it->second);
                }

                std::vector<Mesh::Vertex> vertices;
                vertices.reserve(positions.count);

                for (size_t vertexIndex = 0; vertexIndex < positions.count; ++vertexIndex) {
                    Mesh::Vertex vertex{};

                    const glm::vec3 position = gltfReadVec3(positions, vertexIndex);
                    vertex.position = glm::vec3(-position.x, -position.y, position.z);

                    if (normals.data) {
                        const glm::vec3 normal = gltfReadVec3(normals, vertexIndex);
                        vertex.normal = gltfSafeNormal(glm::vec3(-normal.x, -normal.y, normal.z));
                    }

                    if (texcoords.data) {
                        const glm::vec2 uv = gltfReadVec2(texcoords, vertexIndex);
                        vertex.uv = glm::vec2(uv.x, 1.0f - uv.y);
                    }

                    if (colors.data) {
                        vertex.color = glm::vec3(gltfReadVec4(colors, vertexIndex, glm::vec4(1.0f)));
                    } else {
                        vertex.color = glm::vec3(1.0f);
                    }

                    if (tangents.data) {
                        const glm::vec4 tangent = gltfReadVec4(tangents, vertexIndex, glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
                        glm::vec3 tangentAxis = glm::vec3(-tangent.x, -tangent.y, tangent.z);
                        if (gltfHasLength(tangentAxis)) {
                            tangentAxis = glm::normalize(tangentAxis);
                            vertex.tangent = glm::vec4(tangentAxis, -tangent.w);
                        }
                    }

                    vertices.push_back(vertex);
                }

                std::vector<uint32_t> indices;
                if (primitive.indices >= 0) {
                    const GLTFAccessorView indexView = gltfGetAccessorView(model, primitive.indices);
                    indices.reserve(indexView.count);
                    for (size_t index = 0; index < indexView.count; ++index) {
                        indices.push_back(gltfReadIndex(indexView, index));
                    }
                } else {
                    indices.reserve(vertices.size());
                    for (uint32_t index = 0; index < static_cast<uint32_t>(vertices.size()); ++index) {
                        indices.push_back(index);
                    }
                }

                gltfFinalizeVertexFrames(vertices, indices);
                primitiveHandles.back() = assets.createMesh(std::move(vertices), std::move(indices));
            }
        }

        std::vector<entt::entity> entities;
        if (!model.scenes.empty()) {
            const int sceneIndex = model.defaultScene >= 0 ? model.defaultScene : 0;
            if (sceneIndex >= 0 && sceneIndex < static_cast<int>(model.scenes.size())) {
                for (const int nodeIndex: model.scenes[sceneIndex].nodes) {
                    processNode(registry, model, nodeIndex, glm::mat4(1.0f), parentEntity, meshHandles, imageHandles, entities);
                }
            }
        } else {
            for (size_t nodeIndex = 0; nodeIndex < model.nodes.size(); ++nodeIndex) {
                processNode(registry, model, static_cast<int32_t>(nodeIndex), glm::mat4(1.0f), parentEntity, meshHandles, imageHandles, entities);
            }
        }

        if (model.extensions.contains("ATLAS_skybox")) {
            const entt::entity entity = registry.create();
            auto &node = registry.emplace<SceneNodeComponent>(entity);
            node.name = "Skybox";
            node.parent = parentEntity;
            gltfAttachToParent(registry, parentEntity, entity);
            handleSkybox(registry, entity, model);
            entities.push_back(entity);
        }

        if (model.extensions.contains("ATLAS_post_processing")) {
            const entt::entity entity = registry.create();
            auto &node = registry.emplace<SceneNodeComponent>(entity);
            node.name = "PostProcessing";
            node.parent = parentEntity;
            gltfAttachToParent(registry, parentEntity, entity);
            handlePostProcessing(registry, entity, model);
            entities.push_back(entity);
        }

        scheduleTextureDecode(std::move(model.images), imageHandles, path);

        AT_INFO("GLTFAccessor: created {} entities from {}", entities.size(), path);
        return entities;
    }

    std::vector<std::byte> GLTFAccessor::exportAsset(
        const std::vector<entt::entity> &entities,
        const entt::registry &registry) {
        (void)registry;

        std::ostringstream gltf;
        gltf << "{\"asset\":{\"version\":\"2.0\",\"generator\":\"Atlas\"},";
        gltf << "\"extras\":{\"note\":\"glTF export is temporarily disabled while typed AssetHandle export paths are rebuilt\",";
        gltf << "\"requestedEntities\":" << entities.size() << "}}";

        AT_WARN("GLTFAccessor: export is temporarily disabled for the typed asset system");
        return gltfToBytes(gltf.str());
    }

    void GLTFAccessor::scheduleTextureDecode(
        std::vector<tinygltf::Image> images,
        std::vector<AssetHandle<Texture>> imageHandles,
        const std::string &path) {
        if (images.empty() || imageHandles.empty()) {
            return;
        }

        AssetManager *assetManager = &assets;
        auto job = executor.submit([assetManager, images = std::move(images), imageHandles = std::move(imageHandles), path]() mutable {
            const size_t count = std::min(images.size(), imageHandles.size());
            size_t created = 0;

            for (size_t imageIndex = 0; imageIndex < count; ++imageIndex) {
                int width = 0;
                int height = 0;
                std::vector<std::byte> pixels = gltfDecodeImageToRgba(images[imageIndex], width, height);
                if (pixels.empty()) {
                    AT_WARN("GLTFAccessor: skipping image {} from {} because it has unsupported data", imageIndex, path);
                    constexpr uint8_t white[4] = {255, 255, 255, 255};
                    pixels.assign(
                        reinterpret_cast<const std::byte *>(white),
                        reinterpret_cast<const std::byte *>(white) + 4);
                    width = 1;
                    height = 1;
                }

                assetManager->fulfillTexture(
                    imageHandles[imageIndex],
                    std::move(pixels),
                    static_cast<uint32_t>(width),
                    static_cast<uint32_t>(height),
                    VK_FORMAT_R8G8B8A8_SRGB,
                    VK_SAMPLER_ADDRESS_MODE_REPEAT);
                ++created;
            }

            AT_INFO("GLTFAccessor: decoded {} textures from {}", created, path);
        });

        std::lock_guard lock(textureJobsMutex);
        textureJobs.push_back(std::move(job));
    }

    void GLTFAccessor::processNode(
        entt::registry &registry,
        const tinygltf::Model &model,
        const int32_t nodeIdx,
        const glm::mat4 &parentTransform,
        const entt::entity parentEntity,
        const std::vector<std::vector<AssetHandle<Mesh>>> &meshHandles,
        const std::vector<AssetHandle<Texture>> &imageHandles,
        std::vector<entt::entity> &outEntities) {
        if (nodeIdx < 0 || nodeIdx >= static_cast<int32_t>(model.nodes.size())) {
            return;
        }

        const tinygltf::Node &node = model.nodes[nodeIdx];
        const glm::mat4 worldTransform = parentTransform * getNodeTransform(node);
        entt::entity childParent = parentEntity;

        if (node.mesh >= 0 && node.mesh < static_cast<int>(model.meshes.size()) && node.mesh < static_cast<int>(meshHandles.size())) {
            const tinygltf::Mesh &mesh = model.meshes[node.mesh];
            const auto &handles = meshHandles[node.mesh];

            for (size_t primitiveIndex = 0; primitiveIndex < handles.size(); ++primitiveIndex) {
                const AssetHandle<Mesh> meshHandle = handles[primitiveIndex];
                if (!meshHandle) {
                    continue;
                }

                const entt::entity entity = registry.create();

                auto &sceneNode = registry.emplace<SceneNodeComponent>(entity);
                sceneNode.name = !node.name.empty()
                                     ? node.name
                                     : (!mesh.name.empty() ? mesh.name : "GLTF_Node_" + std::to_string(nodeIdx));
                if (handles.size() > 1) {
                    sceneNode.name += "_prim" + std::to_string(primitiveIndex);
                }
                sceneNode.parent = parentEntity;
                gltfAttachToParent(registry, parentEntity, entity);

                gltfApplyWorldTransform(registry, entity, worldTransform);

                auto &modelComponent = registry.emplace<ModelComponent>(entity);
                modelComponent.meshHandle = meshHandle;

                auto &material = registry.emplace<MaterialComponent>(entity);
                if (primitiveIndex < mesh.primitives.size()) {
                    gltfApplyMaterial(material, model, mesh.primitives[primitiveIndex], imageHandles);
                }

                if (childParent == parentEntity) {
                    childParent = entity;
                }

                outEntities.push_back(entity);
            }
        }

        if (node.light >= 0 && node.light < static_cast<int>(model.lights.size())) {
            const tinygltf::Light &gltfLight = model.lights[node.light];
            const entt::entity entity = registry.create();

            auto &sceneNode = registry.emplace<SceneNodeComponent>(entity);
            sceneNode.name = gltfLight.name.empty() ? "Light" : gltfLight.name;
            sceneNode.parent = parentEntity;
            gltfAttachToParent(registry, parentEntity, entity);

            gltfApplyWorldTransform(registry, entity, worldTransform);

            auto &light = registry.emplace<LightComponent>(entity);
            if (gltfLight.type == "point") {
                light.type = LightType::POINT;
            } else if (gltfLight.type == "spot") {
                light.type = LightType::SPOT;
                light.innerConeAngle = static_cast<float>(gltfLight.spot.innerConeAngle);
                light.outerConeAngle = static_cast<float>(gltfLight.spot.outerConeAngle);
            } else if (gltfLight.type == "directional") {
                light.type = LightType::DIRECTIONAL;
            }

            if (gltfLight.color.size() == 3) {
                light.color = glm::vec3(
                    static_cast<float>(gltfLight.color[0]),
                    static_cast<float>(gltfLight.color[1]),
                    static_cast<float>(gltfLight.color[2]));
            }
            light.intensity = static_cast<float>(gltfLight.intensity);
            light.range = static_cast<float>(gltfLight.range);

            const glm::vec3 direction = glm::normalize(glm::vec3(worldTransform * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
            light.direction = glm::vec3(-direction.x, -direction.y, direction.z);

            if (childParent == parentEntity) {
                childParent = entity;
            }

            outEntities.push_back(entity);
        }

        for (const int childIdx: node.children) {
            processNode(registry, model, childIdx, worldTransform, childParent, meshHandles, imageHandles, outEntities);
        }
    }

    void GLTFAccessor::handleSkybox(
        entt::registry &registry,
        entt::entity entity,
        const tinygltf::Model &model) {
        (void)model;

        registry.emplace<SkyboxComponent>(entity);
        AT_WARN("GLTFAccessor: ATLAS_skybox import is waiting for typed cubemap file loading");
    }

    void GLTFAccessor::handlePostProcessing(
        entt::registry &registry,
        entt::entity entity,
        const tinygltf::Model &model) {
        auto ppIt = model.extensions.find("ATLAS_post_processing");
        if (ppIt == model.extensions.end()) {
            return;
        }

        const auto &ext = ppIt->second;
        auto &pp = registry.emplace<PostProcessingVolumeComponent>(entity);

        const auto getFloat = [&ext](const char *key, const float fallback) {
            return ext.Has(key) && ext.Get(key).IsNumber()
                       ? static_cast<float>(ext.Get(key).Get<double>())
                       : fallback;
        };

        pp.exposure = getFloat("exposure", 1.0f);
        pp.contrast = getFloat("contrast", 1.0f);
        pp.saturation = getFloat("saturation", 1.0f);

        if (ext.Has("colorTint") && ext.Get("colorTint").IsArray()) {
            const auto &arr = ext.Get("colorTint").Get<tinygltf::Value::Array>();
            if (arr.size() >= 3 && arr[0].IsNumber() && arr[1].IsNumber() && arr[2].IsNumber()) {
                pp.colorTint = glm::vec3(
                    static_cast<float>(arr[0].Get<double>()),
                    static_cast<float>(arr[1].Get<double>()),
                    static_cast<float>(arr[2].Get<double>()));
            }
        }
    }

    AssetHandle<Texture> GLTFAccessor::resolveTexture(
        const tinygltf::Model &model,
        const int texIdx,
        const std::vector<AssetHandle<Texture>> &imageHandles) {
        if (texIdx < 0 || texIdx >= static_cast<int>(model.textures.size())) {
            return AssetHandle<Texture>::invalid();
        }

        const int imageIndex = model.textures[texIdx].source;
        if (imageIndex < 0 || imageIndex >= static_cast<int>(imageHandles.size())) {
            return AssetHandle<Texture>::invalid();
        }

        return imageHandles[imageIndex];
    }

    glm::mat4 GLTFAccessor::getNodeTransform(const tinygltf::Node &node) {
        glm::mat4 mat(1.0f);

        if (node.matrix.size() == 16) {
            mat = glm::make_mat4x4(node.matrix.data());
        } else {
            if (node.translation.size() == 3) {
                glm::vec3 translation(
                    static_cast<float>(node.translation[0]),
                    static_cast<float>(node.translation[1]),
                    static_cast<float>(node.translation[2]));
                translation = glm::vec3(-translation.x, -translation.y, translation.z);
                mat = glm::translate(mat, translation);
            }

            if (node.rotation.size() == 4) {
                const glm::quat rotation(
                    static_cast<float>(node.rotation[3]),
                    static_cast<float>(node.rotation[0]),
                    static_cast<float>(node.rotation[1]),
                    static_cast<float>(node.rotation[2]));
                mat *= glm::mat4_cast(rotation);
            }

            if (node.scale.size() == 3) {
                mat = glm::scale(mat, glm::vec3(
                                     static_cast<float>(node.scale[0]),
                                     static_cast<float>(node.scale[1]),
                                     static_cast<float>(node.scale[2])));
            }
        }

        return mat;
    }
}
