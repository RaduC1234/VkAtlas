#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include <memory>
#include <tiny_gltf.h>
#include <unordered_map>
#include <entt/entity/registry.hpp>

#include "renderer/Cubemap.hpp"
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

#pragma region Class methods
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

#pragma endregion

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
         * @brief Load a skybox/cubemap asset and return its handle
         *
         * The virtualPath should point to either a cubemap descriptor or a folder
         * containing the 6 faces. The implementation is platform/engine-specific
         * and is expected to create a Cubemap asset and return its handle.
         *
         * @param virtualPath Virtual path to the skybox resource
         * @return AssetHandle for the created/loaded cubemap, or INVALID_ASSET_HANDLE on failure
         */
        AssetHandle loadSkybox(const std::string &virtualPath);

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
         * @brief Create a simple procedural plane mesh and return its handle
         *
         * Generates a single quad (two triangles) centered at the origin, aligned
         * to the XZ plane (Y up). The details (normals, UVs) are created by the
         * implementation.
         *
         * @param width Plane width along X
         * @param height Plane height along Z
         * @return AssetHandle for the created plane mesh
         */
        AssetHandle createPlane(float width, float height);

        /**
         * @brief Load a glTF file (GLTF/GLB) and create mesh and texture assets.
         * @param virtualPath Path to the glTF file inside assets
         * @return AssetHandle of the first mesh created, or INVALID_ASSET_HANDLE on failure
         */
        std::vector<AssetHandle> loadGltf(const std::string &virtualPath);

        /**
         * @brief Load a glTF file and return an entt::registry representing the scene
         *
         * This function will parse the glTF scene graph, create entities and
         * components in an EnTT registry, and return the populated registry. The
         * returned registry can be used by higher-level scene systems to spawn
         * or integrate the loaded scene.
         *
         * @param path Path to the glTF file
         * @return entt::registry containing entities created from the glTF scene
         */
        entt::registry loadGltfAsScene(const std::string &path);

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
         * @brief Get cubemap/cube texture by handle
         * @param handle Asset handle
         * @return Shared pointer to Cubemap, or nullptr if not found
         */
        std::shared_ptr<Cubemap> getCubemap(AssetHandle handle);

        /**
         * @brief Get the virtual path for a given handle (for debugging)
         * @param handle Asset handle
         * @return Virtual path string
         */
        [[nodiscard]] std::string getPath(AssetHandle handle) const;

        /**
         * @brief Free a texture asset and release GPU memory
         *
         * Removes the texture from internal pools, clears all lookup maps (pathToHandle,
         * handleToPath, hashToHandle), and releases the shared_ptr. This allows the
         * texture's destructor to free GPU memory (VkImage, VkImageView, VkDeviceMemory).
         *
         * @param handle Asset handle to free
         * @return true if asset was freed, false if handle was invalid or not found
         *
         * @note This is safe to call even if other systems hold references to the texture.
         *       GPU memory will only be freed when the last shared_ptr is released.
         *
         * @warning Do not call this for assets still referenced by active entities in the scene.
         *          Ensure all MaterialComponent references are updated before freeing.
         *
         * Example usage:
         * @code
         * AssetHandle texHandle = AssetManager::get().loadTexture("textures/old_texture.png");
         * // ... use texture ...
         * if (AssetManager::get().freeTexture(texHandle)) {
         *     AT_INFO("Texture freed successfully");
         * }
         * @endcode
         */
        bool freeTexture(AssetHandle handle);

        /**
         * @brief Free a mesh asset and release GPU memory
         *
         * Removes the mesh from internal pools, clears all lookup maps (pathToHandle,
         * handleToPath, hashToHandle), and releases the shared_ptr. This allows the
         * mesh's destructor to free GPU memory (vertex/index buffers, VkDeviceMemory).
         *
         * @param handle Asset handle to free
         * @return true if asset was freed, false if handle was invalid or not found
         *
         * @note This is safe to call even if other systems hold references to the mesh.
         *       GPU memory will only be freed when the last shared_ptr is released.
         *
         * @warning Do not call this for assets still referenced by active entities in the scene.
         *          Ensure all ModelComponent references are updated before freeing.
         *
         * Example usage:
         * @code
         * AssetHandle meshHandle = AssetManager::get().loadMesh("models/old_model.obj");
         * // ... use mesh ...
         * if (AssetManager::get().freeMesh(meshHandle)) {
         *     AT_INFO("Mesh freed successfully");
         * }
         * @endcode
         */
        bool freeMesh(AssetHandle handle);

        /**
         * @brief Free a cubemap asset and release GPU memory
         *
         * @param handle Asset handle to free
         * @return true if asset was freed, false if handle was invalid or not found
         *
         * @note Currently not implemented - returns false with warning log
         */
        bool freeCubemap(AssetHandle handle);

        /**
         * @brief Free any asset (texture, mesh, or cubemap) by handle
         *
         * Automatically detects the asset type and calls the appropriate free function.
         * Useful when you don't know the asset type at runtime.
         *
         * @param handle Asset handle to free
         * @return true if asset was freed, false if handle was invalid or not found
         *
         * Example usage:
         * @code
         * // Free any asset without knowing its type
         * AssetHandle someHandle = getSomeAssetHandle();
         * if (AssetManager::get().freeAsset(someHandle)) {
         *     AT_INFO("Asset freed successfully");
         * } else {
         *     AT_WARN("Failed to free asset or handle not found");
         * }
         * @endcode
         */
        bool freeAsset(AssetHandle handle);

