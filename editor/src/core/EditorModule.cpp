#include "EditorLayer.hpp"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <utility>

#include <Atlas.hpp>

#ifndef ATLAS_DEFAULT_PROJECT_MANIFEST
#define ATLAS_DEFAULT_PROJECT_MANIFEST "samples/office/project.atlas.json"
#endif

#ifndef ATLAS_DEFAULT_PROJECT_MODULE
#define ATLAS_DEFAULT_PROJECT_MODULE ""
#endif

namespace Atlas {
    Application *CreateApplication(ApplicationCommandLineArgs args) {
        const std::filesystem::path manifestPath = args.count > 1
                                                       ? std::filesystem::path(args.values[1])
                                                       : std::filesystem::path{};
        const std::filesystem::path modulePath = args.count > 1
                                                     ? std::filesystem::path{}
                                                     : std::filesystem::path{};

        ApplicationCreateInfo createInfo{};
        createInfo.name = "Atlas Editor";
        createInfo.projectManifest = manifestPath;
        createInfo.projectModule = modulePath;
        createInfo.rendererCreateInfo.window.title = createInfo.name;
        createInfo.rendererCreateInfo.window.properties = Window::Properties::Decorated | Window::Properties::Resizeable;
        createInfo.rendererCreateInfo.window.inputProvider = &DesktopInputProvider::instance();
        createInfo.rendererCreateInfo.sceneOutputTarget = Renderer::SceneOutputTarget::Texture;

        auto *application = new Application(createInfo);
        auto &projectLayer = application->pushLayer<ProjectLayer>(application->renderer(), application->assets(), manifestPath, modulePath);
        application->pushOverlay<Editor::EditorLayer>(projectLayer);

        return application;
    }
}
