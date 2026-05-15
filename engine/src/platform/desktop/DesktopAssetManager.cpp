#ifdef ATLAS_PLATFORM_DESKTOP

#include "DesktopAssetManager.hpp"
#include "core/Log.hpp"

namespace Atlas {
    DesktopAssetManager::DesktopAssetManager(Device& device, void* nativeApp) : AssetManager(device, nativeApp) {
        assetsPath = std::filesystem::current_path() / "assets";
        AT_INFO("Desktop AssetManager initialized. Assets path: {}", assetsPath.string());
    }

    std::filesystem::path DesktopAssetManager::rootPath() const {
        return assetsPath;
    }

    void DesktopAssetManager::setRootPath(const std::filesystem::path &path) {
        assetsPath = path;
        AT_INFO("Desktop AssetManager root changed to: {}", assetsPath.string());
    }

}

#endif
