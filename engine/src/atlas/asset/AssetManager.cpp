#include "AssetManager.hpp"

#include <fstream>
#include "core/Log.hpp"

#define GLM_ENABLE_EXPERIMENTAL
#include <stb_image.h>

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/hash.hpp>

#include "accessors/GLTFAccessor.hpp"
#include "accessors/OBJAcessor.hpp"

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
        registerLoader<GLTFAccessor>(device.executor());
        registerLoader<OBJAccessor>(device.executor());
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

        std::filesystem::path fullPath = rootPath() / virtualPath;
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
        } else if (ext == ".bin") {
            std::ifstream file(fullPath, std::ios::binary | std::ios::ate);
            if (!file) {
                AT_ERROR("Failed tp open {}", fullPath.generic_string())
                return INVALID_ASSET_HANDLE;
            }

            auto size = file.tellg();
            file.seekg(0, std::ios::beg);

            int texels = size / (4 * sizeof(float)); // RGBA32F
            width = 64;
            height = texels / 64;
            channels = 4;

            pixels = malloc(size);
            file.read(static_cast<char*>(pixels), size);
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

    AssetHandle AssetManager::loadCubemap(const std::string &virtualPath) {
        auto it = pathToHandle.find(virtualPath);
        if (it != pathToHandle.end()) {
            AT_TRACE("Cubemap already loaded: {} (handle: {})", virtualPath, it->second);
            return it->second;
        }
        AssetHandle handle = nextHandle++;
        pathToHandle[virtualPath] = handle;
        handleToPath[handle] = virtualPath;

        std::filesystem::path fullPath = rootPath() / virtualPath;

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
                (rootPath() / right).string(),
                (rootPath() / left).string(),
                (rootPath() / top).string(),
                (rootPath() / bottom).string(),
                (rootPath() / front).string(),
                (rootPath() / back).string()
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

    AssetHandle AssetManager::getHandle(const std::string &virtualPath) const {
        auto it = pathToHandle.find(virtualPath);
        return it != pathToHandle.end() ? it->second : INVALID_ASSET_HANDLE;
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
        std::filesystem::path filePath = get().rootPath() / path;

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
            std::filesystem::path filePath = get().rootPath() / path;

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

    std::vector<entt::entity> AssetManager::importAsset(const std::string& virtualPath, entt::registry& registry, entt::entity parentEntity)
    {
        std::string ext = std::filesystem::path(virtualPath).extension().string();
        std::ranges::transform(ext, ext.begin(), ::tolower);

        auto it = accessors.find(ext);
        if (it == accessors.end()) {
            AT_ERROR("No loader registered for extension: {}", ext);
            return {};
        }

        return it->second->importAsset(virtualPath, registry, parentEntity);
    }
}