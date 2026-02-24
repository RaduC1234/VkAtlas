#include "AndroidAssetManager.hpp"

#ifdef ATLAS_PLATFORM_ANDROID
#include "core/Log.hpp"
#include <game-activity/native_app_glue/android_native_app_glue.h>

namespace Atlas {

    AndroidAssetManager::AndroidAssetManager(Device& device, void* nativeApp)
        : AssetManager(device, nativeApp) {
        if (!nativeApp) {
            AT_ERROR("Native app handle is null!");
            androidAssetManager = nullptr;
            return;
        }

        auto* app = reinterpret_cast<android_app*>(nativeApp);
        androidAssetManager = app->activity->assetManager;
        AT_INFO("Android AssetManager initialized");
    }

    std::filesystem::path AndroidAssetManager::getAssetsPath() const {
        // Android uses AssetManager API, not filesystem paths
        // Return empty path as assets are accessed directly from APK
        return std::filesystem::path();
    }

}

#endif // __ANDROID__
