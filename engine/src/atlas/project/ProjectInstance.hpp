#pragma once

#include <filesystem>
#include <memory>

#include "project/ProjectManifest.hpp"
#include "project/ProjectModule.hpp"

namespace Atlas {
    class AssetManager;
    class Renderer;

    class ProjectInstance {
    public:
        ProjectInstance() = default;
        ~ProjectInstance();

        ProjectInstance(const ProjectInstance &) = delete;
        ProjectInstance &operator=(const ProjectInstance &) = delete;

        void load(
            Renderer &renderer,
            AssetManager &assets,
            const std::filesystem::path &manifestPath,
            const std::filesystem::path &moduleOverride = {},
            const std::filesystem::path &startupLevelOverride = {});
        void unload();

        IScene *scene() const { return currentScene; }
        const ProjectManifest &manifest() const { return projectManifest; }
        const std::filesystem::path &rootPath() const { return projectRoot; }
        const std::filesystem::path &assetsPath() const { return assetRoot; }

    private:
        static std::filesystem::path absolutePath(const std::filesystem::path &path);
        static std::filesystem::path projectRelativePath(const std::filesystem::path &projectRoot, const std::filesystem::path &path);
        static void *openLibrary(const std::filesystem::path &path);
        static void closeLibrary(void *library);
        static void *loadSymbol(void *library, const char *symbolName, const std::filesystem::path &libraryPath);

        ProjectManifest projectManifest;
        std::filesystem::path projectRoot;
        std::filesystem::path assetRoot;
        std::filesystem::path projectModulePath;
        std::unique_ptr<ProjectContext> projectContext;

        void *projectLibrary = nullptr;
        IProjectModule *projectModule = nullptr;
        DestroyProjectModuleFn destroyProjectModule = nullptr;
        IScene *currentScene = nullptr;
    };
}
