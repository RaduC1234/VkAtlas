#include "GLTFAccessor.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <mutex>
#include <numeric>
#include <utility>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <stb_image.h>

#include "core/Log.hpp"
#include "entity/Object.hpp"
#include "asset/Texture.hpp"
#include "asset/Mesh.hpp"

namespace Atlas {
    namespace {
        std::string toLower(std::string value) {
            std::ranges::transform(value, value.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return value;
        }

        glm::vec3 toEngineDirection(const glm::vec3 &direction) {
            glm::vec3 converted{-direction.x, -direction.y, direction.z};
            const float len2 = glm::dot(converted, converted);
            return len2 > 0.0f ? converted * glm::inversesqrt(len2) : converted;
        }

        bool tinyValueNumber(const tinygltf::Value &value) {
            return value.IsNumber();
        }

        float tinyValueFloat(const tinygltf::Value &value, float fallback = 0.0f) {
            return tinyValueNumber(value) ? static_cast<float>(value.GetNumberAsDouble()) : fallback;
        }

        std::filesystem::path resolveAssetPath(const AssetManager &assets, const std::string &path) {
            const std::filesystem::path requested(path);
            if (requested.is_absolute()) {
                return requested;
            }

            if (!assets.rootPath().empty()) {
                const auto candidate = assets.rootPath() / requested;
                if (std::filesystem::exists(candidate)) {
                    return candidate;
                }
            }

            for (auto directory = std::filesystem::current_path(); !directory.empty(); directory = directory.parent_path()) {
                const auto candidate = directory / "assets" / requested;
                if (std::filesystem::exists(candidate)) {
                    return candidate;
                }

                if (directory == directory.root_path()) {
                    break;
                }
            }

            return assets.rootPath().empty()
                ? std::filesystem::current_path() / "assets" / requested
                : assets.rootPath() / requested;
        }
    }

    GLTFAccessor::GLTFAccessor(AssetManager &assets, ExecutorService &service) : assets(assets), executor(service) {}

    std::vector<std::byte> GLTFAccessor::exportAsset(const std::vector<entt::entity> &, const entt::registry &) {
        AT_WARN("GLTFAccessor::exportAsset not implemented");
        return {};
    }

