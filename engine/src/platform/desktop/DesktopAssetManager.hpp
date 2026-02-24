#pragma once

#ifdef ATLAS_PLATFORM_DESKTOP

#include "asset/AssetManager.hpp"
#include <filesystem>

namespace Atlas {

    /**
     * @brief Desktop platform implementation of AssetManager
     *
     * Loads assets from the filesystem using standard C++ file I/O
     */
    class DesktopAssetManager : public AssetManager {
    public:
        explicit DesktopAssetManager(Device& device, void* nativeApp = nullptr);
        ~DesktopAssetManager() override = default;

        /**
         * @brief Get the desktop assets path
         * @return Path to the assets directory
         */
        [[nodiscard]] std::filesystem::path getAssetsPath() const override;

    private:
        std::filesystem::path assetsPath;
    };

}

#endif