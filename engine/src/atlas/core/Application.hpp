#pragma once

#include "core/LayerStack.hpp"
#include "asset/AssetManager.hpp"
#include "renderer/ImGuiLayer.hpp"
#include "renderer/Renderer.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <utility>

namespace Atlas {
    struct ApplicationCommandLineArgs {
        int count = 0;
        char **values = nullptr;
    };

    struct ApplicationCreateInfo {
        std::string name = "Atlas";
        std::filesystem::path projectManifest;
        std::filesystem::path projectModule;
        Renderer::CreateInfo rendererSettings;
        bool enableImGui = false;
        bool enableDockspace = false;
    };

    class Application {
    public:
        explicit Application(const ApplicationCreateInfo& specification);
        ~Application();

        Application(const Application &) = delete;
        Application &operator=(const Application &) = delete;

        void run();

        Renderer &renderer() { return renderer_; }
        const Renderer &renderer() const { return renderer_; }
        AssetManager &assets() { return *assetManager_; }
        const AssetManager &assets() const { return *assetManager_; }
        const ApplicationCreateInfo &specification() const { return specification_; }

        template<class T, class... Args>
        T &pushLayer(Args &&... args) {
            return static_cast<T &>(layers.pushLayer(std::make_unique<T>(std::forward<Args>(args)...)));
        }

        template<class T, class... Args>
        T &pushOverlay(Args &&... args) {
            return static_cast<T &>(layers.pushOverlay(std::make_unique<T>(std::forward<Args>(args)...)));
        }

    private:
        ApplicationCreateInfo specification_;
        Renderer renderer_;
        std::unique_ptr<AssetManager> assetManager_;
        LayerStack layers;
        std::unique_ptr<ImGuiLayer> imguiLayer;
    };

    extern Application *CreateApplication(ApplicationCommandLineArgs args);
}
