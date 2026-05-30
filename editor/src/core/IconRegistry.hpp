#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>

#include <imgui.h>
#include <vulkan/vulkan.h>

namespace Atlas {
    class Device;
    class GPUTexture;
}

namespace Atlas::Editor {
    class IconRegistry final {
    public:
        struct Icon {
            VkDescriptorSet descriptor = VK_NULL_HANDLE;
            uint32_t width = 0;
            uint32_t height = 0;

            bool valid() const { return descriptor != VK_NULL_HANDLE; }
            ImTextureID textureId() const { return (ImTextureID) descriptor; }
            ImVec2 size() const { return ImVec2(static_cast<float>(width), static_cast<float>(height)); }
        };

        explicit IconRegistry(Device &device, std::filesystem::path iconRoot = {});
        ~IconRegistry();

        IconRegistry(const IconRegistry &) = delete;
        IconRegistry &operator=(const IconRegistry &) = delete;
        IconRegistry(IconRegistry &&) = delete;
        IconRegistry &operator=(IconRegistry &&) = delete;

        const Icon &get(const std::string &name, uint32_t pixelSize);
        void clear();

    private:
        struct Entry {
            Icon icon;
            std::unique_ptr<GPUTexture> texture;
        };

        Device &device;
        std::filesystem::path iconRoot;
        std::unordered_map<std::string, std::filesystem::path> iconPaths;
        std::unordered_map<std::string, Entry> icons;
        Icon missingIcon;

        void loadIconCatalog();
        Entry load(const std::string &name, uint32_t pixelSize);
        std::filesystem::path resolvePath(const std::string &name) const;
        std::string cacheKey(const std::string &name, uint32_t pixelSize) const;
        static std::string iconKey(const std::string &name);
        static std::filesystem::path resolveEditorPath(const std::filesystem::path &path);
        static std::filesystem::path defaultIconRoot();
    };
}
