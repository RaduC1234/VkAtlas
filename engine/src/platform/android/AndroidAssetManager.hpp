#pragma once

#include "asset/AssetManager.hpp"

#ifdef ATLAS_PLATFORM_ANDROID
#include <android/asset_manager.h>

namespace Atlas {

    /**
     * @brief Android platform implementation of AssetManager
     *
     * Loads assets from the Android APK using the AAssetManager API
     */
    class AndroidAssetManager : public AssetManager {
    public:
        explicit AndroidAssetManager(Device& device, void* nativeApp);
        ~AndroidAssetManager() override = default;

        /**
         * @brief Get the Android assets path (returns empty path as Android uses AssetManager API)
         * @return Empty filesystem path
         */
        [[nodiscard]] std::filesystem::path rootPath() const override;

    private:
        AAssetManager* androidAssetManager;
    };

}

#endif // __ANDROID__
