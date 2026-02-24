#include "DesktopAssetManager.hpp"
#include "core/Log.hpp"

namespace Atlas {
    DesktopAssetManager::DesktopAssetManager(Device& device, void* nativeApp) : AssetManager(device, nativeApp) {
        assetsPath = std::filesystem::current_path() / "assets";
        AT_INFO("Desktop AssetManager initialized. Assets path: {}", assetsPath.string());
    }

    std::filesystem::path DesktopAssetManager::getAssetsPath() const {
        return assetsPath;
    }

}
