#pragma once

#include <filesystem>
#include <string>

#include "asset/AssetManager.hpp"
#include "scene/IScene.hpp"

namespace Atlas {
    class Renderer;
    struct ProjectManifest;

#ifndef ATLAS_PROJECT_API
#if defined(_WIN32)
#define ATLAS_PROJECT_API __declspec(dllexport)
#else
#define ATLAS_PROJECT_API __attribute__((visibility("default")))
#endif
#endif

    struct ProjectContext {
        Renderer &renderer;
        AssetManager &assets;
        const ProjectManifest &manifest;
        std::filesystem::path projectRoot;
        std::filesystem::path assetRoot;
    };

    class IProjectModule {
    public:
        virtual ~IProjectModule() = default;

        virtual void onProjectLoaded(ProjectContext &context) {}
        virtual IScene *createScene(ProjectContext &context, const std::string &sceneId) = 0;
        virtual void destroyScene(IScene *scene) { delete scene; }
        virtual void onProjectUnloaded(ProjectContext &context) {}
    };

    using CreateProjectModuleFn = IProjectModule *(*)();
    using DestroyProjectModuleFn = void (*)(IProjectModule *);

    inline constexpr const char *CREATE_PROJECT_MODULE_SYMBOL = "atlasCreateProjectModule";
    inline constexpr const char *DESTROY_PROJECT_MODULE_SYMBOL = "atlasDestroyProjectModule";
}
