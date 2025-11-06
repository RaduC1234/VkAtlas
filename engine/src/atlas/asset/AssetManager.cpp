#include "AssetManager.hpp"

#include "core/Log.hpp"
#include <fstream>
#include <set>

// Do not define TINYOBJLOADER_IMPLEMENTATION here — the dependency already compiles its implementation
#include <tiny_obj_loader.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>
#include <memory>

#include <stb_image.h>

// Allow tinygltf to use its own stb image implementation from the dependency build
#include <tiny_gltf.h>

#if defined(__ANDROID__)
#include "android/AndroidAssetManager.hpp"
#elif defined(_WIN32)
#include "desktop/DesktopAssetManager.hpp"
#endif

namespace std {
    template<>
    struct hash<Atlas::Mesh::Vertex> {
        size_t operator()(const Atlas::Mesh::Vertex &vertex) const noexcept {
            size_t seed = 0;
            std::hash<glm::vec3> hasher;
            std::hash<glm::vec2> hasher2;
            seed ^= hasher(vertex.position) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            seed ^= hasher(vertex.color) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            seed ^= hasher(vertex.normal) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            seed ^= hasher2(vertex.uv) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            return seed;
        }
    };
}

namespace Atlas {
    std::shared_ptr<AssetManager> AssetManager::instance = nullptr;

    // Protected constructor implementation
    AssetManager::AssetManager(Device &device, void *nativeApp) : device(device), nativeApp(nativeApp) {
    }

    AssetManager &AssetManager::create(Device &device, void *nativeApp) {
        if (instance) {
            AT_WARN("AssetManager already exists. Returning existing instance.");
            return *instance;
        }

#if defined(__ANDROID__)
        instance = std::shared_ptr<AndroidAssetManager>(new AndroidAssetManager(device, nativeApp));
        AT_INFO("Created Android AssetManager instance");
#elif defined(_WIN32)
        instance = std::make_shared<DesktopAssetManager>(device, nativeApp);
        AT_INFO("Created Desktop AssetManager instance");
#else
        AT_ERROR("Unsupported platform for AssetManager");
        throw std::runtime_error("Unsupported platform for AssetManager");
#endif
        return *instance;
    }

    AssetManager &AssetManager::get() {
        if (!instance) {
            AT_ERROR("AssetManager not initialized! Call AssetManager::create() first.");
            throw std::runtime_error("AssetManager::get() called before create()");
        }
        return *instance;
    }

    void AssetManager::destroy() {
        instance.reset();
        AT_INFO("AssetManager instance destroyed");
    }

    AssetHandle AssetManager::loadTexture(const std::string &virtualPath) {
        auto it = pathToHandle.find(virtualPath);
        if (it != pathToHandle.end()) {
            AT_TRACE("Texture already loaded: {} (handle: {})", virtualPath, it->second);
            return it->second;
        }

        AssetHandle handle = nextHandle++;
        pathToHandle[virtualPath] = handle;
        handleToPath[handle] = virtualPath;

        std::filesystem::path fullPath = getAssetsPath() / virtualPath;

        int width, height, channels;
        stbi_set_flip_vertically_on_load(true);
        unsigned char *pixels = stbi_load(fullPath.string().c_str(), &width, &height, &channels, STBI_rgb_alpha);

        if (!pixels) {
            AT_ERROR("Failed to load texture: {}", fullPath.string());
            return INVALID_ASSET_HANDLE;
        }

        VkFormat format = VK_FORMAT_R8G8B8A8_SRGB;

        auto sampler = Sampler::create(device, pixels, width, height, format);
        texturePool[handle] = sampler;

        stbi_image_free(pixels);

        AT_TRACE("Loaded texture: {} (handle: {}, {}x{})", virtualPath, handle, width, height);
        return handle;
    }

