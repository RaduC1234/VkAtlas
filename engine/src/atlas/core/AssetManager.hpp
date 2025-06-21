#pragma once

#include <string>
#include <vector>
#include <memory>

#if defined(__ANDROID__)
#include <android/asset_manager.h>
#elif defined(_WIN32)
#include <filesystem>
#endif

namespace Atlas {

    class AssetManager {
    public:
        static void init(void* nativeApp);
        static AssetManager get();

        std::vector<char> load(const std::string& resource);

    protected:
        static std::shared_ptr<AssetManager> assetManager;

#if defined(__ANDROID__)
        static AAssetManager* androidAssetManager;
#elif defined(_WIN32)
        static std::filesystem::path assetsPath;
#endif
    };

}