#pragma region non-coherent functions
        /**
         * @brief Load a text file from assets
         * @param path Path to the resource relative to assets directory
         * @return Vector of characters containing the file data
         */
        static std::vector<char> loadFileAsU8(const std::string &path);
        static void saveFileAsU8(const std::vector<char> &data, const std::string &path);

        static std::string loadFileAsString(const std::string &path);
        static void saveFileAsString(const std::string &data, const std::string &path);
#pragma endregion
        /**
         * @brief Get the platform-specific assets path
         * @return Path to the assets directory
         */
        [[nodiscard]] virtual std::filesystem::path getAssetsPath() const = 0;

        /**
         * @brief Load a scene serialized to JSON and return an EnTT registry
         *
         * This will parse a JSON scene file produced by the editor/runtime and
         * populate an entt::registry with entities and their components.
         *
         * @param filePath Path to the JSON scene file
         * @return entt::registry populated from the JSON file
         */
        entt::registry loadSceneFromJson(const std::string &filePath) const {
            // Default header-provided implementation: return an empty registry.
            // The full JSON scene loader can be implemented in the .cpp if the
            // project needs a non-header definition or more complex parsing.
            // TODO: implement JSON parsing that populates and returns a registry.
            (void)filePath; // silence unused parameter warning
            return entt::registry{};
        }

    protected:
        /**
         * @brief Protected constructor for derived classes
         * @param device Vulkan device reference
         * @param nativeApp Platform-specific application handle
         */
        explicit AssetManager(Device &device, void *nativeApp = nullptr);

        /**
         * @brief Create or return an existing mesh from raw vertex/index data
         *
         * This helper checks internal hashes/pools and either returns an existing
         * handle for identical geometry or creates a new Mesh asset, stores it in
         * the mesh pool and returns the new handle.
         *
         * @param vertices Vertex list for the mesh
         * @param indices Index list for the mesh
         * @param virtualPath Optional virtual path used for debugging/lookup
         * @return AssetHandle referring to the created or existing mesh
         */
        AssetHandle getOrCreateMesh(const std::vector<Mesh::Vertex> &vertices, const std::vector<uint32_t> &indices, const std::string &virtualPath);

        /**
         * @brief Create or return an existing texture from raw pixel data
         *
         * Checks internal pools and hashes to deduplicate identical textures. If
         * none exists it creates a new Sampler asset and stores it in the texture pool.
         *
         * @param pixels Pointer to RGBA/encoded pixel data
         * @param width Texture width
         * @param height Texture height
         * @param format Vulkan pixel format of the provided data
         * @param virtualPath Optional virtual path used for debugging/lookup
         * @return AssetHandle referring to the created or existing texture
         */
        AssetHandle getOrCreateTexture(const unsigned char *pixels, uint32_t width, uint32_t height, VkFormat format, const std::string &virtualPath);

        /**
         * @brief Compute the local transform matrix for a glTF node
         *
         * Extracts translation, rotation and scale from the tinygltf::Node and
         * returns a glm::mat4 representing the node's transform in local space.
         *
         * @param node Reference to a tinygltf::Node
         * @return glm::mat4 transform matrix
         */
        glm::mat4 getNodeTransform(const tinygltf::Node &node);

        /**
         * @brief Recursively process a glTF node and populate an EnTT registry
         *
         * This function traverses the glTF node hierarchy, creates entities for
         * nodes that contain mesh/primitive data, attaches necessary components
         * (transform, mesh handle, material references), and appends the created
         * entities to outEntities.
         *
         * @param registry EnTT registry to populate
         * @param model tinygltf model owning the nodes
         * @param nodeIdx Index of the node to process
         * @param parentTransform Transform matrix of the parent node
         * @param virtualPath Virtual path of the source glTF (for debugging)
         * @param outEntities Vector to receive created entities
         */
        void processNode(entt::registry &registry, const tinygltf::Model &model, int32_t nodeIdx, const glm::mat4 &parentTransform, const std::string &virtualPath, std::vector<entt::entity> &outEntities);

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