    void GLTFAccessor::importAsset(const std::string &path, EntityBuffer &buffer) {

        // ---- Load file bytes ------------------------------------------------
        const auto sourcePath = resolveAssetPath(assets, path);
        std::ifstream file(sourcePath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            AT_ERROR("GLTFAccessor: failed to open file: {}", sourcePath.string());
            return;
        }

        const std::streamsize fileSize = file.tellg();
        if (fileSize < 0) {
            AT_ERROR("GLTFAccessor: failed to determine file size: {}", sourcePath.string());
            return;
        }

        file.seekg(0, std::ios::beg);
        std::vector<uint8_t> fileBytes(static_cast<size_t>(fileSize));
        if (fileSize > 0 && !file.read(reinterpret_cast<char *>(fileBytes.data()), fileSize)) {
            AT_ERROR("GLTFAccessor: failed to read file: {}", sourcePath.string());
            return;
        }

        if (fileBytes.empty()) {
            AT_ERROR("GLTFAccessor: empty glTF file: {}", sourcePath.string());
            return;
        }

        // ---- Parse glTF -----------------------------------------------------
        tinygltf::Model    model;
        tinygltf::TinyGLTF loader;
        std::string        err, warn;

        loader.RemoveImageLoader();
        loader.SetStoreOriginalJSONForExtrasAndExtensions(true);

        const std::string ext = toLower(sourcePath.extension().string());
        bool success = false;
        if (ext == ".glb") {
            success = loader.LoadBinaryFromMemory(
                &model, &err, &warn,
                reinterpret_cast<const unsigned char *>(fileBytes.data()),
                static_cast<uint32_t>(fileBytes.size()));
        } else {
            // For .gltf we need the resolved filesystem directory so tinygltf can resolve buffers/images.
            const std::string baseDir = sourcePath.parent_path().string();
            success = loader.LoadASCIIFromString(
                &model, &err, &warn,
                reinterpret_cast<const char *>(fileBytes.data()), static_cast<uint32_t>(fileBytes.size()), baseDir);
        }

        if (!warn.empty()) AT_WARN("glTF warning ({}): {}", path, warn);
        if (!success) {
            AT_ERROR("glTF parse error ({}): {}", path, err);
            return;
        }

        AT_INFO("GLTFAccessor: loaded {} — {} meshes, {} images",
                path, model.meshes.size(), model.images.size());

        // =========================================================================
        // STEP 1: Decode & store all images (parallel)
        // =========================================================================
        std::vector<AssetHandle<Texture>> imageHandles = decodeAndStoreTextures(model.images, path);

        // =========================================================================
        // STEP 2: Build mesh handles (parallel per mesh)
        // =========================================================================
        std::vector<std::vector<AssetHandle<Mesh>>> meshHandles(model.meshes.size());
        {
            std::mutex           meshMutex;
            std::vector<std::future<void>> meshFutures;
            meshFutures.reserve(model.meshes.size());

            for (size_t meshIdx = 0; meshIdx < model.meshes.size(); ++meshIdx) {
                meshFutures.push_back(executor.submit([&, meshIdx]() {
                    const tinygltf::Mesh &gltfMesh = model.meshes[meshIdx];
                    std::vector<AssetHandle<Mesh>> primHandles;

                    for (size_t primIdx = 0; primIdx < gltfMesh.primitives.size(); ++primIdx) {
                        const tinygltf::Primitive &prim = gltfMesh.primitives[primIdx];

                        if (prim.mode != TINYGLTF_MODE_TRIANGLES) {
                            AT_WARN("Skipping non-triangle primitive mesh[{}] prim[{}]", meshIdx, primIdx);
                            continue;
                        }

                        // ---- POSITION (required) --------------------------------
                        auto posIt = prim.attributes.find("POSITION");
                        if (posIt == prim.attributes.end()) {
                            AT_ERROR("Primitive missing POSITION — mesh[{}] prim[{}]", meshIdx, primIdx);
                            continue;
                        }
                        const tinygltf::Accessor   &posAcc  = model.accessors[posIt->second];
                        const tinygltf::BufferView &posView = model.bufferViews[posAcc.bufferView];
                        const tinygltf::Buffer     &posBuf  = model.buffers[posView.buffer];
                        const unsigned char *posBase  = posBuf.data.data() + posView.byteOffset + posAcc.byteOffset;
                        size_t               posStride = posView.byteStride ? posView.byteStride : (3 * sizeof(float));

                        // ---- NORMAL ---------------------------------------------
                        const unsigned char *normBase  = nullptr;
                        size_t               normStride = 0;
                        if (auto it = prim.attributes.find("NORMAL"); it != prim.attributes.end()) {
                            const auto &acc  = model.accessors[it->second];
                            const auto &view = model.bufferViews[acc.bufferView];
                            normBase   = model.buffers[view.buffer].data.data() + view.byteOffset + acc.byteOffset;
                            normStride = view.byteStride ? view.byteStride : (3 * sizeof(float));
                        }

                        // ---- TEXCOORD_0 -----------------------------------------
                        const unsigned char *texBase  = nullptr;
                        size_t               texStride = 0;
                        if (auto it = prim.attributes.find("TEXCOORD_0"); it != prim.attributes.end()) {
                            const auto &acc  = model.accessors[it->second];
                            const auto &view = model.bufferViews[acc.bufferView];
                            texBase   = model.buffers[view.buffer].data.data() + view.byteOffset + acc.byteOffset;
                            texStride = view.byteStride ? view.byteStride : (2 * sizeof(float));
                        }

                        // ---- COLOR_0 --------------------------------------------
                        const unsigned char *colorBase  = nullptr;
                        size_t               colorStride = 0;
                        if (auto it = prim.attributes.find("COLOR_0"); it != prim.attributes.end()) {
                            const auto &acc  = model.accessors[it->second];
                            const auto &view = model.bufferViews[acc.bufferView];
                            colorBase   = model.buffers[view.buffer].data.data() + view.byteOffset + acc.byteOffset;
                            colorStride = view.byteStride ? view.byteStride : (3 * sizeof(float));
                        }

                        // ---- TANGENT --------------------------------------------
                        const unsigned char *tangentBase  = nullptr;
                        size_t               tangentStride = 0;
                        if (auto it = prim.attributes.find("TANGENT"); it != prim.attributes.end()) {
                            const auto &acc  = model.accessors[it->second];
                            const auto &view = model.bufferViews[acc.bufferView];
                            tangentBase   = model.buffers[view.buffer].data.data() + view.byteOffset + acc.byteOffset;
                            tangentStride = view.byteStride ? view.byteStride : (4 * sizeof(float));
                        }

                        // ---- Build vertex array ---------------------------------
                        std::vector<Mesh::Vertex> vertices;
                        vertices.reserve(posAcc.count);
                        for (size_t v = 0; v < posAcc.count; ++v) {
                            Mesh::Vertex vert{};

                            // Position — 180° rotation around Z (coordinate system conversion)
                            auto pv = reinterpret_cast<const float *>(posBase + v * posStride);
                            vert.position = glm::vec3(-pv[0], -pv[1], pv[2]);

                            if (normBase) {
                                auto nv = reinterpret_cast<const float *>(normBase + v * normStride);
                                vert.normal = glm::vec3(-nv[0], -nv[1], nv[2]);
                                if (glm::dot(vert.normal, vert.normal) > 0.0f) {
                                    vert.normal = glm::normalize(vert.normal);
                                }
                            }

                            if (texBase) {
                                auto tv = reinterpret_cast<const float *>(texBase + v * texStride);
                                vert.uv = glm::vec2(tv[0], 1.0f - tv[1]); // flip V for Vulkan
                            }

                            if (colorBase) {
                                auto cv = reinterpret_cast<const float *>(colorBase + v * colorStride);
                                vert.color = glm::vec3(cv[0], cv[1], cv[2]);
                            } else {
                                vert.color = glm::vec3(1.0f);
                            }

                            if (tangentBase) {
                                auto tv = reinterpret_cast<const float *>(tangentBase + v * tangentStride);
                                glm::vec3 tangent{-tv[0], -tv[1], tv[2]};
                                if (glm::dot(tangent, tangent) > 0.0f) {
                                    tangent = glm::normalize(tangent);
                                }
                                vert.tangent = glm::vec4(tangent, -tv[3]);
                            } else {
                                vert.tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
                            }

                            vertices.push_back(vert);
                        }

                        // ---- Build index array ----------------------------------
                        std::vector<uint32_t> indices;
                        if (prim.indices >= 0) {
                            const auto &idxAcc  = model.accessors[prim.indices];
                            const auto &idxView = model.bufferViews[idxAcc.bufferView];
                            const unsigned char *base = model.buffers[idxView.buffer].data.data()
                                                        + idxView.byteOffset + idxAcc.byteOffset;
                            size_t idxStride = idxView.byteStride
                                               ? idxView.byteStride
                                               : tinygltf::GetComponentSizeInBytes(idxAcc.componentType);

                            indices.reserve(idxAcc.count);
                            for (size_t i = 0; i < idxAcc.count; ++i) {
                                uint32_t idx = 0;
                                switch (idxAcc.componentType) {
                                    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                                        idx = *reinterpret_cast<const uint8_t *>(base + i * idxStride); break;
                                    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                                        idx = *reinterpret_cast<const uint16_t *>(base + i * idxStride); break;
                                    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
                                        idx = *reinterpret_cast<const uint32_t *>(base + i * idxStride); break;
                                    default:
                                        AT_ERROR("Unsupported index component type"); break;
                                }
                                indices.push_back(idx);
                            }
                        } else {
                            // Non-indexed — generate sequential indices
                            indices.resize(vertices.size());
                            std::iota(indices.begin(), indices.end(), 0u);
                        }

                        // ---- Store via AssetManager -----------------------------
                        const std::string meshPath = path + "#mesh" + std::to_string(meshIdx)
                                                     + "_prim" + std::to_string(primIdx);
                        auto meshAsset  = std::make_shared<Mesh>(std::move(vertices), std::move(indices));
                        auto meshHandle = assets.store(std::move(meshAsset), meshPath);

                        primHandles.push_back(meshHandle);
                    }

                    std::lock_guard lock(meshMutex);
                    meshHandles[meshIdx] = std::move(primHandles);
                }));
            }

            for (auto &f : meshFutures) f.get();
        }

        AT_INFO("GLTFAccessor: built {} mesh groups", meshHandles.size());

        // =========================================================================
        // STEP 3: Walk scene graph, create entities
        // =========================================================================
        bool skyboxAdded = false;
        bool postProcessingAdded = false;
        handleSkybox(buffer, model, skyboxAdded);
        handlePostProcessing(buffer, model, postProcessingAdded);

        const int sceneIdx = model.defaultScene >= 0 ? model.defaultScene : 0;
        if (sceneIdx < static_cast<int>(model.scenes.size())) {
            for (int nodeIdx : model.scenes[sceneIdx].nodes) {
                processNode(buffer, model, nodeIdx, glm::mat4(1.0f),
                            meshHandles, imageHandles, path);
            }
        }

        AT_INFO("GLTFAccessor: staged entities from {}", path);
    }

