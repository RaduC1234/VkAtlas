#include "GLTFAccessor.hpp"

#define GLM_ENABLE_EXPERIMENTAL
#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <numeric>
#include <fstream>
#include <limits>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include "stb_image.h"
#include "core/Log.hpp"
#include "entity/Object.hpp"

namespace Atlas {
    namespace {
        constexpr float DIRECTION_EPSILON = 1.0e-6f;

        void addExtensionUsed(tinygltf::Model &model, const std::string &extension) {
            if (std::ranges::find(model.extensionsUsed, extension) == model.extensionsUsed.end()) {
                model.extensionsUsed.push_back(extension);
            }
        }

        std::vector<std::byte> toBytes(const std::string &text) {
            std::vector<std::byte> bytes;
            bytes.reserve(text.size());
            for (const char ch : text) {
                bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));
            }
            return bytes;
        }

        std::string entityName(entt::entity entity, const entt::registry &registry, const std::string &fallbackPrefix) {
            if (const auto *node = registry.try_get<SceneNodeComponent>(entity); node && !node->name.empty()) {
                return node->name;
            }

            return fallbackPrefix + "_" + std::to_string(entt::to_integral(entity));
        }

        void appendEntityTree(
            entt::entity entity,
            const entt::registry &registry,
            std::vector<entt::entity> &outEntities,
            std::unordered_set<entt::id_type> &visited) {
            if (entity == entt::null || !registry.valid(entity)) {
                return;
            }

            const entt::id_type id = entt::to_integral(entity);
            if (!visited.insert(id).second) {
                return;
            }

            outEntities.push_back(entity);

            if (const auto *node = registry.try_get<SceneNodeComponent>(entity)) {
                for (const entt::entity child : node->children) {
                    appendEntityTree(child, registry, outEntities, visited);
                }
            }
        }

        void alignBuffer(tinygltf::Buffer &buffer) {
            while (buffer.data.size() % 4 != 0) {
                buffer.data.push_back(0);
            }
        }

        template<typename T>
        int appendAccessor(
            tinygltf::Model &model,
            tinygltf::Buffer &buffer,
            const std::vector<T> &data,
            const int target,
            const int componentType,
            const int type,
            const size_t count,
            const std::string &name,
            const std::vector<double> &minValues = {},
            const std::vector<double> &maxValues = {}) {
            if (data.empty() || count == 0) {
                return -1;
            }

            alignBuffer(buffer);

            tinygltf::BufferView view;
            view.name = name + "_view";
            view.buffer = 0;
            view.byteOffset = buffer.data.size();
            view.byteLength = data.size() * sizeof(T);
            view.target = target;

            const auto *bytes = reinterpret_cast<const unsigned char *>(data.data());
            buffer.data.insert(buffer.data.end(), bytes, bytes + view.byteLength);

            const int viewIndex = static_cast<int>(model.bufferViews.size());
            model.bufferViews.push_back(std::move(view));

            tinygltf::Accessor accessor;
            accessor.name = name;
            accessor.bufferView = viewIndex;
            accessor.byteOffset = 0;
            accessor.componentType = componentType;
            accessor.count = count;
            accessor.type = type;
            accessor.minValues = minValues;
            accessor.maxValues = maxValues;

            const int accessorIndex = static_cast<int>(model.accessors.size());
            model.accessors.push_back(std::move(accessor));
            return accessorIndex;
        }

        glm::vec3 safeNormalize(const glm::vec3 &value, const glm::vec3 &fallback) {
            return glm::dot(value, value) > DIRECTION_EPSILON ? glm::normalize(value) : fallback;
        }

        glm::vec3 toGltfAxis(const glm::vec3 &value) {
            return glm::vec3(-value.x, -value.y, value.z);
        }

        glm::vec2 toGltfUv(const glm::vec2 &value) {
            return glm::vec2(value.x, 1.0f - value.y);
        }

        std::vector<double> toNumberArray(const glm::vec3 &value) {
            return {
                static_cast<double>(value.x),
                static_cast<double>(value.y),
                static_cast<double>(value.z)
            };
        }

        tinygltf::Value toValueArray(const glm::vec3 &value) {
            tinygltf::Value::Array array;
            array.emplace_back(static_cast<double>(value.x));
            array.emplace_back(static_cast<double>(value.y));
            array.emplace_back(static_cast<double>(value.z));
            return tinygltf::Value(std::move(array));
        }

        tinygltf::Value toValueArray(const glm::vec4 &value) {
            tinygltf::Value::Array array;
            array.emplace_back(static_cast<double>(value.x));
            array.emplace_back(static_cast<double>(value.y));
            array.emplace_back(static_cast<double>(value.z));
            array.emplace_back(static_cast<double>(value.w));
            return tinygltf::Value(std::move(array));
        }

        std::vector<double> toGltfMatrix(const TransformComponent &transform) {
            const glm::mat4 matrix = transform.mat4();
            const float *values = glm::value_ptr(matrix);

            std::vector<double> result(16);
            for (size_t index = 0; index < result.size(); ++index) {
                result[index] = static_cast<double>(values[index]);
            }

            return result;
        }

        glm::quat rotationFromTransform(const TransformComponent *transform) {
            if (!transform) {
                return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            }

            glm::vec3 scale;
            glm::quat rotation;
            glm::vec3 translation;
            glm::vec3 skew;
            glm::vec4 perspective;
            glm::decompose(transform->mat4(), scale, rotation, translation, skew, perspective);
            return rotation;
        }

        std::string lowercaseExtension(const std::filesystem::path &path) {
            std::string ext = path.extension().string();
            std::ranges::transform(ext, ext.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            return ext;
        }

        std::string mimeTypeForImage(const std::filesystem::path &path) {
            const std::string ext = lowercaseExtension(path);
            if (ext == ".png") {
                return "image/png";
            }
            if (ext == ".jpg" || ext == ".jpeg") {
                return "image/jpeg";
            }
            if (ext == ".bmp") {
                return "image/bmp";
            }
            if (ext == ".gif") {
                return "image/gif";
            }
            return {};
        }

        bool readBinaryFile(const std::filesystem::path &path, std::vector<unsigned char> &outBytes) {
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            if (!file.is_open()) {
                return false;
            }

            const std::streamsize size = file.tellg();
            if (size <= 0) {
                return false;
            }

            outBytes.resize(static_cast<size_t>(size));
            file.seekg(0, std::ios::beg);
            return static_cast<bool>(file.read(reinterpret_cast<char *>(outBytes.data()), size));
        }

        int appendTexture(
            tinygltf::Model &model,
            std::unordered_map<AssetHandle, int> &textureCache,
            AssetManager &assets,
            const AssetHandle handle) {
            if (handle == INVALID_ASSET_HANDLE) {
                return -1;
            }

            if (const auto it = textureCache.find(handle); it != textureCache.end()) {
                return it->second;
            }

            const std::string virtualPath = assets.getPath(handle);
            if (virtualPath.empty() || virtualPath.find('#') != std::string::npos) {
                return -1;
            }

            const std::filesystem::path fullPath = assets.rootPath() / virtualPath;
            const std::string mimeType = mimeTypeForImage(fullPath);
            if (mimeType.empty()) {
                return -1;
            }

            std::vector<unsigned char> imageBytes;
            if (!readBinaryFile(fullPath, imageBytes)) {
                AT_WARN("GLTFAccessor: skipping texture export for missing image {}", virtualPath);
                return -1;
            }

            tinygltf::Image image;
            image.name = fullPath.stem().string();
            image.mimeType = mimeType;
            image.image = std::move(imageBytes);
            image.as_is = true;

            tinygltf::Texture texture;
            texture.name = image.name;
            texture.source = static_cast<int>(model.images.size());

            model.images.push_back(std::move(image));

            const int textureIndex = static_cast<int>(model.textures.size());
            model.textures.push_back(std::move(texture));
            textureCache.emplace(handle, textureIndex);
            return textureIndex;
        }

        int appendMaterial(
            tinygltf::Model &model,
            std::unordered_map<AssetHandle, int> &textureCache,
            AssetManager &assets,
            const MaterialComponent *material,
            const std::string &name) {
            if (!material) {
                return -1;
            }

            tinygltf::Material gltfMaterial;
            gltfMaterial.name = name;
            gltfMaterial.pbrMetallicRoughness.baseColorFactor = {
                static_cast<double>(material->baseColor.r),
                static_cast<double>(material->baseColor.g),
                static_cast<double>(material->baseColor.b),
                static_cast<double>(material->baseColor.a)
            };

            if (material->transparent || material->baseColor.a < 1.0f) {
                gltfMaterial.alphaMode = "BLEND";
            } else if (material->alphaMasked) {
                gltfMaterial.alphaMode = "MASK";
                gltfMaterial.alphaCutoff = 0.5;
            }

            const int albedoTexture = appendTexture(model, textureCache, assets, material->albedoTexture);
            if (albedoTexture >= 0) {
                gltfMaterial.pbrMetallicRoughness.baseColorTexture.index = albedoTexture;
            }

            const int normalTexture = appendTexture(model, textureCache, assets, material->normalMap);
            if (normalTexture >= 0) {
                gltfMaterial.normalTexture.index = normalTexture;
            }

            const int metallicRoughnessTexture = appendTexture(model, textureCache, assets, material->metallicRoughnessMap);
            if (metallicRoughnessTexture >= 0) {
                gltfMaterial.pbrMetallicRoughness.metallicRoughnessTexture.index = metallicRoughnessTexture;
            }

            const int occlusionTexture = appendTexture(model, textureCache, assets, material->ambientOcclusion);
            if (occlusionTexture >= 0) {
                gltfMaterial.occlusionTexture.index = occlusionTexture;
            }

            const int materialIndex = static_cast<int>(model.materials.size());
            model.materials.push_back(std::move(gltfMaterial));
            return materialIndex;
        }

        int appendMesh(
            tinygltf::Model &model,
            tinygltf::Buffer &buffer,
            const std::shared_ptr<Mesh> &mesh,
            const int materialIndex,
            const std::string &name) {
            const auto &vertices = mesh->getVertices();
            if (vertices.empty()) {
                return -1;
            }

            std::vector<float> positions;
            std::vector<float> normals;
            std::vector<float> texcoords;
            std::vector<float> colors;
            std::vector<float> tangents;
            positions.reserve(vertices.size() * 3);
            normals.reserve(vertices.size() * 3);
            texcoords.reserve(vertices.size() * 2);
            colors.reserve(vertices.size() * 3);
            tangents.reserve(vertices.size() * 4);

            glm::vec3 minPosition(std::numeric_limits<float>::max());
            glm::vec3 maxPosition(std::numeric_limits<float>::lowest());

            for (const auto &vertex : vertices) {
                const glm::vec3 position = toGltfAxis(vertex.position);
                minPosition = glm::min(minPosition, position);
                maxPosition = glm::max(maxPosition, position);
                positions.insert(positions.end(), {position.x, position.y, position.z});

                const glm::vec3 normal = toGltfAxis(safeNormalize(vertex.normal, glm::vec3(0.0f, 1.0f, 0.0f)));
                normals.insert(normals.end(), {normal.x, normal.y, normal.z});

                const glm::vec2 uv = toGltfUv(vertex.uv);
                texcoords.insert(texcoords.end(), {uv.x, uv.y});

                colors.insert(colors.end(), {vertex.color.r, vertex.color.g, vertex.color.b});

                const glm::vec3 tangentAxis = toGltfAxis(safeNormalize(glm::vec3(vertex.tangent), glm::vec3(1.0f, 0.0f, 0.0f)));
                tangents.insert(tangents.end(), {tangentAxis.x, tangentAxis.y, tangentAxis.z, -vertex.tangent.w});
            }

            std::vector<uint32_t> indices = mesh->getIndices();
            if (indices.empty()) {
                indices.reserve(vertices.size());
                for (uint32_t index = 0; index < static_cast<uint32_t>(vertices.size()); ++index) {
                    indices.push_back(index);
                }
            }

            tinygltf::Primitive primitive;
            primitive.mode = TINYGLTF_MODE_TRIANGLES;
            primitive.attributes["POSITION"] = appendAccessor(
                model,
                buffer,
                positions,
                TINYGLTF_TARGET_ARRAY_BUFFER,
                TINYGLTF_COMPONENT_TYPE_FLOAT,
                TINYGLTF_TYPE_VEC3,
                vertices.size(),
                name + "_positions",
                toNumberArray(minPosition),
                toNumberArray(maxPosition));
            primitive.attributes["NORMAL"] = appendAccessor(
                model,
                buffer,
                normals,
                TINYGLTF_TARGET_ARRAY_BUFFER,
                TINYGLTF_COMPONENT_TYPE_FLOAT,
                TINYGLTF_TYPE_VEC3,
                vertices.size(),
                name + "_normals");
            primitive.attributes["TEXCOORD_0"] = appendAccessor(
                model,
                buffer,
                texcoords,
                TINYGLTF_TARGET_ARRAY_BUFFER,
                TINYGLTF_COMPONENT_TYPE_FLOAT,
                TINYGLTF_TYPE_VEC2,
                vertices.size(),
                name + "_texcoords");
            primitive.attributes["COLOR_0"] = appendAccessor(
                model,
                buffer,
                colors,
                TINYGLTF_TARGET_ARRAY_BUFFER,
                TINYGLTF_COMPONENT_TYPE_FLOAT,
                TINYGLTF_TYPE_VEC3,
                vertices.size(),
                name + "_colors");
            primitive.attributes["TANGENT"] = appendAccessor(
                model,
                buffer,
                tangents,
                TINYGLTF_TARGET_ARRAY_BUFFER,
                TINYGLTF_COMPONENT_TYPE_FLOAT,
                TINYGLTF_TYPE_VEC4,
                vertices.size(),
                name + "_tangents");
            primitive.indices = appendAccessor(
                model,
                buffer,
                indices,
                TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER,
                TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT,
                TINYGLTF_TYPE_SCALAR,
                indices.size(),
                name + "_indices");

            if (materialIndex >= 0) {
                primitive.material = materialIndex;
            }

            tinygltf::Mesh gltfMesh;
            gltfMesh.name = name;
            gltfMesh.primitives.push_back(std::move(primitive));

            const int meshIndex = static_cast<int>(model.meshes.size());
            model.meshes.push_back(std::move(gltfMesh));
            return meshIndex;
        }

        std::string lightTypeName(const LightType type) {
            switch (type) {
                case LightType::POINT:
                    return "point";
                case LightType::SPOT:
                    return "spot";
                case LightType::DIRECTIONAL:
                    return "directional";
                default:
                    return {};
            }
        }

        int appendPunctualLight(tinygltf::Model &model, const LightComponent &light, const std::string &name) {
            const std::string type = lightTypeName(light.type);
            if (type.empty()) {
                return -1;
            }

            tinygltf::Light gltfLight;
            gltfLight.name = name;
            gltfLight.type = type;
            gltfLight.color = {
                static_cast<double>(light.color.r),
                static_cast<double>(light.color.g),
                static_cast<double>(light.color.b)
            };
            gltfLight.intensity = light.intensity;
            gltfLight.range = light.range;

            if (light.type == LightType::SPOT) {
                gltfLight.spot.innerConeAngle = light.innerConeAngle;
                gltfLight.spot.outerConeAngle = light.outerConeAngle;
            }

            const int lightIndex = static_cast<int>(model.lights.size());
            model.lights.push_back(std::move(gltfLight));
            return lightIndex;
        }

        int appendAtlasRectLight(
            tinygltf::Value::Array &atlasLights,
            const LightComponent &light,
            const TransformComponent *transform,
            const std::string &name) {
            const glm::quat rotation = rotationFromTransform(transform);
            const glm::vec3 gltfDirection = toGltfAxis(safeNormalize(light.direction, glm::vec3(0.0f, 0.0f, -1.0f)));
            const glm::vec3 localDirection = safeNormalize(glm::inverse(rotation) * gltfDirection, glm::vec3(0.0f, 0.0f, -1.0f));

            tinygltf::Value::Object object;
            object.emplace("name", tinygltf::Value(name));
            object.emplace("type", tinygltf::Value("rect"));
            object.emplace("color", toValueArray(light.color));
            object.emplace("intensity", tinygltf::Value(static_cast<double>(light.intensity)));
            object.emplace("width", tinygltf::Value(static_cast<double>(light.width)));
            object.emplace("height", tinygltf::Value(static_cast<double>(light.height)));
            object.emplace("direction", toValueArray(localDirection));

            const int index = static_cast<int>(atlasLights.size());
            atlasLights.emplace_back(tinygltf::Value(std::move(object)));
            return index;
        }

        void exportSkyboxExtension(tinygltf::Model &model, const std::vector<entt::entity> &entities, const entt::registry &registry, AssetManager &assets) {
            for (const entt::entity entity : entities) {
                const auto *skybox = registry.try_get<SkyboxComponent>(entity);
                if (!skybox) {
                    continue;
                }

                tinygltf::Value::Object object;
                auto appendPath = [&](const char *key, AssetHandle handle) {
                    if (handle == INVALID_ASSET_HANDLE) {
                        return;
                    }

                    const std::string path = assets.getPath(handle);
                    if (!path.empty()) {
                        object.emplace(key, tinygltf::Value(path));
                    }
                };

                appendPath("skybox", skybox->skyboxHandle);
                appendPath("irradiance", skybox->irradianceHandle);
                appendPath("prefilter", skybox->prefilterHandle);

                if (!object.empty()) {
                    model.extensions["ATLAS_skybox"] = tinygltf::Value(std::move(object));
                    addExtensionUsed(model, "ATLAS_skybox");
                }
                return;
            }
        }

        void exportPostProcessingExtension(tinygltf::Model &model, const std::vector<entt::entity> &entities, const entt::registry &registry) {
            for (const entt::entity entity : entities) {
                const auto *postProcessing = registry.try_get<PostProcessingVolumeComponent>(entity);
                if (!postProcessing) {
                    continue;
                }

                tinygltf::Value::Object object;
                object.emplace("exposure", tinygltf::Value(static_cast<double>(postProcessing->exposure)));
                object.emplace("contrast", tinygltf::Value(static_cast<double>(postProcessing->contrast)));
                object.emplace("saturation", tinygltf::Value(static_cast<double>(postProcessing->saturation)));
                object.emplace("colorTint", toValueArray(postProcessing->colorTint));

                model.extensions["ATLAS_post_processing"] = tinygltf::Value(std::move(object));
                addExtensionUsed(model, "ATLAS_post_processing");
                return;
            }
        }
    }

    GLTFAccessor::GLTFAccessor(AssetManager &assets, ExecutorService &service) : assets(assets), executor(service) {
    }

    std::vector<entt::entity> GLTFAccessor::importAsset(
        const std::string &path,
        entt::registry &registry,
        entt::entity parentEntity) {
        std::vector<AssetHandle> allAssets;

        std::filesystem::path fullPath = assets.rootPath() / path;

        std::ifstream file(fullPath, std::ios::binary | std::ios::ate);
        size_t fileSize = file.tellg();
        file.seekg(0);
        std::vector<uint8_t> fileBuffer(fileSize);
        file.read(reinterpret_cast<char *>(fileBuffer.data()), fileSize);
        file.close();

        tinygltf::Model model;
        tinygltf::TinyGLTF loader;
        std::string err, warn;

        loader.RemoveImageLoader();
        loader.SetStoreOriginalJSONForExtrasAndExtensions(true);

        bool success = false;
        if (fullPath.extension() == ".glb") {
            success = loader.LoadBinaryFromFile(&model, &err, &warn, fullPath.string());
        } else {
            success = loader.LoadASCIIFromFile(&model, &err, &warn, fullPath.string());
        }

        if (!success) {
            AT_ERROR("Failed to load glTF scene: {}", err);
            return {};
        }

        AT_INFO("Loading glTF file: {} ({} meshes, {} images)", path, model.meshes.size(), model.images.size());

        std::mutex handleMutex;
        std::vector<AssetHandle> imageHandles(model.images.size(), INVALID_ASSET_HANDLE);

        AT_INFO("Loading assets...");

        std::vector<std::future<void> > imageFutures;
        imageFutures.reserve(model.images.size());

        for (size_t imgIdx = 0; imgIdx < model.images.size(); ++imgIdx) {
            imageFutures.push_back(executor.submit([this, &model, imgIdx, &imageHandles, &handleMutex, &path]() {
                const tinygltf::Image &image = model.images[imgIdx];

                // Skip if no image data
                if (image.image.empty()) {
                    AT_WARN("Image {} has no data", imgIdx);
                    return;
                }

                int width = 0, height = 0;
                unsigned char *pixelData = nullptr;
                bool needsFreeing = false;

                // Check if tinygltf already decoded the image (bufferView-based images)
                if (image.width > 0 && image.height > 0 && image.component > 0) {
                    // Image is already decoded by tinygltf
                    width = image.width;
                    height = image.height;

                    // Convert to RGBA if needed
                    if (image.component == 4) {
                        // Already RGBA, use directly
                        pixelData = const_cast<unsigned char *>(image.image.data());
                        needsFreeing = false;
                    } else {
                        // Convert to RGBA
                        size_t pixelCount = width * height;
                        pixelData = new unsigned char[pixelCount * 4];
                        needsFreeing = true;

                        for (size_t i = 0; i < pixelCount; ++i) {
                            if (image.component == 1) {
                                // Grayscale
                                pixelData[i * 4 + 0] = image.image[i];
                                pixelData[i * 4 + 1] = image.image[i];
                                pixelData[i * 4 + 2] = image.image[i];
                                pixelData[i * 4 + 3] = 255;
                            } else if (image.component == 2) {
                                // Grayscale + Alpha
                                pixelData[i * 4 + 0] = image.image[i * 2 + 0];
                                pixelData[i * 4 + 1] = image.image[i * 2 + 0];
                                pixelData[i * 4 + 2] = image.image[i * 2 + 0];
                                pixelData[i * 4 + 3] = image.image[i * 2 + 1];
                            } else if (image.component == 3) {
                                // RGB
                                pixelData[i * 4 + 0] = image.image[i * 3 + 0];
                                pixelData[i * 4 + 1] = image.image[i * 3 + 1];
                                pixelData[i * 4 + 2] = image.image[i * 3 + 2];
                                pixelData[i * 4 + 3] = 255;
                            }
                        }
                    }
                } else {
                    // Image is compressed (PNG/JPEG), decode with stbi in parallel
                    int channels = 0;
                    pixelData = stbi_load_from_memory(
                        image.image.data(),
                        static_cast<int>(image.image.size()),
                        &width, &height, &channels,
                        STBI_rgb_alpha
                    );
                    needsFreeing = true; // stbi allocated memory

                    if (!pixelData) {
                        AT_ERROR("Failed to decode image {}: {}", imgIdx, stbi_failure_reason());
                        return;
                    }
                }

                // Determine correct format based on texture role inferred from name.
                // Albedo/emissive are perceptual (sRGB). Normal, metallic-roughness,
                // and occlusion are linear data (UNORM). Loading linear maps as SRGB
                // causes the GPU to gamma-correct them, making roughness/metallic values
                // wrong (sofa looks white, floor has no reflections, etc.).
                std::string imgName = image.name;
                std::transform(imgName.begin(), imgName.end(), imgName.begin(), ::tolower);

                const bool isNormalMap =
                        imgName.find("normal") != std::string::npos ||
                        imgName.find("nrm") != std::string::npos ||
                        imgName.find("norm") != std::string::npos ||
                        imgName.ends_with("_n") ||
                        imgName.ends_with("-n") ||
                        imgName.find("_n.") != std::string::npos ||
                        imgName.find("_n_") != std::string::npos;

                const bool isLinearData =
                        isNormalMap ||
                        imgName.find("metallic") != std::string::npos ||
                        imgName.find("roughness") != std::string::npos ||
                        imgName.find("metallicroughness") != std::string::npos ||
                        imgName.find("_mr") != std::string::npos ||
                        imgName.find("_orm") != std::string::npos ||
                        imgName.find("occlusion") != std::string::npos ||
                        imgName.find("_ao") != std::string::npos ||
                        imgName.find("ambientocclusion") != std::string::npos;

                VkFormat format = isLinearData ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R8G8B8A8_SRGB;

                std::string texturePath = path + "#image" + std::to_string(imgIdx);
                if (!image.name.empty()) {
                    texturePath = path + "#" + image.name;
                }

                // Thread-safe creation (upload the decoded pixels)
                {
                    std::lock_guard<std::mutex> lock(handleMutex);
                    AssetHandle handle = assets.getOrCreateTexture(pixelData, static_cast<uint32_t>(width), static_cast<uint32_t>(height), format, texturePath);
                    imageHandles[imgIdx] = handle;
                }

                // Free decoded pixel data if needed
                if (needsFreeing) {
                    if (image.width > 0) {
                        // We allocated with new[]
                        delete[] pixelData;
                    } else {
                        // stbi allocated
                        stbi_image_free(pixelData);
                    }
                }

                AT_TRACE("Loaded image[{}]: {} (handle: {}, {}x{}, {})",
                         imgIdx, texturePath, imageHandles[imgIdx], width, height,
                         isNormalMap ? "normal" : (isLinearData ? "linear" : "albedo"));
            }));
        }

        // Wait for all images to load
        for (auto &future: imageFutures) {
            future.get();
        }

        AT_INFO("Loaded {} images from glTF", imageHandles.size());

        // ========================================================================
        // STEP 2: Load all meshes in parallel
        // ========================================================================
        std::vector<std::future<void> > meshFutures;
        std::vector<std::vector<AssetHandle> > meshIndexToHandles(model.meshes.size());
        meshFutures.reserve(model.meshes.size());

        for (size_t meshIndex = 0; meshIndex < model.meshes.size(); ++meshIndex) {
            meshFutures.push_back(executor.submit([this, &model, meshIndex, &meshIndexToHandles, &imageHandles, &handleMutex, &path]() {
                const tinygltf::Mesh &mesh = model.meshes[meshIndex];
                std::vector<AssetHandle> primHandles;

                for (size_t primIndex = 0; primIndex < mesh.primitives.size(); ++primIndex) {
                    const tinygltf::Primitive &prim = mesh.primitives[primIndex];

                    // Only support triangles
                    if (prim.mode != TINYGLTF_MODE_TRIANGLES) {
                        AT_WARN("Skipping non-triangle primitive in mesh[{}]", meshIndex);
                        continue;
                    }

                    // Extract vertex data
                    std::vector<Mesh::Vertex> vertices;
                    std::vector<uint32_t> indices;

                    // Get position accessor
                    auto posIt = prim.attributes.find("POSITION");
                    if (posIt == prim.attributes.end()) {
                        AT_ERROR("Primitive missing POSITION attribute");
                        continue;
                    }

                    const tinygltf::Accessor &posAcc = model.accessors[posIt->second];
                    const tinygltf::BufferView &posView = model.bufferViews[posAcc.bufferView];
                    const tinygltf::Buffer &posBuffer = model.buffers[posView.buffer];
                    const unsigned char *posBase = posBuffer.data.data() + posView.byteOffset + posAcc.byteOffset;
                    size_t posStride = posView.byteStride ? posView.byteStride : (3 * sizeof(float));

                    // Get optional attributes
                    const unsigned char *normBase = nullptr;
                    size_t normStride = 0;
                    bool hasNormal = false;
                    auto normIt = prim.attributes.find("NORMAL");
                    if (normIt != prim.attributes.end()) {
                        const tinygltf::Accessor &normAcc = model.accessors[normIt->second];
                        const tinygltf::BufferView &normView = model.bufferViews[normAcc.bufferView];
                        const tinygltf::Buffer &normBuffer = model.buffers[normView.buffer];
                        normBase = normBuffer.data.data() + normView.byteOffset + normAcc.byteOffset;
                        normStride = normView.byteStride ? normView.byteStride : (3 * sizeof(float));
                        hasNormal = true;
                    }

                    const unsigned char *texBase = nullptr;
                    size_t texStride = 0;
                    bool hasTex = false;
                    auto texIt = prim.attributes.find("TEXCOORD_0");
                    if (texIt != prim.attributes.end()) {
                        const tinygltf::Accessor &texAcc = model.accessors[texIt->second];
                        const tinygltf::BufferView &texView = model.bufferViews[texAcc.bufferView];
                        const tinygltf::Buffer &texBuffer = model.buffers[texView.buffer];
                        texBase = texBuffer.data.data() + texView.byteOffset + texAcc.byteOffset;
                        texStride = texView.byteStride ? texView.byteStride : (2 * sizeof(float));
                        hasTex = true;
                    }

                    const unsigned char *colorBase = nullptr;
                    size_t colorStride = 0;
                    bool hasColor = false;
                    auto colorIt = prim.attributes.find("COLOR_0");
                    if (colorIt != prim.attributes.end()) {
                        const tinygltf::Accessor &colorAcc = model.accessors[colorIt->second];
                        const tinygltf::BufferView &colorView = model.bufferViews[colorAcc.bufferView];
                        const tinygltf::Buffer &colorBuffer = model.buffers[colorView.buffer];
                        colorBase = colorBuffer.data.data() + colorView.byteOffset + colorAcc.byteOffset;
                        colorStride = colorView.byteStride ? colorView.byteStride : (3 * sizeof(float));
                        hasColor = true;
                    }

                    const unsigned char *tangentBase = nullptr;
                    size_t tangentStride = 0;
                    bool hasTangent = false;
                    auto tangentIt = prim.attributes.find("TANGENT");
                    if (tangentIt != prim.attributes.end()) {
                        const tinygltf::Accessor &tangentAcc = model.accessors[tangentIt->second];
                        const tinygltf::BufferView &tangentView = model.bufferViews[tangentAcc.bufferView];
                        const tinygltf::Buffer &tangentBuffer = model.buffers[tangentView.buffer];
                        tangentBase = tangentBuffer.data.data() + tangentView.byteOffset + tangentAcc.byteOffset;
                        tangentStride = tangentView.byteStride ? tangentView.byteStride : (4 * sizeof(float));
                        hasTangent = true;
                    }

                    // Build vertices
                    vertices.reserve(posAcc.count);
                    for (size_t v = 0; v < posAcc.count; ++v) {
                        Mesh::Vertex vert{};

                        // Position - apply 180° rotation around Z for coordinate system conversion
                        auto pv = reinterpret_cast<const float *>(posBase + v * posStride);
                        vert.position = glm::vec3(-pv[0], -pv[1], pv[2]);

                        // Normal
                        if (hasNormal) {
                            auto nv = reinterpret_cast<const float *>(normBase + v * normStride);
                            vert.normal = glm::vec3(-nv[0], -nv[1], nv[2]);
                            if (glm::dot(vert.normal, vert.normal) > 0.0f) {
                                vert.normal = glm::normalize(vert.normal);
                            }
                        } else {
                            vert.normal = glm::vec3(0.0f);
                        }


                        if (hasTex) {
                            auto tv = reinterpret_cast<const float *>(texBase + v * texStride);
                            vert.uv = glm::vec2(tv[0], 1.0f - tv[1]); // UV - flip V coordinate for vulkan
                        } else {
                            vert.uv = glm::vec2(0.0f);
                        }

                        // Color
                        if (hasColor) {
                            auto cv = reinterpret_cast<const float *>(colorBase + v * colorStride);
                            vert.color = glm::vec3(cv[0], cv[1], cv[2]);
                        } else {
                            vert.color = glm::vec3(1.0f);
                        }

                        // Tangent
                        if (hasTangent) {
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

                    // Build indices
                    if (prim.indices >= 0) {
                        const tinygltf::Accessor &idxAcc = model.accessors[prim.indices];
                        const tinygltf::BufferView &idxView = model.bufferViews[idxAcc.bufferView];
                        const tinygltf::Buffer &idxBuffer = model.buffers[idxView.buffer];
                        const unsigned char *base = idxBuffer.data.data() + idxView.byteOffset + idxAcc.byteOffset;
                        size_t idxStride = idxView.byteStride ? idxView.byteStride : tinygltf::GetComponentSizeInBytes(idxAcc.componentType);

                        indices.reserve(idxAcc.count);
                        for (size_t i = 0; i < idxAcc.count; ++i) {
                            uint32_t index = 0;
                            switch (idxAcc.componentType) {
                                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
                                    auto p = reinterpret_cast<const uint16_t *>(base + i * idxStride);
                                    index = static_cast<uint32_t>(*p);
                                    break;
                                }
                                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: {
                                    auto p = reinterpret_cast<const uint32_t *>(base + i * idxStride);
                                    index = *p;
                                    break;
                                }
                                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: {
                                    auto p = reinterpret_cast<const uint8_t *>(base + i * idxStride);
                                    index = static_cast<uint32_t>(*p);
                                    break;
                                }
                                default:
                                    AT_ERROR("Unsupported index component type");
                                    break;
                            }
                            indices.push_back(index);
                        }
                    } else {
                        // Non-indexed: create sequential indices
                        indices.reserve(vertices.size());
                        for (uint32_t i = 0; i < static_cast<uint32_t>(vertices.size()); ++i) {
                            indices.push_back(i);
                        }
                    }

                    // Create mesh path
                    std::string meshPath = path + "#mesh" + std::to_string(meshIndex) + "_prim" + std::to_string(primIndex);

                    // Thread-safe mesh creation with hash-based deduplication
                    AssetHandle meshHandle; {
                        std::lock_guard<std::mutex> lock(handleMutex);
                        meshHandle = assets.getOrCreateMesh(vertices, indices, meshPath);
                    }

                    primHandles.push_back(meshHandle);
                }

                // Store handles for this mesh
                std::lock_guard<std::mutex> lock(handleMutex);
                meshIndexToHandles[meshIndex] = primHandles;
            }));
        }

        // Wait for all meshes to load
        for (auto &future: meshFutures) {
            future.get();
        }

        // ========================================================================
        // STEP 3: Collect all asset handles into one vector
        // ========================================================================

        // Add all texture handles
        for (auto handle: imageHandles) {
            if (handle != INVALID_ASSET_HANDLE) {
                allAssets.push_back(handle);
            }
        }

        // Add all mesh handles
        for (const auto &handles: meshIndexToHandles) {
            for (auto handle: handles) {
                if (handle != INVALID_ASSET_HANDLE) {
                    allAssets.push_back(handle);
                }
            }
        }

        AT_INFO("Successfully loaded glTF: {} ({} total assets: {} meshes, {} textures)", path, allAssets.size(), allAssets.size() - imageHandles.size(), imageHandles.size());

        std::vector<entt::entity> entities;

        // Scene-level extensions
        {
            auto skyIt = model.extensions.find("ATLAS_skybox");
            if (skyIt != model.extensions.end()) {
                auto e   = registry.create();
                auto &sn = registry.emplace<SceneNodeComponent>(e);
                sn.name  = "Skybox";
                sn.parent = entt::null;
                entities.push_back(e);
                handleSkybox(registry, e, model);
            }

            auto ppIt = model.extensions.find("ATLAS_post_processing");
            if (ppIt != model.extensions.end()) {
                auto e   = registry.create();
                auto &sn = registry.emplace<SceneNodeComponent>(e);
                sn.name  = "PostProcessing";
                sn.parent = entt::null;
                entities.push_back(e);
                handlePostProcessing(registry, e, model);
            }
        }

        // Process each scene (usually there's just one)
        if (!model.scenes.empty()) {
            const tinygltf::Scene &gltfScene = model.scenes[model.defaultScene >= 0 ? model.defaultScene : 0];

            // Process each root node
            for (int nodeIdx: gltfScene.nodes) {
                glm::mat4 parentTransform = glm::mat4(1.0f);
                processNode(registry, model, nodeIdx, parentTransform, path, entities);
            }
        }

        AT_INFO("Created {} entities from glTF scene: {}", entities.size(), path);

        return entities;
    }

    std::vector<std::byte> GLTFAccessor::exportAsset(
        const std::vector<entt::entity> &entities,
        const entt::registry &registry) {
        tinygltf::Model model;
        model.asset.version = "2.0";
        model.asset.generator = "Atlas";
        model.defaultScene = 0;

        tinygltf::Scene scene;
        scene.name = "AtlasScene";
        model.scenes.push_back(std::move(scene));

        model.buffers.emplace_back();
        tinygltf::Buffer &buffer = model.buffers.front();
        buffer.name = "AtlasBuffer";

        std::vector<entt::entity> exportEntities;
        std::unordered_set<entt::id_type> visited;
        for (const entt::entity entity : entities) {
            appendEntityTree(entity, registry, exportEntities, visited);
        }

        std::unordered_map<AssetHandle, int> textureCache;
        tinygltf::Value::Array atlasSpecialLights;

        for (const entt::entity entity : exportEntities) {
            const auto *modelComponent = registry.try_get<ModelComponent>(entity);
            const auto *lightComponent = registry.try_get<LightComponent>(entity);

            if (!modelComponent && !lightComponent) {
                continue;
            }

            const std::string name = entityName(entity, registry, "Node");
            const auto *transform = registry.try_get<TransformComponent>(entity);
            const auto *material = registry.try_get<MaterialComponent>(entity);

            tinygltf::Node node;
            node.name = name;

            if (transform) {
                node.matrix = toGltfMatrix(*transform);
            }

            if (modelComponent && modelComponent->meshHandle != INVALID_ASSET_HANDLE) {
                const auto mesh = assets.getMesh(modelComponent->meshHandle);
                if (mesh) {
                    const int materialIndex = appendMaterial(model, textureCache, assets, material, name + "_Material");
                    const int meshIndex = appendMesh(model, buffer, mesh, materialIndex, name + "_Mesh");
                    if (meshIndex >= 0) {
                        node.mesh = meshIndex;
                    }
                } else {
                    AT_WARN("GLTFAccessor: mesh handle {} not found while exporting {}", modelComponent->meshHandle, name);
                }
            }

            if (lightComponent) {
                if (lightComponent->type == LightType::RECT) {
                    const int atlasLightIndex = appendAtlasRectLight(atlasSpecialLights, *lightComponent, transform, name);

                    tinygltf::Value::Object nodeExtension;
                    nodeExtension.emplace("light", tinygltf::Value(atlasLightIndex));
                    node.extensions["ATLAS_lights_special"] = tinygltf::Value(std::move(nodeExtension));
                } else {
                    const int lightIndex = appendPunctualLight(model, *lightComponent, name);
                    if (lightIndex >= 0) {
                        node.light = lightIndex;
                    }
                }
            }

            if (node.mesh < 0 && node.light < 0 && node.extensions.empty()) {
                continue;
            }

            const int nodeIndex = static_cast<int>(model.nodes.size());
            model.nodes.push_back(std::move(node));
            model.scenes[model.defaultScene].nodes.push_back(nodeIndex);
        }

        if (!atlasSpecialLights.empty()) {
            tinygltf::Value::Object atlasExtension;
            atlasExtension.emplace("lights", tinygltf::Value(std::move(atlasSpecialLights)));
            model.extensions["ATLAS_lights_special"] = tinygltf::Value(std::move(atlasExtension));
            addExtensionUsed(model, "ATLAS_lights_special");
        }

        exportSkyboxExtension(model, exportEntities, registry, assets);
        exportPostProcessingExtension(model, exportEntities, registry);

        if (buffer.data.empty()) {
            model.buffers.clear();
        } else {
            alignBuffer(buffer);
        }

        std::ostringstream stream;
        tinygltf::TinyGLTF writer;
        if (!writer.WriteGltfSceneToStream(&model, stream, true, false)) {
            AT_ERROR("GLTFAccessor: failed to serialize glTF export");
            return {};
        }

        AT_INFO("GLTFAccessor: exported {} nodes, {} meshes, {} punctual lights, {} Atlas special lights",
                model.nodes.size(), model.meshes.size(), model.lights.size(), model.extensions.contains("ATLAS_lights_special") ? model.extensions["ATLAS_lights_special"].Get<tinygltf::Value::Object>().at("lights").ArrayLen() : 0);

        return toBytes(stream.str());
    }

    void GLTFAccessor::processNode(
        entt::registry &registry,
        const tinygltf::Model &model,
        int32_t nodeIdx,
        const glm::mat4 &parentTransform,
        const std::string &virtualPath,
        std::vector<entt::entity> &outEntities) {
        const tinygltf::Node &node = model.nodes[nodeIdx];

        glm::mat4 localTransform = getNodeTransform(node);
        glm::mat4 worldTransform = parentTransform * localTransform;

        // Decompose world transform once for this node
        glm::vec3 wTranslation, wScale, wSkew;
        glm::quat wRotation;
        glm::vec4 wPerspective;
        glm::decompose(worldTransform, wScale, wRotation, wTranslation, wSkew, wPerspective);

        const auto toEngineDirection = [](const glm::vec3 &direction) {
            return glm::normalize(glm::vec3(-direction.x, -direction.y, direction.z));
        };

        if (node.mesh >= 0 && node.mesh < static_cast<int>(model.meshes.size())) {
            const tinygltf::Mesh &mesh = model.meshes[node.mesh];

            for (size_t primIdx = 0; primIdx < mesh.primitives.size(); ++primIdx) {
                auto entity = registry.create();

                auto &sn  = registry.emplace<SceneNodeComponent>(entity);
                sn.name   = (mesh.name.empty() ? ("Node_" + std::to_string(nodeIdx)) : mesh.name)
                            + (mesh.primitives.size() > 1 ? "_prim" + std::to_string(primIdx) : "");
                sn.parent = entt::null;

                auto &transform = registry.emplace<TransformComponent>(entity);
                transform.translation = wTranslation;
                transform.rotation    = glm::eulerAngles(wRotation);
                transform.scale       = wScale;

                std::string meshPath = virtualPath + "#mesh" + std::to_string(node.mesh) + "_prim" + std::to_string(primIdx);
                AssetHandle meshHandle = assets.getHandle(meshPath);
                if (meshHandle != INVALID_ASSET_HANDLE) {
                    auto &modelComp = registry.emplace<ModelComponent>(entity);
                    modelComp.meshHandle = meshHandle;

                    auto &material = registry.emplace<MaterialComponent>(entity);

                    const tinygltf::Primitive &prim = mesh.primitives[primIdx];
                    if (prim.material >= 0 && prim.material < static_cast<int>(model.materials.size())) {
                        const tinygltf::Material &mat = model.materials[prim.material];
                        const auto &pbr = mat.pbrMetallicRoughness;

                        // Base color factor
                        if (pbr.baseColorFactor.size() == 4) {
                            material.baseColor = glm::vec4(
                                pbr.baseColorFactor[0],
                                pbr.baseColorFactor[1],
                                pbr.baseColorFactor[2],
                                pbr.baseColorFactor[3]
                            );
                        } else {
                            material.baseColor = glm::vec4(1.0f);
                        }

                        material.alphaMasked = mat.alphaMode == "MASK";
                        material.transparent = mat.alphaMode == "BLEND" ||
                                               material.alphaMasked ||
                                               material.baseColor.a < 1.0f;

                        // Albedo texture
                        if (pbr.baseColorTexture.index >= 0) {
                            int texIdx = pbr.baseColorTexture.index;
                            if (texIdx >= 0 && texIdx < static_cast<int>(model.textures.size())) {
                                int imgIdx = model.textures[texIdx].source;

                                if (imgIdx >= 0 && imgIdx < static_cast<int>(model.images.size())) {
                                    const tinygltf::Image &image = model.images[imgIdx];

                                    std::string imagePath;
                                    if (!image.name.empty()) {
                                        imagePath = virtualPath + "#" + image.name;
                                    } else {
                                        imagePath = virtualPath + "#image" + std::to_string(imgIdx);
                                    }

                                    AssetHandle texHandle = assets.getHandle(imagePath);
                                    if (texHandle != INVALID_ASSET_HANDLE) {
                                        material.albedoTexture = texHandle;
                                    }
                                }
                            }
                        }

                        // Normal map
                        if (mat.normalTexture.index >= 0) {
                            int texIdx = mat.normalTexture.index;
                            if (texIdx >= 0 && texIdx < static_cast<int>(model.textures.size())) {
                                int imgIdx = model.textures[texIdx].source;

                                if (imgIdx >= 0 && imgIdx < static_cast<int>(model.images.size())) {
                                    const tinygltf::Image &image = model.images[imgIdx];

                                    std::string imagePath;
                                    if (!image.name.empty()) {
                                        imagePath = virtualPath + "#" + image.name;
                                    } else {
                                        imagePath = virtualPath + "#image" + std::to_string(imgIdx);
                                    }

                                    AssetHandle normHandle = assets.getHandle(imagePath);
                                    if (normHandle != INVALID_ASSET_HANDLE) {
                                        material.normalMap = normHandle;
                                    }
                                }
                            }
                        }

                        // Metallic-Roughness
                        if (pbr.metallicRoughnessTexture.index >= 0) {
                            int texIdx = pbr.metallicRoughnessTexture.index;
                            if (texIdx < static_cast<int>(model.textures.size())) {
                                int imgIdx = model.textures[texIdx].source;
                                if (imgIdx >= 0 && imgIdx < static_cast<int>(model.images.size())) {
                                    const tinygltf::Image &image = model.images[imgIdx];
                                    std::string imagePath = !image.name.empty()
                                                                ? virtualPath + "#" + image.name
                                                                : virtualPath + "#image" + std::to_string(imgIdx);
                                    AssetHandle mrHandle = assets.getHandle(imagePath);
                                    if (mrHandle != INVALID_ASSET_HANDLE)
                                        material.metallicRoughnessMap = mrHandle;
                                }
                            }
                        }

                        // Occlusion
                        if (mat.occlusionTexture.index >= 0) {
                            int texIdx = mat.occlusionTexture.index;
                            if (texIdx < static_cast<int>(model.textures.size())) {
                                int imgIdx = model.textures[texIdx].source;
                                if (imgIdx >= 0 && imgIdx < static_cast<int>(model.images.size())) {
                                    const tinygltf::Image &image = model.images[imgIdx];
                                    std::string imagePath = !image.name.empty()
                                                                ? virtualPath + "#" + image.name
                                                                : virtualPath + "#image" + std::to_string(imgIdx);
                                    AssetHandle aoHandle = assets.getHandle(imagePath);
                                    if (aoHandle != INVALID_ASSET_HANDLE)
                                        material.ambientOcclusion = aoHandle;
                                }
                            }
                        }
                    }

                    outEntities.push_back(entity);
                }
            }
        }

        // KHR_lights_punctual
        if (node.light >= 0 && node.light < static_cast<int>(model.lights.size())) {
            const tinygltf::Light &gltfLight = model.lights[node.light];

            auto entity = registry.create();

            auto &sn  = registry.emplace<SceneNodeComponent>(entity);
            sn.name   = gltfLight.name.empty() ? "Light" : gltfLight.name;
            sn.parent = entt::null;

            auto &transform = registry.emplace<TransformComponent>(entity);
            transform.translation = wTranslation;
            transform.rotation    = glm::eulerAngles(wRotation);
            transform.scale       = wScale;

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
                    static_cast<float>(gltfLight.color[2])
                );
            } else {
                light.color = glm::vec3(1.0f);
            }

            light.intensity = static_cast<float>(gltfLight.intensity);
            light.range = static_cast<float>(gltfLight.range);

            // glTF punctual lights emit along local -Z. Convert that vector
            // through the same 180-degree Z turn used for imported geometry.
            constexpr glm::vec3 defaultDir = glm::vec3{0.0f, 0.0f, -1.0f};
            light.direction = toEngineDirection(wRotation * defaultDir);

            outEntities.push_back(entity);

            AT_TRACE("Created light entity: type={}, color=({}, {}, {}), intensity={}, range={}",
                     gltfLight.type, light.color.r, light.color.g, light.color.b,
                     light.intensity, light.range);
        }

        // ATLAS_lights_punctual
        {
            auto nodeAtlasIt = node.extensions.find("ATLAS_lights_special");
            if (nodeAtlasIt != node.extensions.end() && nodeAtlasIt->second.IsObject()) {
                const tinygltf::Value &nodeAtlas = nodeAtlasIt->second;

                if (nodeAtlas.Has("light") && nodeAtlas.Get("light").IsInt()) {
                    const int lightIndex = nodeAtlas.Get("light").Get<int>();

                    auto modelAtlasIt = model.extensions.find("ATLAS_lights_special");
                    if (modelAtlasIt != model.extensions.end() && modelAtlasIt->second.IsObject()) {
                        const tinygltf::Value &modelAtlas = modelAtlasIt->second;

                        if (modelAtlas.Has("lights") && modelAtlas.Get("lights").IsArray()) {
                            const auto &lightsArr = modelAtlas.Get("lights").Get<tinygltf::Value::Array>();

                            if (lightIndex >= 0 && lightIndex < static_cast<int>(lightsArr.size()) && lightsArr[lightIndex].IsObject()) {
                                const tinygltf::Value &lobj = lightsArr[lightIndex];

                                auto entity = registry.create();

                                auto &sn  = registry.emplace<SceneNodeComponent>(entity);
                                sn.name   = lobj.Has("name") && lobj.Get("name").IsString()
                                    ? lobj.Get("name").Get<std::string>()
                                    : ("AtlasLight_" + std::to_string(lightIndex));
                                sn.parent = entt::null;

                                auto &transform = registry.emplace<TransformComponent>(entity);
                                transform.translation = wTranslation;
                                transform.rotation    = glm::eulerAngles(wRotation);
                                transform.scale       = wScale;

                                auto &light = registry.emplace<LightComponent>(entity);

                                // type
                                std::string type = "rect";
                                if (lobj.Has("type") && lobj.Get("type").IsString()) {
                                    type = lobj.Get("type").Get<std::string>();
                                }
                                if (type == "rect") {
                                    light.type = LightType::RECT;
                                }

                                // The Unreal exporter inserts the light child node rotation used by
                                // glTF punctual lights, so the rect emitter plane is local X/Y here.
                                glm::vec3 localDirection{0.0f, 0.0f, -1.0f};
                                if (lobj.Has("direction") && lobj.Get("direction").IsArray()) {
                                    const auto &darr = lobj.Get("direction").Get<tinygltf::Value::Array>();
                                    if (darr.size() >= 3 && darr[0].IsNumber() && darr[1].IsNumber() && darr[2].IsNumber()) {
                                        localDirection = glm::vec3(
                                            static_cast<float>(darr[0].Get<double>()),
                                            static_cast<float>(darr[1].Get<double>()),
                                            static_cast<float>(darr[2].Get<double>())
                                        );
                                    }
                                }
                                light.direction = toEngineDirection(wRotation * localDirection);
                                light.rectRight = toEngineDirection(wRotation * glm::vec3(1.0f, 0.0f, 0.0f));
                                light.rectUp = toEngineDirection(wRotation * glm::vec3(0.0f, 1.0f, 0.0f));

                                // color
                                light.color = glm::vec3(1.0f);
                                if (lobj.Has("color") && lobj.Get("color").IsArray()) {
                                    const auto &carr = lobj.Get("color").Get<tinygltf::Value::Array>();
                                    if (carr.size() >= 3 && carr[0].IsNumber() && carr[1].IsNumber() && carr[2].IsNumber()) {
                                        light.color = glm::vec3(
                                            static_cast<float>(carr[0].Get<double>()),
                                            static_cast<float>(carr[1].Get<double>()),
                                            static_cast<float>(carr[2].Get<double>())
                                        );
                                    }
                                }

                                // intensity
                                if (lobj.Has("intensity") && lobj.Get("intensity").IsNumber()) {
                                    light.intensity = static_cast<float>(lobj.Get("intensity").Get<double>());
                                }

                                // width/height
                                if (lobj.Has("width") && lobj.Get("width").IsNumber()) {
                                    light.width = static_cast<float>(lobj.Get("width").Get<double>());
                                }
                                if (lobj.Has("height") && lobj.Get("height").IsNumber()) {
                                    light.height = static_cast<float>(lobj.Get("height").Get<double>());
                                }

                                outEntities.push_back(entity);

                                if (lobj.Has("name") && lobj.Get("name").IsString()) {
                                    AT_TRACE("Created ATLAS_lights_special (ext) light '{}' idx={} type={} width={} height={} intensity={}",
                                             lobj.Get("name").Get<std::string>(), lightIndex, type, light.width, light.height, light.intensity);
                                } else {
                                    AT_TRACE("Created ATLAS_lights_special (ext) light idx={} type={} width={} height={} intensity={}",
                                             lightIndex, type, light.width, light.height, light.intensity);
                                }
                            }
                        }
                    }
                }
            }
        }

        for (int childIdx: node.children) {
            processNode(registry, model, childIdx, worldTransform, virtualPath, outEntities);
        }
    }

    // =========================================================================
    // handleSkybox
    // =========================================================================
    void GLTFAccessor::handleSkybox(
        entt::registry &registry,
        entt::entity entity,
        const tinygltf::Model &model) {
        auto skyIt = model.extensions.find("ATLAS_skybox");
        if (skyIt == model.extensions.end()) return;

        const auto &ext = skyIt->second;
        auto &sky = registry.emplace<SkyboxComponent>(entity);
        auto loadCubemap = [&](const char *key) -> AssetHandle {
            if (ext.Has(key) && ext.Get(key).IsString())
                return assets.loadCubemap(ext.Get(key).Get<std::string>());
            return INVALID_ASSET_HANDLE;
        };

        sky.skyboxHandle = loadCubemap("skybox");
        sky.irradianceHandle = loadCubemap("irradiance");
        sky.prefilterHandle = loadCubemap("prefilter");

        AT_TRACE("Loaded skybox from ATLAS_skybox extension");
    }

    // =========================================================================
    // handlePostProcessing
    // =========================================================================
    void GLTFAccessor::handlePostProcessing(
        entt::registry &registry,
        entt::entity entity,
        const tinygltf::Model &model) {
        auto ppIt = model.extensions.find("ATLAS_post_processing");
        if (ppIt == model.extensions.end()) return;

        const auto &ext = ppIt->second;
        auto &pp = registry.emplace<PostProcessingVolumeComponent>(entity);

        auto getFloat = [&](const char *key, float fallback) -> float {
            return ext.Has(key) && ext.Get(key).IsNumber()
                       ? static_cast<float>(ext.Get(key).Get<double>())
                       : fallback;
        };

        pp.exposure = getFloat("exposure", 1.0f);
        pp.contrast = getFloat("contrast", 1.0f);
        pp.saturation = getFloat("saturation", 1.0f);

        if (ext.Has("colorTint") && ext.Get("colorTint").IsArray()) {
            const auto &arr = ext.Get("colorTint").Get<tinygltf::Value::Array>();
            if (arr.size() >= 3)
                pp.colorTint = {
                    static_cast<float>(arr[0].Get<double>()),
                    static_cast<float>(arr[1].Get<double>()),
                    static_cast<float>(arr[2].Get<double>())
                };
        }

        AT_TRACE("Loaded post processing: exposure={} contrast={} saturation={}", pp.exposure, pp.contrast, pp.saturation);
    }

    AssetHandle GLTFAccessor::resolveTexture(const tinygltf::Model &model,int texIdx, const std::vector<AssetHandle> &imageHandles) {
        if (texIdx < 0 || texIdx >= static_cast<int>(model.textures.size()))
            return INVALID_ASSET_HANDLE;

        int imgIdx = model.textures[texIdx].source;
        if (imgIdx < 0 || imgIdx >= static_cast<int>(imageHandles.size()))
            return INVALID_ASSET_HANDLE;

        return imageHandles[imgIdx];
    }

    glm::mat4 GLTFAccessor::getNodeTransform(const tinygltf::Node &node) {
        auto mat = glm::mat4(1.0f);

        if (node.matrix.size() == 16) {
            mat = glm::make_mat4x4(node.matrix.data());
        } else {
            if (node.translation.size() == 3) {
                glm::vec3 translation(node.translation[0], node.translation[1], node.translation[2]);
                translation = glm::vec3(-translation.x, -translation.y, translation.z); // rotate 180 around Z
                mat = glm::translate(mat, translation);
            }

            if (node.rotation.size() == 4) {
                const glm::quat rotation(
                    static_cast<float>(node.rotation[3]), // w
                    static_cast<float>(node.rotation[0]), // x
                    static_cast<float>(node.rotation[1]), // y
                    static_cast<float>(node.rotation[2]) // z
                );
                mat *= glm::mat4_cast(rotation);
            }

            if (node.scale.size() == 3) {
                mat = glm::scale(mat, glm::vec3(
                                     node.scale[0],
                                     node.scale[1],
                                     node.scale[2]));
            }
        }

        return mat;
    }
} // namespace Atlas
