#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <entt/entity/registry.hpp>

#include "renderer/Mesh.hpp"
#include "renderer/Sampler.hpp"

namespace Atlas {
    /**
     * @brief Abstract base class for platform-agnostic asset management
     *
     * This class provides a singleton pattern for platform-specific
     * asset manager implementations.
     */

    using AssetHandle = int32_t;
    constexpr AssetHandle INVALID_ASSET_HANDLE = -1;

    class AssetManager {
    public:
        virtual ~AssetManager() = default;

        /**
         * @brief Create the singleton instance of AssetManager
         * @param device Vulkan device reference
         * @param nativeApp Platform-specific application handle (android_app* on Android, nullptr on desktop)
         * @return Reference to the created AssetManager instance
         */
        static AssetManager &create(Device &device, void *nativeApp = nullptr);

        /**
         * @brief Get the singleton instance of AssetManager
         * @return Reference to the AssetManager instance
         */
        static AssetManager &get();

        /**
         * @brief Reset and destroy the singleton instance
         */
        static void destroy();

        /**
         * @brief Load a texture and return its handle
         * @param virtualPath Virtual path to the texture (e.g., "textures/wood.png")
         * @return AssetHandle for the loaded texture
         */
        AssetHandle loadTexture(const std::string &virtualPath);

        /**
         * @brief Load a mesh and return its handle
         * @param virtualPath Virtual path to the mesh file (e.g., "models/sphere.obj")
         * @return AssetHandle for the loaded mesh
         */
        AssetHandle loadMesh(const std::string &virtualPath);

        /**
         * @brief Create a procedural sphere mesh and return its handle
         * @param radius Sphere radius
         * @param segments Number of horizontal segments
         * @param rings Number of vertical rings
         * @return AssetHandle for the created sphere mesh
         */
        AssetHandle createSphere(float radius = 1.0f, uint32_t segments = 32, uint32_t rings = 16);

        /**
         * @brief Create a procedural cube mesh and return its handle
         * @param size Cube size
         * @return AssetHandle for the created cube mesh
         */
        AssetHandle createCube(float size = 1.0f);

        /**
        *
        * @param width
        * @param height
        * @return
        */
        AssetHandle createPlane(float width, float height);

        /**
         * @brief Load a glTF file (GLTF/GLB) and create mesh and texture assets.
         * @param virtualPath Path to the glTF file inside assets
         * @return AssetHandle of the first mesh created, or INVALID_ASSET_HANDLE on failure
         */
        std::vector<AssetHandle> loadGltf(const std::string &virtualPath);

        /**
         * @brief Create a default white 1x1 texture and return its handle
         * @return AssetHandle for the created white texture
         */
        AssetHandle createDefaultWhiteTexture();

        /**
         * @brief Get texture by handle
         * @param handle Asset handle
         * @return Shared pointer to Sampler, or nullptr if not found
         */
        std::shared_ptr<Sampler> getTexture(AssetHandle handle);

        /**
         * @brief Get mesh by handle
         * @param handle Asset handle
         * @return Shared pointer to Mesh, or nullptr if not found
         */
        std::shared_ptr<Mesh> getMesh(AssetHandle handle);

        /**
         * @brief Get the virtual path for a given handle (for debugging)
         * @param handle Asset handle
         * @return Virtual path string
         */
        [[nodiscard]] std::string getPath(AssetHandle handle) const;

#pragma region non-coherent functions
        /**
         * @brief Load a text file from assets
         * @param path Path to the resource relative to assets directory
         * @return Vector of characters containing the file data
         */
        static std::vector<char> loadTextFileAsU8(const std::string &path);
        static void saveFileAsU8(const std::vector<char> &data, const std::string &path);

        static std::string loadTextFileAsString(const std::string &path);
        static void saveFileAsString(const std::string &data, const std::string &path);
#pragma endregion
        /**
         * @brief Get the platform-specific assets path
         * @return Path to the assets directory
         */
        [[nodiscard]] virtual std::filesystem::path getAssetsPath() const = 0;

        entt::registry loadSceneFromJson(const std::string &filePath) const;

    protected:
        /**
         * @brief Protected constructor for derived classes
         * @param device Vulkan device reference
         * @param nativeApp Platform-specific application handle
         */
        explicit AssetManager(Device &device, void *nativeApp = nullptr);

        AssetHandle getOrCreateMesh(const std::vector<Mesh::Vertex> &vertices, const std::vector<uint32_t> &indices, const std::string &virtualPath);
        AssetHandle getOrCreateTexture(const unsigned char *pixels, uint32_t width, uint32_t height, VkFormat format, const std::string &virtualPath);

        Device &device;
        void *nativeApp;

        static std::shared_ptr<AssetManager> instance;

        // Handle generation
        AssetHandle nextHandle = 1; // 0 and negative values are invalid

        // Bidirectional lookup
        std::unordered_map<std::string, AssetHandle> pathToHandle;
        std::unordered_map<AssetHandle, std::string> handleToPath;

        std::unordered_map<size_t, AssetHandle> hashToHandle;

        // Resource pools
        std::unordered_map<AssetHandle, std::shared_ptr<Sampler> > texturePool;
        std::unordered_map<AssetHandle, std::shared_ptr<Mesh> > meshPool;
    };
}
