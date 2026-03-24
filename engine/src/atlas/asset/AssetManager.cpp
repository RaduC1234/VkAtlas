#include "AssetManager.hpp"

#include "core/Log.hpp"
#include <fstream>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>
#include <stb_image.h>
#include <tiny_gltf.h>
#include <tiny_obj_loader.h>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "entity/Object.hpp"

#if defined(ATLAS_PLATFORM_ANDROID)
#include "android/AndroidAssetManager.hpp"
#elif defined(ATLAS_PLATFORM_DESKTOP)
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

    AssetManager::AssetManager(Device &device, void *nativeApp) : device(device), nativeApp(nativeApp) {
    }

    AssetManager &AssetManager::create(Device &device, void *nativeApp) {
        if (instance) {
            AT_WARN("AssetManager already exists. Returning existing instance.");
            return *instance;
        }

#if defined(ATLAS_PLATFORM_ANDROID)
        instance = std::shared_ptr<AndroidAssetManager>(new AndroidAssetManager(device, nativeApp));
        AT_INFO("Created Android AssetManager instance");
#elif defined(ATLAS_PLATFORM_DESKTOP)
        instance = std::make_shared<DesktopAssetManager>(device, nativeApp);
        AT_INFO("Created Desktop AssetManager instance");
#else
#error Unsupported platform for AssetManager
#endif
        return *instance;
    }

    AssetManager &AssetManager::get() {
        assert(instance && "AssetManager::get() called before create()");

        return *instance;
    }

    void AssetManager::destroy() {
        instance.reset();
        AT_INFO("AssetManager instance destroyed");
    }

    AssetHandle AssetManager::loadTexture(const std::string &virtualPath, VkFormat format, VkSamplerAddressMode addressMode) {
        auto it = pathToHandle.find(virtualPath);
        if (it != pathToHandle.end()) {
            AT_TRACE("Texture already loaded: {} (handle: {})", virtualPath, it->second);
            return it->second;
        }

        std::filesystem::path fullPath = getAssetsPath() / virtualPath;
        std::string ext = fullPath.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        int width, height, channels;
        void *pixels = nullptr;
        bool isHDR = false;

        stbi_set_flip_vertically_on_load(true);

        if (ext == ".hdr") {
            pixels = stbi_loadf(fullPath.string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
            isHDR = true;
            // Override format for HDR if caller left it as default LDR format
            if (format == VK_FORMAT_R8G8B8_SRGB || format == VK_FORMAT_R8G8B8A8_SRGB) {
                format = VK_FORMAT_R32G32B32A32_SFLOAT;
            }
        } else {
            pixels = stbi_load(fullPath.string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
        }

        if (!pixels) {
            AT_ERROR("Failed to load texture: {}", fullPath.string());
            return INVALID_ASSET_HANDLE;
        }

        AssetHandle handle = nextHandle++;
        pathToHandle[virtualPath] = handle;
        handleToPath[handle] = virtualPath;

        auto sampler = Sampler::create(device, pixels, static_cast<uint32_t>(width), static_cast<uint32_t>(height), format, addressMode);
        texturePool[handle] = sampler;

        stbi_image_free(pixels);

        AT_TRACE("Loaded texture: {} (handle: {}, {}x{}, hdr: {})", virtualPath, handle, width, height, isHDR);
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

    AssetHandle AssetManager::loadCubemap(const std::string &virtualPath) {
        auto it = pathToHandle.find(virtualPath);
        if (it != pathToHandle.end()) {
            AT_TRACE("Cubemap already loaded: {} (handle: {})", virtualPath, it->second);
            return it->second;
        }
        AssetHandle handle = nextHandle++;
        pathToHandle[virtualPath] = handle;
        handleToPath[handle] = virtualPath;

        std::filesystem::path fullPath = getAssetsPath() / virtualPath;

        // Check if file exists
        if (!std::filesystem::exists(fullPath)) {
            AT_ERROR("Cubemap file not found: {}", fullPath.string());
            pathToHandle.erase(virtualPath);
            handleToPath.erase(handle);
            return INVALID_ASSET_HANDLE;
        }

        // Determine if this is an HDR file or a regular cubemap texture
        std::string extension = fullPath.extension().string();
        std::ranges::transform(extension, extension.begin(), ::tolower);

        try {
            std::shared_ptr<Cubemap> cubemap;

            if (extension == ".hdr" || extension == ".ktx2") {
                // Load HDR equirectangular and convert to cubemap
                cubemap = Cubemap::create(device, fullPath.string());
            } else {
                // For non-HDR files, we need 6 face images
                // This path should use the 6-face overload instead
                AT_ERROR("Single texture cubemap loading requires HDR format. Use loadCubemap with 6 faces for other formats: {}", fullPath.string());
                pathToHandle.erase(virtualPath);
                handleToPath.erase(handle);
                return INVALID_ASSET_HANDLE;
            }

            cubemapPool[handle] = cubemap;
            AT_TRACE("Loaded cubemap: {} (handle: {})", virtualPath, handle);
            return handle;
        } catch (const std::exception &e) {
            AT_ERROR("Failed to load cubemap: {} - {}", fullPath.string(), e.what());
            pathToHandle.erase(virtualPath);
            handleToPath.erase(handle);
            return INVALID_ASSET_HANDLE;
        }
    }

    AssetHandle AssetManager::loadCubemap(const std::string &right, const std::string &left,
                                          const std::string &top, const std::string &bottom,
                                          const std::string &front, const std::string &back) {
        // Create a composite virtual path for the cubemap
        std::string virtualPath = "cubemap://" + right + "|" + left + "|" + top + "|" + bottom + "|" + front + "|" + back;

        auto it = pathToHandle.find(virtualPath);
        if (it != pathToHandle.end()) {
            AT_TRACE("Cubemap already loaded: {} (handle: {})", virtualPath, it->second);
            return it->second;
        }

        AssetHandle handle = nextHandle++;
        pathToHandle[virtualPath] = handle;
        handleToPath[handle] = virtualPath;

        try {
            // Resolve all face paths to full filesystem paths
            std::array<std::string, 6> facePaths = {
                (getAssetsPath() / right).string(),
                (getAssetsPath() / left).string(),
                (getAssetsPath() / top).string(),
                (getAssetsPath() / bottom).string(),
                (getAssetsPath() / front).string(),
                (getAssetsPath() / back).string()
            };

            // Verify all files exist
            for (size_t i = 0; i < facePaths.size(); ++i) {
                if (!std::filesystem::exists(facePaths[i])) {
                    AT_ERROR("Cubemap face file not found: {}", facePaths[i]);
                    pathToHandle.erase(virtualPath);
                    handleToPath.erase(handle);
                    return INVALID_ASSET_HANDLE;
                }
            }

            // Create the cubemap using the 6-face method
            auto cubemap = Cubemap::create(device, facePaths);
            cubemapPool[handle] = cubemap;

            AT_TRACE("Loaded 6-face cubemap (handle: {})", handle);
            return handle;
        } catch (const std::exception &e) {
            AT_ERROR("Failed to load 6-face cubemap: {}", e.what());
            pathToHandle.erase(virtualPath);
            handleToPath.erase(handle);
            return INVALID_ASSET_HANDLE;
        }
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

    AssetHandle AssetManager::createPlane(float width, float height) {
        std::string virtualPath = "procedural://plane_wh" + std::to_string(width) + "_" + std::to_string(height);

        auto it = pathToHandle.find(virtualPath);
        if (it != pathToHandle.end()) {
            AT_TRACE("Plane mesh already created: {} (handle: {})", virtualPath, it->second);
        }

        AssetHandle handle = nextHandle++;
        pathToHandle[virtualPath] = handle;
        handleToPath[handle] = virtualPath;

        auto mesh = Mesh::createPlane(device, width, height);
        meshPool[handle] = std::move(mesh);

        AT_TRACE("Created procedural plane mesh (handle: {}, size: {} {})", handle, width, height);
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
        const auto texture = Sampler::create(device, pixels, 1, 1);
        texturePool[handle] = texture;

        AT_TRACE("Created default white texture (handle: {})", handle);
        return handle;
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

    std::shared_ptr<Cubemap> AssetManager::getCubemap(AssetHandle handle) {
        if (handle == INVALID_ASSET_HANDLE) return nullptr;

        auto it = cubemapPool.find(handle);
        return it != cubemapPool.end() ? it->second : nullptr;
    }

    std::string AssetManager::getPath(AssetHandle handle) const {
        auto it = handleToPath.find(handle);
        return it != handleToPath.end() ? it->second : "";
    }

    bool AssetManager::freeTexture(AssetHandle handle) {
        if (handle == INVALID_ASSET_HANDLE) {
            return false;
        }

        auto it = texturePool.find(handle);
        if (it == texturePool.end()) {
            return false;
        }

        // Get the path before removing from handleToPath
        std::string path = getPath(handle);

        // Get the texture's hash to remove from hashToHandle
        size_t textureHash = it->second->getHash();

        // Remove from all internal maps
        texturePool.erase(it);
        handleToPath.erase(handle);
        if (!path.empty()) {
            pathToHandle.erase(path);
        }

        // Remove from hash-based lookup if this handle owns the hash
        auto hashIt = hashToHandle.find(textureHash);
        if (hashIt != hashToHandle.end() && hashIt->second == handle) {
            hashToHandle.erase(hashIt);
        }

        AT_TRACE("Freed texture asset: {} (handle: {})", path, handle);
        return true;
    }

    bool AssetManager::freeMesh(AssetHandle handle) {
        if (handle == INVALID_ASSET_HANDLE) {
            return false;
        }

        auto it = meshPool.find(handle);
        if (it == meshPool.end()) {
            return false;
        }

        // Get the path before removing from handleToPath
        std::string path = getPath(handle);

        // Get the mesh's hash to remove from hashToHandle
        size_t meshHash = it->second->getHash();

        // Remove from all internal maps
        meshPool.erase(it);
        handleToPath.erase(handle);
        if (!path.empty()) {
            pathToHandle.erase(path);
        }

        // Remove from hash-based lookup if this handle owns the hash
        auto hashIt = hashToHandle.find(meshHash);
        if (hashIt != hashToHandle.end() && hashIt->second == handle) {
            hashToHandle.erase(hashIt);
        }

        AT_TRACE("Freed mesh asset: {} (handle: {})", path, handle);
        return true;
    }

    bool AssetManager::freeCubemap(AssetHandle handle) {
        if (handle == INVALID_ASSET_HANDLE) {
            return false;
        }

        auto it = cubemapPool.find(handle);
        if (it == cubemapPool.end()) {
            return false;
        }

        // Get the path before removing from handleToPath
        std::string path = getPath(handle);

        // Remove from all internal maps
        cubemapPool.erase(it);
        handleToPath.erase(handle);
        if (!path.empty()) {
            pathToHandle.erase(path);
        }

        AT_TRACE("Freed cubemap asset: {} (handle: {})", path, handle);
        return true;
    }

    bool AssetManager::freeAsset(AssetHandle handle) {
        if (handle == INVALID_ASSET_HANDLE) {
            return false;
        }

        // Try to free from each pool
        if (freeTexture(handle)) return true;
        if (freeMesh(handle)) return true;
        if (freeCubemap(handle)) return true;

        AT_WARN("Asset handle {} not found in any pool", handle);
        return false;
    }

    std::vector<char> AssetManager::loadFileAsU8(const std::string &path) {
        std::filesystem::path filePath = get().getAssetsPath() / path;

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
        AT_TRACE("Loaded file: {} ({} bytes)", path, size);
        return buffer;
    }

    void AssetManager::saveFileAsU8(const std::vector<char> &data, const std::string &path) {
        try {
            std::filesystem::path filePath = get().getAssetsPath() / path;

            std::ofstream out(filePath, std::ios::binary);
            if (!out.is_open()) {
                AT_ERROR("Failed to open file for writing: {}", filePath.string());
                return;
            }

            if (!data.empty()) {
                out.write(data.data(), static_cast<std::streamsize>(data.size()));
                if (!out) {
                    AT_ERROR("Failed to write data to file: {}", filePath.string());
                    return;
                }
            }

            out.close();
            AT_TRACE("Saved {} bytes to {}", data.size(), filePath.string());
        } catch (const std::exception &e) {
            AT_ERROR("Exception while saving file: {}", e.what());
        }
    }

    std::string AssetManager::loadFileAsString(const std::string &path) {
        const auto data = loadFileAsU8(path);
        return {data.begin(), data.end()};
    }

    void AssetManager::saveFileAsString(const std::string &data, const std::string &path) {
        const std::vector bytes(data.begin(), data.end());
        saveFileAsU8(bytes, path);
    }

    AssetHandle AssetManager::getOrCreateMesh(const std::vector<Mesh::Vertex> &vertices, const std::vector<uint32_t> &indices, const std::string &virtualPath) {
        size_t meshHash = Mesh::computeHash(vertices, indices);

        // Check if we already have this asset loaded (by hash)
        auto hashIt = hashToHandle.find(meshHash);
        if (hashIt != hashToHandle.end()) {
            AT_TRACE("Mesh already loaded (hash match): {} -> handle {}", virtualPath, hashIt->second);
            // Create an alias in pathToHandle for this virtual path
            pathToHandle[virtualPath] = hashIt->second;
            return hashIt->second;
        }

        // Create new mesh
        AssetHandle handle = nextHandle++;
        pathToHandle[virtualPath] = handle;
        handleToPath[handle] = virtualPath;

        // Create the mesh and set its hash
        Mesh::Builder builder;
        builder.vertices = vertices;
        builder.indices = indices;
        auto meshPtr = std::make_unique<Mesh>(device, builder);
        meshPtr->setHash(meshHash);

        meshPool[handle] = std::move(meshPtr);
        hashToHandle[meshHash] = handle;

        AT_TRACE("Created new mesh: {} (handle: {}, hash: {})", virtualPath, handle, meshHash);
        return handle;
    }

    AssetHandle AssetManager::getOrCreateTexture(const unsigned char *pixels, uint32_t width, uint32_t height, const VkFormat format, const std::string &virtualPath) {
        // Compute hash for this texture
        uint32_t bytesPerPixel = 4; // Assuming RGBA
        VkDeviceSize imageSize = static_cast<VkDeviceSize>(width) * height * bytesPerPixel;
        size_t textureHash = Sampler::computeHash(pixels, imageSize);

        // Check if we already have this asset loaded (by hash)
        auto hashIt = hashToHandle.find(textureHash);
        if (hashIt != hashToHandle.end()) {
            AT_TRACE("Texture already loaded (hash match): {} -> handle {}", virtualPath, hashIt->second);
            // Create an alias in pathToHandle for this virtual path
            pathToHandle[virtualPath] = hashIt->second;
            return hashIt->second;
        }

        // Create new texture
        AssetHandle handle = nextHandle++;
        pathToHandle[virtualPath] = handle;
        handleToPath[handle] = virtualPath;

        auto sampler = Sampler::create(device, pixels, width, height, format);
        sampler->setHash(textureHash);

        texturePool[handle] = sampler;
        hashToHandle[textureHash] = handle;

        AT_TRACE("Created new texture: {} (handle: {}, hash: {}, {}x{})",
                 virtualPath, handle, textureHash, width, height);
        return handle;
    }

    std::vector<AssetHandle> AssetManager::loadGltf(const std::string &virtualPath) {
        std::vector<AssetHandle> allAssets;

        // Check if already loaded (by path)
        auto it = pathToHandle.find(virtualPath);
        if (it != pathToHandle.end()) {
            AT_TRACE("glTF already loaded: {} (returning existing assets)", virtualPath);

            // Collect all handles for this gltf (both meshes and textures)
            for (const auto &[path, handle]: pathToHandle) {
                if (path.find(virtualPath + "#") == 0) {
                    allAssets.push_back(handle);
                }
            }

            return allAssets;
        }

        std::filesystem::path fullPath = getAssetsPath() / virtualPath;

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

        bool success = loader.LoadBinaryFromMemory(&model, &err, &warn, fileBuffer.data(), static_cast<const uint32_t>(fileBuffer.size()));

        if (!warn.empty()) AT_WARN("glTF warning: {}", warn);
        if (!success) {
            AT_ERROR("Failed to load glTF: {} - {}", fullPath.string(), err);
            return allAssets; // Return empty vector
        }

        AT_INFO("Loading glTF file: {} ({} meshes, {} images)", virtualPath, model.meshes.size(), model.images.size());

        // Thread-safe containers for results
        std::mutex handleMutex;
        std::vector<AssetHandle> imageHandles(model.images.size(), INVALID_ASSET_HANDLE);

        // Get executor service
        auto &executor = device.getExecutor();

        AT_INFO("Loading assets...");

        // ========================================================================
        // STEP 1: Load all images in parallel
        // ========================================================================
        std::vector<std::future<void> > imageFutures;
        imageFutures.reserve(model.images.size());

        for (size_t imgIdx = 0; imgIdx < model.images.size(); ++imgIdx) {
            imageFutures.push_back(executor.submit([this, &model, imgIdx, &imageHandles,
                    &handleMutex, &virtualPath]() {
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

                    // Determine if this is likely a normal map based on name
                    bool isNormalMap = false;
                    std::string imgName = image.name;
                    std::transform(imgName.begin(), imgName.end(), imgName.begin(), ::tolower);
                    if (imgName.find("normal") != std::string::npos ||
                        imgName.find("nrm") != std::string::npos ||
                        imgName.find("_n") != std::string::npos ||
                        imgName.find("norm") != std::string::npos) {
                        isNormalMap = true;
                    }

                    VkFormat format = isNormalMap ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R8G8B8A8_SRGB;

                    std::string texturePath = virtualPath + "#image" + std::to_string(imgIdx);
                    if (!image.name.empty()) {
                        texturePath = virtualPath + "#" + image.name;
                    }

                    // Thread-safe creation (upload the decoded pixels)
                    {
                        std::lock_guard<std::mutex> lock(handleMutex);
                        AssetHandle handle = getOrCreateTexture(pixelData, static_cast<uint32_t>(width), static_cast<uint32_t>(height), format, texturePath);
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
                             isNormalMap ? "normal" : "albedo");
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
            meshFutures.push_back(executor.submit([this, &model, meshIndex, &meshIndexToHandles, &imageHandles, &handleMutex, &virtualPath]() {
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
                            vert.normal = glm::vec3(nv[0], nv[1], nv[2]);
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
                            vert.tangent = glm::vec4(tv[0], tv[1], tv[2], tv[3]);
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
                    std::string meshPath = virtualPath + "#mesh" + std::to_string(meshIndex) + "_prim" + std::to_string(primIndex);

                    // Thread-safe mesh creation with hash-based deduplication
                    AssetHandle meshHandle; {
                        std::lock_guard<std::mutex> lock(handleMutex);
                        meshHandle = getOrCreateMesh(vertices, indices, meshPath);
                    }

                    primHandles.push_back(meshHandle);

                    // Map material textures (thread-safe)
                    if (prim.material >= 0 && prim.material < static_cast<int>(model.materials.size())) {
                        const tinygltf::Material &mat = model.materials[prim.material];
                        std::string matName = mat.name.empty() ? ("mat" + std::to_string(prim.material)) : mat.name;

                        std::lock_guard<std::mutex> lock(handleMutex);

                        // Base color texture (albedo)
                        if (mat.pbrMetallicRoughness.baseColorTexture.index >= 0) {
                            int texIdx = mat.pbrMetallicRoughness.baseColorTexture.index;
                            if (texIdx < static_cast<int>(model.textures.size())) {
                                int imgIdx = model.textures[texIdx].source;
                                if (imgIdx >= 0 && imgIdx < static_cast<int>(imageHandles.size())) {
                                    std::string baseColorPath = meshPath + "_baseColor";
                                    std::string matBaseColorPath = virtualPath + "#" + matName + "_baseColor";
                                    pathToHandle[baseColorPath] = imageHandles[imgIdx];
                                    pathToHandle[matBaseColorPath] = imageHandles[imgIdx];
                                }
                            }
                        }

                        // Normal map texture
                        if (mat.normalTexture.index >= 0) {
                            int texIdx = mat.normalTexture.index;
                            if (texIdx < static_cast<int>(model.textures.size())) {
                                int imgIdx = model.textures[texIdx].source;
                                if (imgIdx >= 0 && imgIdx < static_cast<int>(imageHandles.size())) {
                                    std::string normalPath = meshPath + "_normal";
                                    std::string matNormalPath = virtualPath + "#" + matName + "_normal";
                                    pathToHandle[normalPath] = imageHandles[imgIdx];
                                    pathToHandle[matNormalPath] = imageHandles[imgIdx];
                                }
                            }
                        }

                        if (mat.occlusionTexture.index >= 0) {
                            int texIdx = mat.occlusionTexture.index;
                            if (texIdx >= 0 && texIdx < static_cast<int>(model.textures.size())) {
                                int imgIdx = model.textures[texIdx].source;
                                if (imgIdx >= 0 && imgIdx < static_cast<int>(model.images.size())) {
                                    std::string ambientOcclusionPath = meshPath + "_ambientOcclusion";
                                    std::string matAmbientOcclusionPath = virtualPath + "#" + matName + "_ambientOcclusion";
                                    pathToHandle[ambientOcclusionPath] = imageHandles[imgIdx];
                                    pathToHandle[matAmbientOcclusionPath] = imageHandles[imgIdx];
                                }
                            }
                        }
                    }
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

        // ========================================================================
        // STEP 4: Map scene nodes to mesh handles
        // ========================================================================
        for (size_t nodeIndex = 0; nodeIndex < model.nodes.size(); ++nodeIndex) {
            const tinygltf::Node &node = model.nodes[nodeIndex];
            if (node.mesh >= 0 && node.mesh < static_cast<int>(meshIndexToHandles.size())) {
                const auto &handles = meshIndexToHandles[node.mesh];
                if (!handles.empty()) {
                    std::string nodePath = virtualPath + "#node" + std::to_string(nodeIndex);
                    pathToHandle[nodePath] = handles[0];
                }
            }
        }

        // Store main path pointing to first asset (for backward compatibility)
        if (!allAssets.empty()) {
            pathToHandle[virtualPath] = allAssets[0];
        }

        AT_INFO("Successfully loaded glTF: {} ({} total assets: {} meshes, {} textures)", virtualPath, allAssets.size(), allAssets.size() - imageHandles.size(), imageHandles.size());

        return allAssets;
    }

    entt::registry AssetManager::loadGltfAsScene(const std::string &virtualPath) {
        entt::registry registry;

        // First, load all assets (meshes and textures)
        std::vector<AssetHandle> assets = loadGltf(virtualPath);

        if (assets.empty()) {
            AT_ERROR("Failed to load any assets from glTF: {}", virtualPath);
            return registry;
        }

        // Now load the GLTF model again to process the scene hierarchy
        std::filesystem::path fullPath = getAssetsPath() / virtualPath;

        tinygltf::Model model;
        tinygltf::TinyGLTF loader;
        std::string err, warn;

        bool success = false;
        if (fullPath.extension() == ".glb") {
            success = loader.LoadBinaryFromFile(&model, &err, &warn, fullPath.string());
        } else {
            success = loader.LoadASCIIFromFile(&model, &err, &warn, fullPath.string());
        }

        if (!success) {
            AT_ERROR("Failed to load glTF scene: {}", err);
            return registry;
        }

        std::vector<entt::entity> entities;

        // Process each scene (usually there's just one)
        if (!model.scenes.empty()) {
            const tinygltf::Scene &gltfScene = model.scenes[model.defaultScene >= 0 ? model.defaultScene : 0];

            // Process each root node
            for (int nodeIdx: gltfScene.nodes) {
                glm::mat4 parentTransform = glm::mat4(1.0f);
                processNode(registry, model, nodeIdx, parentTransform, virtualPath, entities);
            }
        }

        AT_INFO("Created {} entities from glTF scene: {}", entities.size(), virtualPath);

        return registry;
    }

    void AssetManager::processNode(
        entt::registry &registry,
        const tinygltf::Model &model,
        int32_t nodeIdx,
        const glm::mat4 &parentTransform,
        const std::string &virtualPath,
        std::vector<entt::entity> &outEntities) {
        const tinygltf::Node &node = model.nodes[nodeIdx];

        glm::mat4 localTransform = getNodeTransform(node);
        glm::mat4 worldTransform = parentTransform * localTransform;

        if (node.mesh >= 0 && node.mesh < static_cast<int>(model.meshes.size())) {
            const tinygltf::Mesh &mesh = model.meshes[node.mesh];

            for (size_t primIdx = 0; primIdx < mesh.primitives.size(); ++primIdx) {
                auto entity = registry.create();

                glm::vec3 translation, scale, skew;
                glm::quat rotation;
                glm::vec4 perspective;
                glm::decompose(worldTransform, scale, rotation, translation, skew, perspective);

                auto &transform = registry.emplace<TransformComponent>(entity);
                transform.translation = translation;
                transform.rotation = glm::eulerAngles(rotation);
                transform.scale = scale;

                std::string meshPath = virtualPath + "#mesh" + std::to_string(node.mesh) +
                                       "_prim" + std::to_string(primIdx);
                auto it = pathToHandle.find(meshPath);
                if (it != pathToHandle.end()) {
                    auto &modelComp = registry.emplace<ModelComponent>(entity);
                    modelComp.meshHandle = it->second;

                    auto &material = registry.emplace<MaterialComponent>(entity);

                    const tinygltf::Primitive &prim = mesh.primitives[primIdx];
                    if (prim.material >= 0 && prim.material < static_cast<int>(model.materials.size())) {
                        const tinygltf::Material &mat = model.materials[prim.material];
                        const auto &pbr = mat.pbrMetallicRoughness;

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

                                    auto texIt = pathToHandle.find(imagePath);
                                    if (texIt != pathToHandle.end()) {
                                        material.albedoTexture = texIt->second;
                                    }
                                }
                            }
                        }

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

                                    auto normIt = pathToHandle.find(imagePath);
                                    if (normIt != pathToHandle.end()) {
                                        material.normalMap = normIt->second;
                                    }
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

            glm::vec3 translation, scale, skew;
            glm::quat rotation;
            glm::vec4 perspective;
            glm::decompose(worldTransform, scale, rotation, translation, skew, perspective);

            auto &transform = registry.emplace<TransformComponent>(entity);
            transform.translation = translation * glm::vec3(1.0f, -1.0f, 1.0f);
            transform.rotation = glm::eulerAngles(rotation);
            transform.scale = scale;

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

            constexpr glm::vec3 defaultDir = glm::vec3{0.0f, 0.0f, -1.0f};
            light.direction = glm::normalize(rotation * defaultDir);

            outEntities.push_back(entity);

            AT_TRACE("Created light entity: type={}, color=({}, {}, {}), intensity={}, range={}",
                     gltfLight.type, light.color.r, light.color.g, light.color.b,
                     light.intensity, light.range);
        }

        for (int childIdx: node.children) {
            processNode(registry, model, childIdx, worldTransform, virtualPath, outEntities);
        }
    }

    glm::mat4 AssetManager::getNodeTransform(const tinygltf::Node &node) {
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
                glm::quat rotation(
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
                                     node.scale[2]
                                 ));
            }
        }

        return mat;
    }
}
