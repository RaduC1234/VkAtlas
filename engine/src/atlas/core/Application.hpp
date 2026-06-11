#pragma once

#include "core/LayerStack.hpp"
#include "asset/AssetManager.hpp"
#include "renderer/Renderer.hpp"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <utility>

namespace Atlas {
    struct ApplicationCommandLineArgs {
        int count = 0;
        char **values = nullptr;
        void *pNativeApp = nullptr;
    };

    struct ApplicationCreateInfo {
        std::string name = "Atlas";
        std::filesystem::path projectManifest;
        std::filesystem::path projectModule;
        Renderer::CreateInfo rendererCreateInfo;
        std::function<void(Window &, float)> onFrame;
    };

    class Application {
    public:
        Application(const ApplicationCreateInfo &specification);
        ~Application();

        Application(const Application &) = delete;
        Application &operator=(const Application &) = delete;

        void run();

        Renderer &renderer() { return renderer_; }
        const Renderer &renderer() const { return renderer_; }
        AssetManager &assets() { return assetManager_; }
        const AssetManager &assets() const { return assetManager_; }
        const ApplicationCreateInfo &specification() const { return specification_; }

        template<class T, class... Args>
        T &pushLayer(Args &&... args) {
            return static_cast<T &>(layers_.pushLayer(std::make_unique<T>(std::forward<Args>(args)...)));
        }

        template<class T, class... Args>
        T &pushOverlay(Args &&... args) {
            return static_cast<T &>(layers_.pushOverlay(std::make_unique<T>(std::forward<Args>(args)...)));
        }

    private:
        ApplicationCreateInfo specification_;
        Renderer renderer_;
        AssetManager assetManager_;
        LayerStack layers_;
    };

    extern Application *CreateApplication(const ApplicationCommandLineArgs &args);
}
