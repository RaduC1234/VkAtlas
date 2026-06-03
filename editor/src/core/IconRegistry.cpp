#include "IconRegistry.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>

#include <lunasvg.h>

#include "asset/AssetManager.hpp"
#include "asset/Texture.hpp"
#include "core/Log.hpp"
#include "renderer/Device.hpp"
#include "ImGuiLayer.hpp"
#include "renderer/resources/GPUTexture.hpp"

namespace Atlas::Editor {
    IconRegistry::IconRegistry(Device &device, std::filesystem::path iconRoot)
        : device(device),
          iconRoot(resolveEditorPath(iconRoot.empty() ? defaultIconRoot() : std::move(iconRoot))) {
        loadIconCatalog();
    }

    IconRegistry::~IconRegistry() {
        clear();
    }

    const IconRegistry::Icon &IconRegistry::get(const std::string &name, const uint32_t pixelSize) {
        const std::string nameKey = iconKey(name);
        const std::string key = cacheKey(nameKey, pixelSize);
        if (const auto found = icons.find(key); found != icons.end()) {
            return found->second.icon;
        }

        try {
            auto result = icons.emplace(key, load(nameKey, pixelSize));
            return result.first->second.icon;
        } catch (const std::exception &error) {
            AT_ERROR("Failed to load editor icon '{}': {}", name, error.what());
            return missingIcon;
        }
    }

    void IconRegistry::clear() {
        if (icons.empty()) {
            return;
        }

        vkDeviceWaitIdle(device.device());
        for (auto &pair: icons) {
            Entry &entry = pair.second;
            if (entry.icon.descriptor != VK_NULL_HANDLE) {
                ImGuiLayer::removeTexture(entry.icon.descriptor);
                entry.icon.descriptor = VK_NULL_HANDLE;
            }
        }

        icons.clear();
    }

    void IconRegistry::loadIconCatalog() {
        iconPaths.clear();

        if (!std::filesystem::exists(iconRoot) || !std::filesystem::is_directory(iconRoot)) {
            AT_WARN("Editor icon folder does not exist: {}", iconRoot.string());
            return;
        }

        for (const auto &entry: std::filesystem::directory_iterator(iconRoot)) {
            if (!entry.is_regular_file()) {
                continue;
            }

            std::string extension = entry.path().extension().string();
            std::ranges::transform(extension, extension.begin(), [](unsigned char value) {
                return static_cast<char>(std::tolower(value));
            });

            if (extension != ".svg") {
                continue;
            }

            iconPaths[entry.path().stem().string()] = entry.path();
        }
    }

    IconRegistry::Entry IconRegistry::load(const std::string &name, const uint32_t pixelSize) {
        if (pixelSize == 0) {
            throw std::runtime_error("icon size must be greater than zero");
        }

        const std::filesystem::path path = resolvePath(name);
        auto document = lunasvg::Document::loadFromFile(path.string());
        if (!document) {
            throw std::runtime_error("failed to parse " + path.string());
        }

        lunasvg::Bitmap bitmap = document->renderToBitmap(static_cast<int>(pixelSize), static_cast<int>(pixelSize));
        if (bitmap.isNull() || bitmap.width() <= 0 || bitmap.height() <= 0) {
            throw std::runtime_error("failed to render " + path.string());
        }

        bitmap.convertToRGBA();

        const uint32_t width = static_cast<uint32_t>(bitmap.width());
        const uint32_t height = static_cast<uint32_t>(bitmap.height());
        const size_t rowSize = static_cast<size_t>(width) * 4;

        std::vector<std::byte> pixels(rowSize * height);
        const uint8_t *source = bitmap.data();
        for (uint32_t row = 0; row < height; ++row) {
            const auto *rowBegin = reinterpret_cast<const std::byte *>(source + static_cast<size_t>(bitmap.stride()) * row);
            std::copy(rowBegin, rowBegin + rowSize, pixels.begin() + static_cast<size_t>(row) * rowSize);
        }

        Texture texture(pixels, width, height, VK_FORMAT_R8G8B8A8_SRGB, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
        auto gpuTexture = std::make_unique<GPUTexture>(device, texture);

        VkCommandBuffer commandBuffer = device.beginGraphicsCommands();
        gpuTexture->recordUpload(commandBuffer);
        device.endGraphicsCommands(commandBuffer);
        gpuTexture->onUploadComplete();

        const VkDescriptorSet descriptor = ImGuiLayer::addTexture(
            gpuTexture->getSampler(),
            gpuTexture->getImageView(),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        return Entry{
            .icon = {
                .descriptor = descriptor,
                .width = width,
                .height = height,
            },
            .texture = std::move(gpuTexture),
        };
    }

    std::filesystem::path IconRegistry::resolvePath(const std::string &name) const {
        const std::string key = iconKey(name);
        if (const auto found = iconPaths.find(key); found != iconPaths.end()) {
            return found->second;
        }

        std::filesystem::path path(name);
        if (!path.has_extension()) {
            path.replace_extension(".svg");
        }

        if (path.is_absolute()) {
            return path;
        }

        return iconRoot / path;
    }

    std::string IconRegistry::cacheKey(const std::string &name, const uint32_t pixelSize) const {
        return iconKey(name) + "#" + std::to_string(pixelSize);
    }

    std::string IconRegistry::iconKey(const std::string &name) {
        return std::filesystem::path(name).stem().string();
    }

    std::filesystem::path IconRegistry::resolveEditorPath(const std::filesystem::path &path) {
        const std::string generic = path.generic_string();
        if (generic.starts_with("##")) {
            return AssetManager::resolveFilePath(generic);
        }
        return path;
    }

    std::filesystem::path IconRegistry::defaultIconRoot() {
        return "##editor/icons";
    }
}