    // =========================================================================
    // decodeAndStoreTextures
    // =========================================================================

    std::vector<AssetHandle<Texture>> GLTFAccessor::decodeAndStoreTextures(
        const std::vector<tinygltf::Image> &images,
        const std::string &path) {
        std::vector<AssetHandle<Texture>> imageHandles(images.size());
        std::vector<std::future<std::pair<size_t, AssetHandle<Texture>>>> textureFutures;
        textureFutures.reserve(images.size());

        for (size_t imgIdx = 0; imgIdx < images.size(); ++imgIdx) {
            textureFutures.push_back(executor.submit([this, &images, imgIdx, basePath = path]() {
                const tinygltf::Image &image = images[imgIdx];

                if (image.image.empty()) {
                    AT_WARN("Image[{}] has no data — skipping", imgIdx);
                    return std::pair<size_t, AssetHandle<Texture>>{imgIdx, AssetHandle<Texture>::invalid()};
                }

                // Determine if this texture is linear data (normal / metallic-roughness / AO)
                // by checking common name conventions used by DCC tools.
                const std::string name = toLower(image.name);
                const bool isNormalMap =
                    name.find("normal") != std::string::npos ||
                    name.find("nrm") != std::string::npos ||
                    name.find("norm") != std::string::npos ||
                    name.ends_with("_n") ||
                    name.ends_with("-n") ||
                    name.find("_n.") != std::string::npos ||
                    name.find("_n_") != std::string::npos;
                const bool isLinear =
                    isNormalMap ||
                    name.find("roughness") != std::string::npos ||
                    name.find("metallic")  != std::string::npos ||
                    name.find("metallicroughness") != std::string::npos ||
                    name.find("_mr")       != std::string::npos ||
                    name.find("occlusion") != std::string::npos ||
                    name.find("_ao")       != std::string::npos ||
                    name.find("ambientocclusion") != std::string::npos ||
                    name.find("_arm")      != std::string::npos ||
                    name.find("_orm")      != std::string::npos;

                const VkFormat format = isLinear
                                        ? VK_FORMAT_R8G8B8A8_UNORM
                                        : VK_FORMAT_R8G8B8A8_SRGB;

                int    width = 0, height = 0;
                std::vector<std::byte> pixelBytes;

                if (image.width > 0 && image.height > 0 && image.component > 0) {
                    // tinygltf already decoded it (buffer-view embedded image)
                    width  = image.width;
                    height = image.height;

                    if (image.bits != 8) {
                        AT_WARN("Image[{}] has unsupported bit depth {} — skipping", imgIdx, image.bits);
                        return std::pair<size_t, AssetHandle<Texture>>{imgIdx, AssetHandle<Texture>::invalid()};
                    }

                    const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
                    const size_t sourceChannels = static_cast<size_t>(image.component);
                    if (image.image.size() < pixelCount * sourceChannels) {
                        AT_WARN("Image[{}] data is smaller than expected — skipping", imgIdx);
                        return std::pair<size_t, AssetHandle<Texture>>{imgIdx, AssetHandle<Texture>::invalid()};
                    }

                    pixelBytes.resize(pixelCount * 4);
                    auto *dst = reinterpret_cast<unsigned char *>(pixelBytes.data());

                    for (size_t i = 0; i < pixelCount; ++i) {
                        const size_t src = i * sourceChannels;
                        if (sourceChannels == 1) {
                            dst[i * 4 + 0] = image.image[src];
                            dst[i * 4 + 1] = image.image[src];
                            dst[i * 4 + 2] = image.image[src];
                            dst[i * 4 + 3] = 255;
                        } else if (sourceChannels == 2) {
                            dst[i * 4 + 0] = image.image[src + 0];
                            dst[i * 4 + 1] = image.image[src + 0];
                            dst[i * 4 + 2] = image.image[src + 0];
                            dst[i * 4 + 3] = image.image[src + 1];
                        } else {
                            dst[i * 4 + 0] = image.image[src + 0];
                            dst[i * 4 + 1] = image.image[src + 1];
                            dst[i * 4 + 2] = image.image[src + 2];
                            dst[i * 4 + 3] = sourceChannels > 3 ? image.image[src + 3] : 255;
                        }
                    }
                } else {
                    // Compressed (PNG/JPEG) — decode with stb
                    int channels = 0;
                    unsigned char *pixels = stbi_load_from_memory(
                        image.image.data(), static_cast<int>(image.image.size()),
                        &width, &height, &channels, STBI_rgb_alpha);
                    if (!pixels) {
                        AT_ERROR("Image[{}] stb decode failed: {}", imgIdx, stbi_failure_reason());
                        return std::pair<size_t, AssetHandle<Texture>>{imgIdx, AssetHandle<Texture>::invalid()};
                    }

                    const size_t byteCount = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
                    pixelBytes.resize(byteCount);
                    std::memcpy(pixelBytes.data(), pixels, byteCount);
                    stbi_image_free(pixels);
                }

                const std::string texPath = image.name.empty()
                    ? basePath + "#image" + std::to_string(imgIdx)
                    : basePath + "#" + image.name;

                auto handle = assets.store<Texture>(
                    std::make_shared<Texture>(
                        pixelBytes,
                        static_cast<uint32_t>(width),
                        static_cast<uint32_t>(height),
                        format,
                        VK_SAMPLER_ADDRESS_MODE_REPEAT),
                    texPath);

                AT_TRACE("Image[{}] decoded and stored: {} ({}x{}, {})",
                         imgIdx, texPath, width, height,
                         isNormalMap ? "normal" : (isLinear ? "linear" : "sRGB"));

                return std::pair<size_t, AssetHandle<Texture>>{imgIdx, handle};
            }));
        }

        for (auto &future: textureFutures) {
            auto [imgIdx, handle] = future.get();
            imageHandles[imgIdx] = handle;
        }

        return imageHandles;
    }

