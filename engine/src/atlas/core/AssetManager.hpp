#pragma once

#include <string>
#include <vector>
#include <memory>

namespace Atlas {

    /**
     * @brief Abstract base class for platform-agnostic asset management
     *
     * This class provides a factory method to create platform-specific
     * asset manager implementations.
     */
    class AssetManager {
    public:
        virtual ~AssetManager() = default;

        /**
         * @brief Factory method to create platform-specific AssetManager instance
         * @param nativeApp Platform-specific application handle (android_app* on Android, nullptr on desktop)
         * @return Shared pointer to the created AssetManager instance
         */
        static std::shared_ptr<AssetManager> create(void* nativeApp = nullptr);

        /**
         * @brief Get the singleton instance of AssetManager
         * @return Reference to the AssetManager instance
         */
        static AssetManager& get();

        /**
         * @brief Load a text file from assets
         * @param resource Path to the resource relative to assets directory
         * @return Vector of characters containing the file data
         */
        virtual std::vector<char> loadTextFile(const std::string& resource) = 0;

    protected:
        static std::shared_ptr<AssetManager> instance;
    };

}
