#include "AssetManager.hpp"

#if defined(__ANDROID__)
#include <android/asset_manager.h>
#include <game-activity/native_app_glue/android_native_app_glue.h>

#elif defined(_WIN32)
#include <fstream>
#include <filesystem>
#endif

namespace Atlas {

    std::shared_ptr<AssetManager> AssetManager::assetManager = nullptr;

    void AssetManager::init(void* nativeApp) {
        if (!assetManager) {
            assetManager = std::make_shared<AssetManager>();

#if defined(__ANDROID__)
            auto* app = reinterpret_cast<android_app*>(nativeApp);
            assetManager->androidAssetManager = app->activity->assetManager;

#elif defined(_WIN32)
            assetManager->assetsPath = std::filesystem::current_path() / "assets";
#endif
        }
    }

    AssetManager AssetManager::get() {
        return *assetManager;
    }

    std::vector<char> AssetManager::load(const std::string& resource) {
#if defined(__ANDROID__)
        if (!androidAssetManager) return {};

        AAsset* asset = AAssetManager_open(androidAssetManager, resource.c_str(), AASSET_MODE_STREAMING);
        if (!asset) return {};

        off_t size = AAsset_getLength(asset);
        std::vector<char> buffer(size);
        AAsset_read(asset, buffer.data(), size);
        AAsset_close(asset);

        return buffer;

#elif defined(_WIN32)
        std::filesystem::path filePath = assetsPath / resource;
        std::ifstream file(filePath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) return {};

        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);

        std::vector<char> buffer(size);
        file.read(buffer.data(), size);
        file.close();

        return buffer;

#else
        return {};
#endif
    }

#if defined(__ANDROID__)
    // Android-specific member
    AAssetManager* AssetManager::androidAssetManager = nullptr;
#elif defined(_WIN32)
    std::filesystem::path AssetManager::assetsPath;
#endif

} // namespace Atlas
