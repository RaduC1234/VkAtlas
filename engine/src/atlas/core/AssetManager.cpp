#include "AssetManager.hpp"

#if defined(__ANDROID__)
#include <android/asset_manager.h>
#include <stdexcept>

AAssetManager* Atlas::AssetManager::sharedAssetManager = nullptr;

#else
#include <fstream>
#include <stdexcept>
#endif

namespace Atlas {

#if defined(__ANDROID__)
    void AssetManager::init(AAssetManager* mgr) {
        if (!mgr) {
            throw std::runtime_error("AssetManager::init: asset manager is null!");
        }
        sharedAssetManager = mgr;
    }
#else
    void AssetManager::init() {
        // No-op on non-Android
    }
#endif

    std::unique_ptr<AssetManager> AssetManager::create() {
        auto instance = std::make_unique<AssetManager>();

#if defined(__ANDROID__)
        if (!sharedAssetManager) {
            throw std::runtime_error("AssetManager::create: asset manager not initialized!");
        }
        instance->assetManager = sharedAssetManager;
#endif

        return instance;
    }

    std::vector<char> AssetManager::load(const std::string& path) {
#if defined(__ANDROID__)
        if (!assetManager) {
            throw std::runtime_error("AssetManager::load: no asset manager bound to this instance!");
        }

        AAsset* asset = AAssetManager_open(assetManager, path.c_str(), AASSET_MODE_STREAMING);
        if (!asset) {
            throw std::runtime_error("AssetManager: Failed to open asset: " + path);
        }

        off_t length = AAsset_getLength(asset);
        std::vector<char> buffer(length);
        AAsset_read(asset, buffer.data(), length);
        AAsset_close(asset);
        return buffer;

#else
        std::ifstream file(path, std::ios::ate | std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("AssetManager: Failed to open file: " + path);
        }

        size_t size = static_cast<size_t>(file.tellg());
        std::vector<char> buffer(size);
        file.seekg(0);
        file.read(buffer.data(), size);
        file.close();
        return buffer;
#endif
    }

}
