#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>

#include "renderer/Texture.hpp"

namespace Atlas {
    /**
     * @brief Abstract base class for platform-agnostic asset management
     *
     * This class provides a singleton pattern for platform-specific
     * asset manager implementations.
     */
    class AssetManager {
    public:
        virtual ~AssetManager() = default;

        /**
         * @brief Initialize the singleton instance of AssetManager
         * @param nativeApp Platform-specific application handle (android_app* on Android, nullptr on desktop)
         */
        static void init(void *nativeApp = nullptr);

        /**
         * @brief Get the singleton instance of AssetManager
         * @return Reference to the AssetManager instance
         */
        static AssetManager &get();

        /**
         * @brief Reset and recreate the singleton instance using the stored nativeApp from init()
         */
        static void reset();
#pragma region coherent functions

#pragma endregion

#pragma region non-coherent functions
        /**
         * @brief Load a text file from assets
         * @param resource Path to the resource relative to assets directory
         * @return Vector of characters containing the file data
         */
        static std::vector<char> loadTextFile(const std::string &resource);
#pragma endregion
        /**
         * @brief Get the platform-specific assets path
         * @return Path to the assets directory
         */
        [[nodiscard]] virtual std::filesystem::path getAssetsPath() const = 0;

    protected:
        static std::shared_ptr<AssetManager> instance;
        static void *storedNativeApp;

        static std::unordered_map<std::string, std::shared_ptr<Texture>> loadedAssets;

    private:
        /**
         * @brief Factory method to create platform-specific AssetManager instance
         * @param nativeApp Platform-specific application handle (android_app* on Android, nullptr on desktop)
         * @return Shared pointer to the created AssetManager instance
         */
        static std::shared_ptr<AssetManager> create(void *nativeApp);
    };
}