    // =========================================================================
    // processNode
    // =========================================================================

    void GLTFAccessor::processNode(
        EntityBuffer                                    &buffer,
        const tinygltf::Model                           &model,
        int32_t                                          nodeIdx,
        const glm::mat4                                 &parentTransform,
        const std::vector<std::vector<AssetHandle<Mesh>>> &meshHandles,
        const std::vector<AssetHandle<Texture>>          &imageHandles,
        const std::string                                &sourcePath) {

        const tinygltf::Node &node = model.nodes[nodeIdx];

        const glm::mat4 localTransform = getNodeTransform(node);
        const glm::mat4 worldTransform = parentTransform * localTransform;

        glm::vec3 translation, scale, skew;
        glm::quat rotation;
        glm::vec4 perspective;
        glm::decompose(worldTransform, scale, rotation, translation, skew, perspective);

        // ---- Mesh primitives ------------------------------------------------
        if (node.mesh >= 0 && node.mesh < static_cast<int>(meshHandles.size())) {
            const tinygltf::Mesh &gltfMesh = model.meshes[node.mesh];
            const auto &primHandles = meshHandles[node.mesh];

            for (size_t primIdx = 0; primIdx < gltfMesh.primitives.size(); ++primIdx) {
                if (primIdx >= primHandles.size()) break;
                if (!primHandles[primIdx].valid()) continue;

                SceneNodeComponent sceneNode{};
                sceneNode.name = (gltfMesh.name.empty() ? ("Node_" + std::to_string(nodeIdx)) : gltfMesh.name)
                                 + (gltfMesh.primitives.size() > 1 ? "_prim" + std::to_string(primIdx) : "");
                sceneNode.parent = entt::null;
                buffer.add(sceneNode);

                TransformComponent transform{};
                transform.translation = translation;
                transform.rotation    = glm::eulerAngles(rotation);
                transform.scale       = scale;
                buffer.add(transform);

                // Model component
                ModelComponent modelComp{};
                modelComp.meshHandle = primHandles[primIdx];
                buffer.add(modelComp);

                // Material component
                MaterialComponent material{};
                auto materialAsset = std::make_shared<Material>();
                materialAsset->baseColor = glm::vec4(1.0f);

                const tinygltf::Primitive &prim = gltfMesh.primitives[primIdx];
                if (prim.material >= 0 && prim.material < static_cast<int>(model.materials.size())) {
                    const tinygltf::Material &mat = model.materials[prim.material];
                    const auto &pbr = mat.pbrMetallicRoughness;
                    materialAsset->name = mat.name.empty()
                        ? "Material_" + std::to_string(prim.material)
                        : mat.name;

                    if (mat.extensions.contains("KHR_materials_sheen") ||
                        mat.extensions.contains("KHR_material_sheen")) {
                        materialAsset->shadingModel = ShadingModel::CLOTH_CHARLIE;
                    }

                    // Base color factor
                    if (pbr.baseColorFactor.size() == 4) {
                        materialAsset->baseColor = glm::vec4(
                            pbr.baseColorFactor[0], pbr.baseColorFactor[1],
                            pbr.baseColorFactor[2], pbr.baseColorFactor[3]);
                    }

                    materialAsset->baseColorTexture = resolveTexture(model, pbr.baseColorTexture.index, imageHandles);
                    materialAsset->normalTexture = resolveTexture(model, mat.normalTexture.index, imageHandles);
                    materialAsset->metallicRoughnessTexture = resolveTexture(model, pbr.metallicRoughnessTexture.index, imageHandles);
                    materialAsset->occlusionTexture = resolveTexture(model, mat.occlusionTexture.index, imageHandles);
                    materialAsset->metallic = static_cast<float>(pbr.metallicFactor);
                    materialAsset->roughness = static_cast<float>(pbr.roughnessFactor);
                    materialAsset->alphaCutoff = static_cast<float>(mat.alphaCutoff);
                    if (mat.alphaMode == "MASK") {
                        materialAsset->alphaMode = AlphaMode::MASK;
                    } else if (mat.alphaMode == "BLEND") {
                        materialAsset->alphaMode = AlphaMode::BLEND;
                    }
                } else {
                    materialAsset->name = "Material";
                }
                const std::string materialPath = sourcePath + "#material/" +
                                                 (prim.material >= 0
                                                      ? std::to_string(prim.material)
                                                      : "default");
                material.materialHandle = assets.store<Material>(std::move(materialAsset), materialPath);
                buffer.add(material);

                buffer.next();
            }
        }

        // ---- KHR_lights_punctual --------------------------------------------
        if (node.light >= 0 && node.light < static_cast<int>(model.lights.size())) {
            const tinygltf::Light &gltfLight = model.lights[node.light];

            SceneNodeComponent sceneNode{};
            sceneNode.name = gltfLight.name.empty() ? "Light" : gltfLight.name;
            sceneNode.parent = entt::null;
            buffer.add(sceneNode);

            TransformComponent transform{};
            transform.translation = translation;
            transform.rotation    = glm::eulerAngles(rotation);
            transform.scale       = scale;
            buffer.add(transform);

            LightComponent light{};

            if      (gltfLight.type == "point")       light.type = LightType::POINT;
            else if (gltfLight.type == "spot")         { light.type = LightType::SPOT;
                                                         light.innerConeAngle = static_cast<float>(gltfLight.spot.innerConeAngle);
                                                         light.outerConeAngle = static_cast<float>(gltfLight.spot.outerConeAngle); }
            else if (gltfLight.type == "directional")  light.type = LightType::DIRECTIONAL;

            light.color = gltfLight.color.size() == 3
                ? glm::vec3(gltfLight.color[0], gltfLight.color[1], gltfLight.color[2])
                : glm::vec3(1.0f);

            light.intensity = static_cast<float>(gltfLight.intensity);
            light.range     = static_cast<float>(gltfLight.range);

            constexpr glm::vec3 defaultDir{0.0f, 0.0f, -1.0f};
            light.direction = toEngineDirection(rotation * defaultDir);
            buffer.add(light);
            buffer.next();
        }

        // ---- ATLAS_lights_special -------------------------------------------
        if (auto nodeAtlasIt = node.extensions.find("ATLAS_lights_special");
            nodeAtlasIt != node.extensions.end() && nodeAtlasIt->second.IsObject()) {

            const tinygltf::Value &nodeAtlas = nodeAtlasIt->second;
            if (nodeAtlas.Has("light") && nodeAtlas.Get("light").IsInt()) {
                const int lightIndex = nodeAtlas.Get("light").Get<int>();

                if (auto modelAtlasIt = model.extensions.find("ATLAS_lights_special");
                    modelAtlasIt != model.extensions.end() && modelAtlasIt->second.IsObject()) {

                    const tinygltf::Value &modelAtlas = modelAtlasIt->second;
                    if (modelAtlas.Has("lights") && modelAtlas.Get("lights").IsArray()) {
                        const auto &arr = modelAtlas.Get("lights").Get<tinygltf::Value::Array>();

                        if (lightIndex >= 0 && lightIndex < static_cast<int>(arr.size())
                            && arr[lightIndex].IsObject()) {
                            const tinygltf::Value &lobj = arr[lightIndex];

                            SceneNodeComponent sceneNode{};
                            sceneNode.name = lobj.Has("name") && lobj.Get("name").IsString()
                                ? lobj.Get("name").Get<std::string>()
                                : ("AtlasLight_" + std::to_string(lightIndex));
                            sceneNode.parent = entt::null;
                            buffer.add(sceneNode);

                            TransformComponent transform{};
                            transform.translation = translation;
                            transform.rotation    = glm::eulerAngles(rotation);
                            transform.scale       = scale;
                            buffer.add(transform);

                            LightComponent light{};
                            light.type  = LightType::RECT;

                            glm::vec3 localDirection{0.0f, 0.0f, -1.0f};
                            if (lobj.Has("direction") && lobj.Get("direction").IsArray()) {
                                const auto &d = lobj.Get("direction").Get<tinygltf::Value::Array>();
                                if (d.size() >= 3 && tinyValueNumber(d[0]) && tinyValueNumber(d[1]) && tinyValueNumber(d[2])) {
                                    localDirection = glm::vec3(tinyValueFloat(d[0]), tinyValueFloat(d[1]), tinyValueFloat(d[2]));
                                }
                            }
                            light.direction = toEngineDirection(rotation * localDirection);
                            light.rectRight = toEngineDirection(rotation * glm::vec3(1.0f, 0.0f, 0.0f));
                            light.rectUp = toEngineDirection(rotation * glm::vec3(0.0f, 1.0f, 0.0f));

                            light.color = glm::vec3(1.0f);
                            if (lobj.Has("color") && lobj.Get("color").IsArray()) {
                                const auto &c = lobj.Get("color").Get<tinygltf::Value::Array>();
                                if (c.size() >= 3 && tinyValueNumber(c[0]) && tinyValueNumber(c[1]) && tinyValueNumber(c[2])) {
                                    light.color = glm::vec3(tinyValueFloat(c[0]), tinyValueFloat(c[1]), tinyValueFloat(c[2]));
                                }
                            }
                            if (lobj.Has("intensity") && tinyValueNumber(lobj.Get("intensity")))
                                light.intensity = tinyValueFloat(lobj.Get("intensity"));
                            if (lobj.Has("width") && tinyValueNumber(lobj.Get("width")))
                                light.width = tinyValueFloat(lobj.Get("width"));
                            if (lobj.Has("height") && tinyValueNumber(lobj.Get("height")))
                                light.height = tinyValueFloat(lobj.Get("height"));
                            buffer.add(light);
                            buffer.next();
                        }
                    }
                }
            }
        }

        // ---- Recurse into children ------------------------------------------
        for (int childIdx : node.children) {
            processNode(buffer, model, childIdx, worldTransform,
                        meshHandles, imageHandles, sourcePath);
        }
    }

