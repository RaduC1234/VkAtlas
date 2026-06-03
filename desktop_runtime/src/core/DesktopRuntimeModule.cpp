#include <Atlas.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <utility>

#ifndef ATLAS_DEFAULT_PROJECT_MANIFEST
#define ATLAS_DEFAULT_PROJECT_MANIFEST "samples/office/project.atlas.json"
#endif

#ifndef ATLAS_DEFAULT_PROJECT_MODULE
#define ATLAS_DEFAULT_PROJECT_MODULE ""
#endif

namespace Atlas {
    std::filesystem::path resolveRuntimeLevelOverride(const std::filesystem::path &path) {
        if (path.empty()) {
            return path;
        }

        if (path.is_absolute()) {
            return path.lexically_normal();
        }

        return std::filesystem::absolute(path).lexically_normal();
    }

    bool isProjectManifestPath(const std::filesystem::path &path) {
        return path.filename().string().ends_with(".atlas.json");
    }

    Application *CreateApplication(ApplicationCommandLineArgs args) {
        std::filesystem::path manifestPath = ATLAS_DEFAULT_PROJECT_MANIFEST;
        std::filesystem::path startupLevelOverride;

        bool manifestWasSet = false;
        bool useDeferredInput = false;

        for (int i = 1; i < args.count; ++i) {
            const std::string arg = args.values[i] ? args.values[i] : "";

            if (arg == "--editor" ) {
                useDeferredInput = true;
            }

            if (arg == "--level" && i + 1 < args.count) {
                const std::filesystem::path levelPath = resolveRuntimeLevelOverride(args.values[++i]);
                if (isProjectManifestPath(levelPath)) {
                    manifestPath = levelPath;
                    startupLevelOverride.clear();
                    manifestWasSet = true;
                } else {
                    startupLevelOverride = levelPath;
                }

            } else if (!arg.empty() && arg[0] != '-' && !manifestWasSet) {
                manifestPath = arg;
                manifestWasSet = true;
            }
        }

        const std::filesystem::path modulePath = manifestWasSet ? std::filesystem::path{} : ATLAS_DEFAULT_PROJECT_MODULE;


        ApplicationCreateInfo createInfo{};
        createInfo.name = "Atlas Runtime";
        createInfo.projectManifest = manifestPath;
        createInfo.projectModule = modulePath;
        createInfo.rendererCreateInfo.window.title = createInfo.name;
        createInfo.rendererCreateInfo.window.inputProvider = /*useDeferredInput? &PipeServer::InputProvider::instance() :*/ &DesktopInputProvider::instance();
        createInfo.onFrame = [title = createInfo.name, elapsed = 0.0f, frames = uint32_t{0}] (Window &window, const float deltaTime) mutable {
            elapsed += deltaTime;
            ++frames;

            if (elapsed < 0.25f) {
                return;
            }

            const float fps = static_cast<float>(frames) / elapsed;
            char buffer[128]{};
            std::snprintf(buffer, sizeof(buffer), "%s - %.0f FPS", title.c_str(), fps);
            window.setTitle(buffer);

            elapsed = 0.0f;
            frames = 0;
        };

        auto application = std::make_unique<Application>(createInfo);
        application->pushLayer<ProjectLayer>(application->renderer(), application->assets(), manifestPath, modulePath, startupLevelOverride);

        return application.release();
    }
}