    AssetHandle AssetManager::loadMesh(const std::string &virtualPath) {
        auto it = pathToHandle.find(virtualPath);
        if (it != pathToHandle.end()) {
            AT_TRACE("Mesh already loaded: {} (handle: {})", virtualPath, it->second);
            return it->second;
        }

        AssetHandle handle = nextHandle++;
        pathToHandle[virtualPath] = handle;
        handleToPath[handle] = virtualPath;

        std::filesystem::path fullPath = getAssetsPath() / virtualPath;

        Mesh::Builder builder{};
        tinyobj::attrib_t attrib;
        std::vector<tinyobj::shape_t> shapes;
        std::vector<tinyobj::material_t> materials;
        std::string warn, err;

        if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, fullPath.string().c_str())) {
            AT_ERROR("Failed to load mesh: {} - {}", fullPath.string(), warn + err);
            return INVALID_ASSET_HANDLE;
        }

        builder.vertices.clear();
        builder.indices.clear();

        std::unordered_map<Mesh::Vertex, uint32_t> uniqueVertices;
        for (const auto &shape: shapes) {
            for (const auto &index: shape.mesh.indices) {
                Mesh::Vertex vertex{};

                if (index.vertex_index >= 0) {
                    vertex.position = glm::vec3(
                        attrib.vertices[3 * index.vertex_index + 0],
                        attrib.vertices[3 * index.vertex_index + 1],
                        attrib.vertices[3 * index.vertex_index + 2]
                    );

                    if (!attrib.colors.empty()) {
                        vertex.color = glm::vec3(
                            attrib.colors[3 * index.vertex_index + 0],
                            attrib.colors[3 * index.vertex_index + 1],
                            attrib.colors[3 * index.vertex_index + 2]
                        );
                    } else {
                        vertex.color = glm::vec3(1.0f);
                    }
                }

                if (index.normal_index >= 0 && !attrib.normals.empty()) {
                    vertex.normal = glm::vec3(
                        attrib.normals[3 * index.normal_index + 0],
                        attrib.normals[3 * index.normal_index + 1],
                        attrib.normals[3 * index.normal_index + 2]
                    );
                } else {
                    vertex.normal = glm::vec3(0.0f);
                }

                if (index.texcoord_index >= 0 && !attrib.texcoords.empty()) {
                    vertex.uv = glm::vec2(
                        attrib.texcoords[2 * index.texcoord_index + 0],
                        attrib.texcoords[2 * index.texcoord_index + 1]
                    );
                } else {
                    vertex.uv = glm::vec2(0.0f);
                }

                if (uniqueVertices.count(vertex) == 0) {
                    uniqueVertices[vertex] = static_cast<uint32_t>(builder.vertices.size());
                    builder.vertices.push_back(vertex);
                }
                builder.indices.push_back(uniqueVertices[vertex]);
            }
        }

        auto mesh = std::make_unique<Mesh>(device, builder);
        meshPool[handle] = std::move(mesh);

        AT_TRACE("Loaded mesh: {} (handle: {}, {} vertices, {} indices)", virtualPath, handle, builder.vertices.size(), builder.indices.size());
        return handle;
    }

    AssetHandle AssetManager::createSphere(float radius, uint32_t segments, uint32_t rings) {
        std::string virtualPath = "procedural://sphere_r" + std::to_string(radius) +
                                  "_s" + std::to_string(segments) +
                                  "_r" + std::to_string(rings);

        auto it = pathToHandle.find(virtualPath);
        if (it != pathToHandle.end()) {
            AT_TRACE("Sphere mesh already created: {} (handle: {})", virtualPath, it->second);
            return it->second;
        }

        AssetHandle handle = nextHandle++;
        pathToHandle[virtualPath] = handle;
        handleToPath[handle] = virtualPath;

        auto mesh = Mesh::createSphere(device, radius, segments, rings);
        meshPool[handle] = std::move(mesh);

        AT_TRACE("Created procedural sphere mesh (handle: {}, radius: {}, segments: {}, rings: {})",
                 handle, radius, segments, rings);
        return handle;
    }

    AssetHandle AssetManager::createCube(float size) {
        std::string virtualPath = "procedural://cube_s" + std::to_string(size);

        auto it = pathToHandle.find(virtualPath);
        if (it != pathToHandle.end()) {
            AT_TRACE("Cube mesh already created: {} (handle: {})", virtualPath, it->second);
            return it->second;
        }

        AssetHandle handle = nextHandle++;
        pathToHandle[virtualPath] = handle;
        handleToPath[handle] = virtualPath;

        auto mesh = Mesh::createCube(device, size);
        meshPool[handle] = std::move(mesh);

        AT_TRACE("Created procedural cube mesh (handle: {}, size: {})", handle, size);
        return handle;
    }

    AssetHandle AssetManager::createDefaultWhiteTexture() {
        std::string virtualPath = "procedural://white_1x1";

        auto it = pathToHandle.find(virtualPath);
        if (it != pathToHandle.end()) {
            AT_TRACE("Default white texture already created: {} (handle: {})", virtualPath, it->second);
            return it->second;
        }

        AssetHandle handle = nextHandle++;
        pathToHandle[virtualPath] = handle;
        handleToPath[handle] = virtualPath;

        constexpr unsigned char pixels[4] = {255, 255, 255, 255};
        auto texture = Sampler::create(device, pixels, 1, 1);
        texturePool[handle] = texture;

        AT_TRACE("Created default white texture (handle: {})", handle);
        return handle;
    }

    // Helper to read accessor data pointer and stride
    static const unsigned char *accessorDataPtr(const tinygltf::Model &model, const tinygltf::Accessor &accessor, size_t &strideOut, int &numComponentsOut) {
        const tinygltf::BufferView &bv = model.bufferViews[accessor.bufferView];
        const tinygltf::Buffer &buffer = model.buffers[bv.buffer];
        const unsigned char *base = buffer.data.data() + bv.byteOffset + accessor.byteOffset;

        switch (accessor.type) {
            case TINYGLTF_TYPE_SCALAR: numComponentsOut = 1;
                break;
            case TINYGLTF_TYPE_VEC2: numComponentsOut = 2;
                break;
            case TINYGLTF_TYPE_VEC3: numComponentsOut = 3;
                break;
            case TINYGLTF_TYPE_VEC4: numComponentsOut = 4;
                break;
            default: numComponentsOut = 1;
                break;
        }

        // Use tinygltf helper to determine component size in bytes
        auto componentSize = static_cast<size_t>(tinygltf::GetComponentSizeInBytes(accessor.componentType));

        size_t stride = bv.byteStride ? static_cast<size_t>(bv.byteStride) : (componentSize * static_cast<size_t>(numComponentsOut));
        strideOut = stride;
        return base;
    }

    AssetHandle AssetManager::loadGltf(const std::string &virtualPath) {
        // If already loaded, return existing handle (caller may change behavior)
        auto it = pathToHandle.find(virtualPath);
        if (it != pathToHandle.end()) {
            AT_TRACE("gltf already loaded: {} (handle: {})", virtualPath, it->second);
            return it->second;
        }

        std::filesystem::path fullPath = getAssetsPath() / virtualPath;

        tinygltf::Model model;
        tinygltf::TinyGLTF loader;
        loader.SetImageLoader(&tinygltf::LoadImageData, nullptr);
        std::string err;
        std::string warn;
        bool ret = false;

        const std::string ext = fullPath.extension().string();
        if (ext == ".glb") {
            ret = loader.LoadBinaryFromFile(&model, &err, &warn, fullPath.string());
        } else {
            ret = loader.LoadASCIIFromFile(&model, &err, &warn, fullPath.string());
        }

        if (!warn.empty()) AT_WARN("gltf warning: {}", warn);
        if (!err.empty()) AT_ERROR("gltf error: {}", err);
        if (!ret) {
            AT_ERROR("Failed to load glTF: {}", fullPath.string());
            return INVALID_ASSET_HANDLE;
        }

        // Prepare containers
        std::vector<AssetHandle> imageHandles(model.images.size(), INVALID_ASSET_HANDLE);
        std::vector<std::vector<AssetHandle>> meshIndexToHandles(model.meshes.size());
        AssetHandle firstMeshHandle = INVALID_ASSET_HANDLE;

        // --- First pass: determine which images are used as base color textures ---
        std::set<int> baseColorImageIndices;
        for (const auto &mat : model.materials) {
            if (mat.pbrMetallicRoughness.baseColorTexture.index >= 0) {
                int texIdx = mat.pbrMetallicRoughness.baseColorTexture.index;
                if (texIdx < static_cast<int>(model.textures.size())) {
                    int imgIdx = model.textures[texIdx].source;
                    if (imgIdx >= 0 && imgIdx < static_cast<int>(model.images.size())) {
                        baseColorImageIndices.insert(imgIdx);
                    }
                }
            }
        }

        // --- Second pass: only load base color textures ---
        for (int imgIdx : baseColorImageIndices) {
            const tinygltf::Image &img = model.images[imgIdx];

            if (img.image.empty()) {
                // If no image data (rare in GLB), attempt to load from URI if present
                if (!img.uri.empty()) {
                    std::filesystem::path imagePath = fullPath.parent_path() / img.uri;
                    int w = 0, h = 0, c = 0;
                    stbi_set_flip_vertically_on_load(true);
                    unsigned char* pixels = stbi_load(imagePath.string().c_str(), &w, &h, &c, STBI_rgb_alpha);
                    if (!pixels) {
                        AT_WARN("Failed to load external image: {}", imagePath.string());
                        continue;
                    }
                    AssetHandle hnd = nextHandle++;
                    std::string imgPath = virtualPath + "#image" + std::to_string(imgIdx);
                    pathToHandle[imgPath] = hnd;
                    handleToPath[hnd] = imgPath;
                    VkFormat format = VK_FORMAT_R8G8B8A8_SRGB;
                    auto sampler = Sampler::create(device, pixels, static_cast<uint32_t>(w), static_cast<uint32_t>(h), format);
                    texturePool[hnd] = sampler;
                    stbi_image_free(pixels);
                    imageHandles[imgIdx] = hnd;
                    AT_TRACE("Loaded base color texture from URI: image[{}] (handle: {})", imgIdx, hnd);
                }
                continue;
            }

            // img.image contains raw pixel bytes (usually RGBA)
            AssetHandle hnd = nextHandle++;
            std::string imgPath = virtualPath + "#image" + std::to_string(imgIdx);
            pathToHandle[imgPath] = hnd;
            handleToPath[hnd] = imgPath;

            // Use SRGB for base color textures
            VkFormat format = VK_FORMAT_R8G8B8A8_SRGB;
            auto sampler = Sampler::create(device, img.image.data(), static_cast<uint32_t>(img.width), static_cast<uint32_t>(img.height), format);
            texturePool[hnd] = sampler;
            imageHandles[imgIdx] = hnd;
            AT_TRACE("Loaded base color texture: image[{}] (handle: {})", imgIdx, hnd);
        }

        // --- Debug: Log material and texture information ---
        AT_INFO("=== glTF Debug Info for: {} ===", virtualPath);
        AT_INFO("Total images: {}", model.images.size());
        AT_INFO("Loaded base color textures: {}", baseColorImageIndices.size());
        AT_INFO("Total materials: {}", model.materials.size());
        for (size_t i = 0; i < model.materials.size(); ++i) {
            const tinygltf::Material &mat = model.materials[i];
            AT_INFO("Material[{}]: name='{}', baseColorTexture.index={}",
                    i, mat.name, mat.pbrMetallicRoughness.baseColorTexture.index);
            if (mat.pbrMetallicRoughness.baseColorTexture.index >= 0) {
                int texIdx = mat.pbrMetallicRoughness.baseColorTexture.index;
                if (texIdx < static_cast<int>(model.textures.size())) {
                    int imgIdx = model.textures[texIdx].source;
                    AT_INFO("  -> Uses texture[{}] -> image[{}]", texIdx, imgIdx);
                }
            }
        }

        // --- Iterate meshes and primitives -> create Mesh assets ---
        for (size_t meshIndex = 0; meshIndex < model.meshes.size(); ++meshIndex) {
            const tinygltf::Mesh &mesh = model.meshes[meshIndex];
            for (size_t primIndex = 0; primIndex < mesh.primitives.size(); ++primIndex) {
                const tinygltf::Primitive &prim = mesh.primitives[primIndex];

                // Require POSITION
                if (prim.attributes.find("POSITION") == prim.attributes.end()) {
                    AT_WARN("Primitive missing POSITION attribute, skipping (mesh %zu prim %zu)", meshIndex, primIndex);
                    continue;
                }

                const tinygltf::Accessor &posAcc = model.accessors[prim.attributes.at("POSITION")];
                size_t posStride;
                int posComp;
                const unsigned char *posBase = accessorDataPtr(model, posAcc, posStride, posComp);

                bool hasNormal = prim.attributes.find("NORMAL") != prim.attributes.end();
                size_t normStride = 0;
                int normComp = 0;
                const unsigned char *normBase = nullptr;
                if (hasNormal) normBase = accessorDataPtr(model, model.accessors[prim.attributes.at("NORMAL")], normStride, normComp);

                bool hasTex = prim.attributes.find("TEXCOORD_0") != prim.attributes.end();
                size_t texStride = 0;
                int texComp = 0;
                const unsigned char *texBase = nullptr;
                if (hasTex) {
                    texBase = accessorDataPtr(model, model.accessors[prim.attributes.at("TEXCOORD_0")], texStride, texComp);
                    AT_INFO("Mesh[{}] Prim[{}] has UV coordinates (TEXCOORD_0)", meshIndex, primIndex);
                } else {
                    AT_WARN("Mesh[{}] Prim[{}] MISSING UV coordinates! Texture will not display.", meshIndex, primIndex);
                }

                bool hasColor = prim.attributes.find("COLOR_0") != prim.attributes.end();
                size_t colorStride = 0;
                int colorComp = 0;
                const unsigned char *colorBase = nullptr;
                if (hasColor) colorBase = accessorDataPtr(model, model.accessors[prim.attributes.at("COLOR_0")], colorStride, colorComp);

                Mesh::Builder builder;
                builder.vertices.clear();
                builder.indices.clear();
                std::unordered_map<Mesh::Vertex, uint32_t> uniqueVertices;

                // Build vertices WITH TRANSFORMATIONS APPLIED
                for (size_t v = 0; v < posAcc.count; ++v) {
                    Mesh::Vertex vert{};
                    // positions: apply 180° rotation around Z
                    auto pv = reinterpret_cast<const float *>(posBase + v * posStride);
                    vert.position = glm::vec3(-pv[0], -pv[1], pv[2]);

                    if (hasNormal && normBase) {
                        auto nv = reinterpret_cast<const float *>(normBase + v * normStride);
                        vert.normal = glm::vec3(nv[0], nv[1], nv[2]);  // Keep normals as-is
                    } else {
                        vert.normal = glm::vec3(0.0f);
                    }

                    if (hasTex && texBase) {
                        auto tv = reinterpret_cast<const float *>(texBase + v * texStride);
                        vert.uv = glm::vec2(tv[0], 1.0f - tv[1]);  // Flip V coordinate for glTF
                    } else {
                        vert.uv = glm::vec2(0.0f);
                    }

                    if (hasColor && colorBase) {
                        auto cv = reinterpret_cast<const float *>(colorBase + v * colorStride);
                        vert.color = glm::vec3(cv[0], cv[1], cv[2]);
                    } else {
                        vert.color = glm::vec3(1.0f);
                    }

                    // Only add vertex if it doesn't already exist
                    if (uniqueVertices.count(vert) == 0) {
                        uniqueVertices[vert] = static_cast<uint32_t>(builder.vertices.size());
                        builder.vertices.push_back(vert);
                    }
                }

                // Build indices - just map to the already-transformed vertices
                if (prim.indices >= 0) {
                    const tinygltf::Accessor &idxAcc = model.accessors[prim.indices];
                    const tinygltf::BufferView &idxView = model.bufferViews[idxAcc.bufferView];
                    const tinygltf::Buffer &idxBuffer = model.buffers[idxView.buffer];
                    const unsigned char *base = idxBuffer.data.data() + idxView.byteOffset + idxAcc.byteOffset;
                    size_t idxStride = idxView.byteStride ? static_cast<size_t>(idxView.byteStride) : tinygltf::GetComponentSizeInBytes(idxAcc.componentType);

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
                                throw std::runtime_error("Unsupported index component type in glTF");
                        }

                        // Just push the index directly - vertices are already built
                        builder.indices.push_back(index);
                    }
                } else {
                    // non-indexed: create sequential indices
                    for (uint32_t i = 0; i < static_cast<uint32_t>(posAcc.count); ++i) {
                        builder.indices.push_back(i);
                    }
                }

                // Create the Mesh asset
                AssetHandle meshHandle = nextHandle++;
                std::string meshPath = virtualPath + "#mesh" + std::to_string(meshIndex) + "_prim" + std::to_string(primIndex);
                pathToHandle[meshPath] = meshHandle;
                handleToPath[meshHandle] = meshPath;

                auto meshPtr = std::make_unique<Mesh>(device, builder);
                meshPool[meshHandle] = std::move(meshPtr);
                meshIndexToHandles[meshIndex].push_back(meshHandle);

                // --- Map material base color texture to a named path ---
                if (prim.material >= 0 && prim.material < static_cast<int>(model.materials.size())) {
                    const tinygltf::Material &mat = model.materials[prim.material];

                    // Check for base color texture in PBR metallic-roughness workflow
                    if (mat.pbrMetallicRoughness.baseColorTexture.index >= 0) {
                        int texIdx = mat.pbrMetallicRoughness.baseColorTexture.index;
                        if (texIdx < static_cast<int>(model.textures.size())) {
                            int imgIdx = model.textures[texIdx].source;
                            if (imgIdx >= 0 && imgIdx < static_cast<int>(imageHandles.size())) {
                                // Use material name if available, otherwise use index
                                std::string matName = mat.name.empty() ? ("mat" + std::to_string(prim.material)) : mat.name;
                                std::string baseColorPath = meshPath + "_baseColor";
                                std::string matBaseColorPath = virtualPath + "#" + matName + "_baseColor";

                                // Register both paths pointing to the same texture
                                pathToHandle[baseColorPath] = imageHandles[imgIdx];
                                pathToHandle[matBaseColorPath] = imageHandles[imgIdx];

                                AT_TRACE("Mapped base color texture: {} -> image[{}] (handle: {})",
                                         baseColorPath, imgIdx, imageHandles[imgIdx]);
                            }
                        }
                    }
                }

                if (firstMeshHandle == INVALID_ASSET_HANDLE) firstMeshHandle = meshHandle;
            }
        }

        // --- Iterate nodes (scene graph) ---
        for (size_t nodeIndex = 0; nodeIndex < model.nodes.size(); ++nodeIndex) {
            const tinygltf::Node &node = model.nodes[nodeIndex];
            // If node references a mesh, map it to the mesh handles we created for that mesh index
            if (node.mesh >= 0 && node.mesh < static_cast<int>(meshIndexToHandles.size())) {
                const auto &handles = meshIndexToHandles[node.mesh];
                if (!handles.empty()) {
                    // Create a convenient virtual path for this node
                    std::string nodePath = virtualPath + "#node" + std::to_string(nodeIndex);
                    // Map the node to the first mesh handle for compatibility with older API
                    pathToHandle[nodePath] = handles[0];
                    handleToPath[handles[0]] = nodePath;
                }
            }
        }

        if (firstMeshHandle == INVALID_ASSET_HANDLE) {
            AT_ERROR("No meshes created from glTF: {}", virtualPath);
            return INVALID_ASSET_HANDLE;
        }

        AT_TRACE("Loaded glTF: {} (first mesh handle: {})", virtualPath, firstMeshHandle);
        return firstMeshHandle;
    }

    std::shared_ptr<Sampler> AssetManager::getTexture(AssetHandle handle) {
        if (handle == INVALID_ASSET_HANDLE) return nullptr;

        auto it = texturePool.find(handle);
        return it != texturePool.end() ? it->second : nullptr;
    }

    std::shared_ptr<Mesh> AssetManager::getMesh(AssetHandle handle) {
        if (handle == INVALID_ASSET_HANDLE) return nullptr;

        auto it = meshPool.find(handle);
        return it != meshPool.end() ? it->second : nullptr;
    }

    std::string AssetManager::getPath(AssetHandle handle) const {
        auto it = handleToPath.find(handle);
        return it != handleToPath.end() ? it->second : "";
    }

    std::vector<char> AssetManager::loadTextFile(const std::string &resource) {
        std::filesystem::path filePath = get().getAssetsPath() / resource;

        std::ifstream file(filePath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            AT_ERROR("Failed to open file: {}", filePath.string());
            return {};
        }

        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);

        std::vector<char> buffer(size);
        if (!file.read(buffer.data(), size)) {
            AT_ERROR("Failed to read file: {}", filePath.string());
            return {};
        }

        file.close();
        AT_TRACE("Loaded file: {} ({} bytes)", resource, size);
        return buffer;
    }
}