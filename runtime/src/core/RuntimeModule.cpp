#include <Atlas.hpp>

#include <filesystem>
#include <utility>

#ifdef ATLAS_RUNTIME_ENABLE_IMGUI
#include "RuntimeDebugLayer.hpp"
#endif

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

        ApplicationSpecification specification{};
        specification.name = "Atlas Runtime";
        specification.projectManifest = manifestPath;
        specification.projectModule = modulePath;
        specification.rendererSettings.windowSettings.title = specification.name;
#ifdef ATLAS_RUNTIME_ENABLE_IMGUI
        specification.enableImGui = true;
#endif

        auto *application = new Application(std::move(specification));
        application->pushLayer<ProjectLayer>(application->renderer(), application->assets(), manifestPath, modulePath);
#ifdef ATLAS_RUNTIME_ENABLE_IMGUI
        application->pushOverlay<Runtime::RuntimeDebugLayer>();
#endif

        return application;
    }
}
