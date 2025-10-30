#pragma once

#include "core/AssetManager.hpp"
#include <filesystem>

namespace Atlas {

    /**
     * @brief Desktop platform implementation of AssetManager
     *
     * Loads assets from the filesystem using standard C++ file I/O
     */
    class DesktopAssetManager : public AssetManager {
    public:
        explicit DesktopAssetManager(void* nativeApp = nullptr);
        ~DesktopAssetManager() override = default;

        /**
         * @brief Load a text file from the desktop filesystem
         * @param resource Path to the resource relative to assets directory
         * @return Vector of characters containing the file data
         */
        std::vector<char> loadTextFile(const std::string& resource) override;

    private:
        std::filesystem::path assetsPath;
    };

}
