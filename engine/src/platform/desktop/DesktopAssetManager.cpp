#include "DesktopAssetManager.hpp"
#include <fstream>

#include "core/Log.hpp"

namespace Atlas {

    DesktopAssetManager::DesktopAssetManager(void* nativeApp) {
        assetsPath = std::filesystem::current_path() / "assets";
        AT_INFO("Desktop AssetManager initialized. Assets path: {}", assetsPath.string());
    }

    std::vector<char> DesktopAssetManager::loadTextFile(const std::string& resource) {
        std::filesystem::path filePath = assetsPath / resource;

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
