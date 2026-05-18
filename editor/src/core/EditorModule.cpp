#include "EditorLayer.hpp"

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
                                                       ? args.values[1]
                                                       : ATLAS_DEFAULT_PROJECT_MANIFEST;
        const std::filesystem::path modulePath = args.count > 1
                                                     ? std::filesystem::path{}
                                                     : ATLAS_DEFAULT_PROJECT_MODULE;

        ApplicationCreateInfo createInfo{};
        createInfo.name = "Atlas Editor";
        createInfo.projectManifest = manifestPath;
        createInfo.projectModule = modulePath;
        createInfo.rendererCreateInfo.window.title = createInfo.name;
        createInfo.rendererCreateInfo.window.properties = Window::Properties::Decorated | Window::Properties::Resizeable | Window::Properties::CustomTitlebar;
        createInfo.enableImGui = true;
        createInfo.enableDockspace = true;

        auto *application = new Application(createInfo);
        auto &projectLayer = application->pushLayer<ProjectLayer>(application->renderer(), application->assets(), manifestPath, modulePath);
        application->pushOverlay<Editor::EditorLayer>(projectLayer);

        return application;
    }
}
