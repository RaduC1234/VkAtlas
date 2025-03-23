#pragma once

#include <string>
#include <vector>
#include <memory>

#if defined(__ANDROID__)
#include <android/asset_manager.h>
#endif

namespace Atlas {

    class AssetManager {
    public:
        AssetManager() = default;

#if defined(__ANDROID__)
        static void init(AAssetManager* mgr);
#else
        static void init();
#endif
        std::vector<char> load(const std::string& path);

        static std::unique_ptr<AssetManager> create();
    private:
#if defined(__ANDROID__)
        static AAssetManager* sharedAssetManager;
        AAssetManager* assetManager = nullptr;
#endif
    };

}
