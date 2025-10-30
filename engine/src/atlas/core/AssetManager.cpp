#include "AssetManager.hpp"

#include "core/Log.hpp"
#include <fstream>

#if defined(__ANDROID__)
#include "android/AndroidAssetManager.hpp"
#elif defined(_WIN32)
#include "desktop/DesktopAssetManager.hpp"
#endif

namespace Atlas {

    std::shared_ptr<AssetManager> AssetManager::instance = nullptr;
    void* AssetManager::storedNativeApp = nullptr;

    std::shared_ptr<AssetManager> AssetManager::create(void* nativeApp) {
#if defined(__ANDROID__)
        instance = std::make_shared<AndroidAssetManager>(nativeApp);
        AT_INFO("Created Android AssetManager instance");
#elif defined(_WIN32)
        instance = std::make_shared<DesktopAssetManager>(nativeApp);
        AT_INFO("Created Desktop AssetManager instance");
#else
        AT_ERROR("Unsupported platform for AssetManager");
        return nullptr;
#endif
        return instance;
    }

    void AssetManager::init(void* nativeApp) {
        storedNativeApp = nativeApp;
        if (!instance) {
            create(nativeApp);
        }
    }

    AssetManager& AssetManager::get() {
        if (!instance) {
            AT_ERROR("AssetManager not initialized! Call AssetManager::init() first.");
            create(storedNativeApp);
        }
        return *instance;
    }

    void AssetManager::reset() {
        instance.reset();
        create(storedNativeApp);
        AT_INFO("AssetManager instance reset");
    }

    std::vector<char> AssetManager::loadTextFile(const std::string& resource) {
        std::filesystem::path filePath = get().getAssetsPath() / resource;

        std::ifstream file(filePath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            AT_ERROR("Failed to open file: {}", filePath.string());
            return {};
        }

        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);

        std::vector<char> buffer(size);
        if (!file.read(buffer.data(), size)) {
            AT_ERROR("Failed to read file: {}", filePath.string());
            return {};
        }

        file.close();
        AT_TRACE("Loaded file: {} ({} bytes)", resource, size);
        return buffer;
    }

}