    // =========================================================================
    // handleSkybox
    // =========================================================================

    void GLTFAccessor::handleSkybox(
        EntityBuffer &buffer, const tinygltf::Model &model, bool &skyboxAdded) {
        if (skyboxAdded) return;

        auto it = model.extensions.find("ATLAS_skybox");
        if (it == model.extensions.end() || !it->second.IsObject()) return;

        const tinygltf::Value &skyboxExt = it->second;

        auto loadCubemap = [&](const char *key) -> AssetHandle<Cubemap> {
            if (!skyboxExt.Has(key)) return AssetHandle<Cubemap>::invalid();
            const auto &val = skyboxExt.Get(key);
            if (!val.IsString()) return AssetHandle<Cubemap>::invalid();
            return assets.store<Cubemap>(val.Get<std::string>());
        };

        SceneNodeComponent node{};
        node.name = "Skybox";
        node.parent = entt::null;
        buffer.add(node);

        SkyboxComponent skybox{};
        skybox.skyboxHandle = loadCubemap("skybox");
        skybox.irradianceHandle = loadCubemap("irradiance");
        skybox.prefilterHandle = loadCubemap("prefilter");
        buffer.add(skybox);
        buffer.next();

        skyboxAdded = true;
        AT_TRACE("Loaded skybox from ATLAS_skybox extension");
    }

