#include <Atlas.hpp>

#include <filesystem>
#include <string>
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
        std::filesystem::path manifestPath = ATLAS_DEFAULT_PROJECT_MANIFEST;
        std::filesystem::path startupLevelOverride;

        bool manifestWasSet = false;

        for (int i = 1; i < args.count; ++i) {
            const std::string arg = args.values[i] ? args.values[i] : "";

            if (arg == "--level" && i + 1 < args.count) {
                startupLevelOverride = args.values[++i];

            } else if (!arg.empty() && arg[0] != '-' && !manifestWasSet) {
                manifestPath = arg;
                manifestWasSet = true;
            }
        }

        const std::filesystem::path modulePath = manifestWasSet ? std::filesystem::path{} : ATLAS_DEFAULT_PROJECT_MODULE;


        ApplicationCreateInfo specification{};
        specification.name = "Atlas Runtime";
        specification.projectManifest = manifestPath;
        specification.projectModule = modulePath;
        specification.rendererCreateInfo.window.title = specification.name;
#ifdef ATLAS_RUNTIME_ENABLE_IMGUI
        specification.enableImGui = true;
#endif

        auto *application = new Application(specification);
        application->pushLayer<ProjectLayer>(application->renderer(), application->assets(), manifestPath, modulePath, startupLevelOverride);
#ifdef ATLAS_RUNTIME_ENABLE_IMGUI
        application->pushOverlay<Runtime::RuntimeDebugLayer>();
#endif

        return application;
    }
}
