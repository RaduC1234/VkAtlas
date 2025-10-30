#pragma once

#include "core/AssetManager.hpp"

#if defined(__ANDROID__)
#include <android/asset_manager.h>

namespace Atlas {

    /**
     * @brief Android platform implementation of AssetManager
     *
     * Loads assets from the Android APK using the AAssetManager API
     */
    class AndroidAssetManager : public AssetManager {
    public:
        explicit AndroidAssetManager(void* nativeApp);
        ~AndroidAssetManager() override = default;

        /**
         * @brief Load a text file from Android assets
         * @param resource Path to the resource relative to assets directory
         * @return Vector of characters containing the file data
         */
        std::vector<char> loadTextFile(const std::string& resource) override;

    private:
        AAssetManager* androidAssetManager;
    };

}

#endif // __ANDROID__