    // =========================================================================
    // handlePostProcessing
    // =========================================================================

    void GLTFAccessor::handlePostProcessing(
        EntityBuffer &buffer, const tinygltf::Model &model, bool &postProcessingAdded) {
        if (postProcessingAdded) return;

        auto it = model.extensions.find("ATLAS_post_processing");
        if (it == model.extensions.end() || !it->second.IsObject()) return;

        const tinygltf::Value &ext = it->second;

        SceneNodeComponent node{};
        node.name = "PostProcessing";
        node.parent = entt::null;
        buffer.add(node);

        PostProcessingVolumeComponent volume{};
        if (ext.Has("exposure")) volume.exposure = tinyValueFloat(ext.Get("exposure"), volume.exposure);
        if (ext.Has("contrast")) volume.contrast = tinyValueFloat(ext.Get("contrast"), volume.contrast);
        if (ext.Has("saturation")) volume.saturation = tinyValueFloat(ext.Get("saturation"), volume.saturation);

        if (ext.Has("colorTint") && ext.Get("colorTint").IsArray()) {
            const auto &arr = ext.Get("colorTint").Get<tinygltf::Value::Array>();
            if (arr.size() >= 3 && tinyValueNumber(arr[0]) && tinyValueNumber(arr[1]) && tinyValueNumber(arr[2])) {
                volume.colorTint = glm::vec3(tinyValueFloat(arr[0]), tinyValueFloat(arr[1]), tinyValueFloat(arr[2]));
            }
        }

        buffer.add(volume);
        buffer.next();

        postProcessingAdded = true;
        AT_TRACE("Loaded post processing: exposure={} contrast={} saturation={}", volume.exposure, volume.contrast, volume.saturation);
    }

