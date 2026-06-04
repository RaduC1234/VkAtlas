#include <Atlas.hpp>
#include <platform/android/AndroidInputProvider.hpp>

#include <filesystem>
#include <memory>

namespace Atlas {
    Application *CreateApplication(ApplicationCommandLineArgs args) {
        ApplicationCreateInfo createInfo{};
        createInfo.name = "Atlas Runtime";
        createInfo.rendererCreateInfo.window.pNativeApp  = args.pNativeApp;
        createInfo.rendererCreateInfo.window.title       = createInfo.name;
        createInfo.rendererCreateInfo.window.inputProvider = &AndroidInputProvider::instance();

        auto application = std::make_unique<Application>(createInfo);
        application->pushLayer<ProjectLayer>(
                application->renderer(),
                application->assets(),
                createInfo.projectManifest,
                createInfo.projectModule,
                std::filesystem::path{});

        return application.release();
    }
}
