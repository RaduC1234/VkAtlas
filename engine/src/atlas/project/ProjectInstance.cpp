#include "project/ProjectInstance.hpp"

#include <stdexcept>
#include <string>

#include "asset/AssetManager.hpp"
#include "core/Log.hpp"
#include "renderer/Renderer.hpp"
#include "scene/LevelScene.hpp"
#include "utils/DynamicLibrary.hpp"

namespace Atlas {
    std::filesystem::path ProjectInstance::absolutePath(const std::filesystem::path &path) {
        if (path.is_absolute()) {
            return path.lexically_normal();
        }

        return std::filesystem::absolute(path).lexically_normal();
    }

    std::filesystem::path ProjectInstance::projectRelativePath(const std::filesystem::path &projectRoot, const std::filesystem::path &path) {
        if (path.is_absolute()) {
            return path.lexically_normal();
        }

        return (projectRoot / path).lexically_normal();
    }

    void *ProjectInstance::openLibrary(const std::filesystem::path &path) {
        return DynamicLibrary::open(path);
    }

    void ProjectInstance::closeLibrary(void *library) {
        DynamicLibrary::close(library);
    }

    void *ProjectInstance::loadSymbol(void *library, const char *symbolName, const std::filesystem::path &libraryPath) {
        return DynamicLibrary::loadSymbol(library, symbolName, libraryPath);
    }

    ProjectInstance::~ProjectInstance() {
        unload();
    }

    void ProjectInstance::load(
        Renderer &renderer,
        AssetManager &assets,
        const std::filesystem::path &manifestPath,
        const std::filesystem::path &moduleOverride,
        const std::filesystem::path &startupLevelOverride) {
        unload();

        try {
            const auto manifestAbsolutePath = absolutePath(manifestPath);
            projectManifest = loadProjectManifest(manifestAbsolutePath);
            projectRoot = manifestAbsolutePath.parent_path();
            assetRoot = projectRelativePath(projectRoot, projectManifest.assetRoot);
            const std::string startupLevel = startupLevelOverride.empty()
                                                 ? projectManifest.startupLevel
                                                 : startupLevelOverride.generic_string();

            projectModulePath = moduleOverride.empty() ? std::filesystem::path{} : absolutePath(moduleOverride);
            if (moduleOverride.empty() && !projectManifest.codeModule.empty()) {
                projectModulePath = projectRelativePath(projectRoot, projectManifest.codeModule);
            }

            if (!projectModulePath.empty() && !std::filesystem::exists(projectModulePath)) {
                AT_WARN(
                    "Project module '{}' does not exist; loading '{}' with the built-in level scene",
                    projectModulePath.string(),
                    manifestAbsolutePath.string());
                projectModulePath.clear();
            }

            assets.overwriteRootPath(assetRoot);

            projectContext = std::make_unique<ProjectContext>(ProjectContext{
                renderer,
                assets,
                projectManifest,
                projectRoot,
                assetRoot
            });

            if (!projectModulePath.empty()) {
                projectLibrary = openLibrary(projectModulePath);
                const auto createProjectModule = reinterpret_cast<CreateProjectModuleFn>(loadSymbol(projectLibrary, CREATE_PROJECT_MODULE_SYMBOL, projectModulePath));
                destroyProjectModule = reinterpret_cast<DestroyProjectModuleFn>(loadSymbol(projectLibrary, DESTROY_PROJECT_MODULE_SYMBOL, projectModulePath));

                projectModule = createProjectModule();
                if (!projectModule) {
                    throw std::runtime_error("Project module factory returned null: " + projectModulePath.string());
                }

                projectModule->onProjectLoaded(*projectContext);
                currentScene = projectModule->createScene(*projectContext, startupLevel);
            } else {
                const std::filesystem::path levelPath = projectRelativePath(projectRoot, startupLevel);
                currentScene = new LevelScene(renderer, assets, levelPath, projectRoot, assetRoot);
            }

            if (!currentScene) {
                const std::string source = projectModulePath.empty() ? std::string("built-in level scene") : projectModulePath.string();
                throw std::runtime_error("Project returned null level '" + startupLevel + "' from " + source + " loaded by manifest " + manifestAbsolutePath.string());
            }

            currentScene->onLoad(entt::registry{});
        } catch (...) {
            unload();
            throw;
        }
    }

    void ProjectInstance::unload() {
        if (currentScene) {
            currentScene->onDelete();

            if (projectModule) {
                projectModule->destroyScene(currentScene);
            } else {
                delete currentScene;
            }

            currentScene = nullptr;
        }

        if (projectModule && projectContext) {
            projectModule->onProjectUnloaded(*projectContext);
        }

        if (destroyProjectModule && projectModule) {
            destroyProjectModule(projectModule);
        }

        if (projectContext) {
            projectContext->assets.clearCaches();
        }

        projectModule = nullptr;
        destroyProjectModule = nullptr;
        projectContext.reset();

        if (projectLibrary) {
            closeLibrary(projectLibrary);
            projectLibrary = nullptr;
        }
    }
}