    // =========================================================================
    // resolveTexture
    // =========================================================================

    AssetHandle<Texture> GLTFAccessor::resolveTexture(
        const tinygltf::Model             &model,
        int                                texIdx,
        const std::vector<AssetHandle<Texture>> &imageHandles) {

        if (texIdx < 0 || texIdx >= static_cast<int>(model.textures.size()))
            return {};

        const int imgIdx = model.textures[texIdx].source;
        if (imgIdx < 0 || imgIdx >= static_cast<int>(imageHandles.size()))
            return {};

        return imageHandles[imgIdx];
    }

    // =========================================================================
    // getNodeTransform
    // =========================================================================

    glm::mat4 GLTFAccessor::getNodeTransform(const tinygltf::Node &node) {
        glm::mat4 mat(1.0f);

        if (node.matrix.size() == 16) {
            mat = glm::make_mat4x4(node.matrix.data());
        } else {
            if (node.translation.size() == 3) {
                // 180° rotation around Z for coordinate system conversion
                glm::vec3 t(
                    -static_cast<float>(node.translation[0]),
                    -static_cast<float>(node.translation[1]),
                     static_cast<float>(node.translation[2]));
                mat = glm::translate(mat, t);
            }
            if (node.rotation.size() == 4) {
                glm::quat q(
                    static_cast<float>(node.rotation[3]), // w
                    static_cast<float>(node.rotation[0]), // x
                    static_cast<float>(node.rotation[1]), // y
                    static_cast<float>(node.rotation[2]));// z
                mat *= glm::mat4_cast(q);
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

} // namespace Atlas
