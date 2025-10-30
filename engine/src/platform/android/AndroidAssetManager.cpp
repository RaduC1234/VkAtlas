#include "AndroidAssetManager.hpp"

#if defined(__ANDROID__)
#include "core/AT.hpp"
#include <game-activity/native_app_glue/android_native_app_glue.h>

namespace Atlas {

    AndroidAssetManager::AndroidAssetManager(void* nativeApp) {
        if (!nativeApp) {
            AT_ERROR("Native app handle is null!");
            androidAssetManager = nullptr;
            return;
        }

        auto* app = reinterpret_cast<android_app*>(nativeApp);
        androidAssetManager = app->activity->assetManager;
        AT_INFO("Android AssetManager initialized");
    }

    std::vector<char> AndroidAssetManager::loadTextFile(const std::string& resource) {
        if (!androidAssetManager) {
            AT_ERROR("Android AssetManager is not initialized");
            return {};
        }

        AAsset* asset = AAssetManager_open(androidAssetManager, resource.c_str(), AASSET_MODE_STREAMING);
        if (!asset) {
            AT_ERROR("Failed to open asset: {}", resource);
            return {};
        }

        off_t size = AAsset_getLength(asset);
        std::vector<char> buffer(size);

        int bytesRead = AAsset_read(asset, buffer.data(), size);
        AAsset_close(asset);

        if (bytesRead != size) {
            AT_ERROR("Failed to read complete asset: {}", resource);
            return {};
        }

        AT_TRACE("Loaded asset: {} ({} bytes)", resource, size);
        return buffer;
    }

}

#endif // __ANDROID__
