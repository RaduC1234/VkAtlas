#include "AssetManager.hpp"
#include "core/Log.hpp"

#if defined(__ANDROID__)
#include "android/AndroidAssetManager.hpp"
#elif defined(_WIN32)
#include "desktop/DesktopAssetManager.hpp"
#endif

namespace Atlas {

    std::shared_ptr<AssetManager> AssetManager::instance = nullptr;

    std::shared_ptr<AssetManager> AssetManager::create(void* nativeApp) {
        if (!instance) {
#if defined(__ANDROID__)
            instance = std::make_shared<AndroidAssetManager>(nativeApp);
            AT_INFO("Created Android AssetManager instance");
#elif defined(_WIN32)
            instance = std::make_shared<DesktopAssetManager>(nativeApp);
            AT_INFO("Created Desktop AssetManager instance");
#else
            LOG_ERROR("Unsupported platform for AssetManager");
            return nullptr;
#endif
        }
        return instance;
    }

    AssetManager& AssetManager::get() {
        if (!instance) {
            AT_ERROR("AssetManager not initialized! Call AssetManager::create() first.");
            // Create a default instance to avoid crashes
            create();
        }
        return *instance;
    }

}
