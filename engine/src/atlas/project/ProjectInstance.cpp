#include "project/ProjectInstance.hpp"

#include <stdexcept>
#include <string>

#include "asset/AssetManager.hpp"
#include "core/Log.hpp"
#include "renderer/Renderer.hpp"
#include "scene/LevelScene.hpp"

#if defined(ATLAS_PLATFORM_WINDOWS)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

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
        if (!std::filesystem::exists(path)) {
            throw std::runtime_error("Project module does not exist: " + path.string());
        }

#if defined(ATLAS_PLATFORM_WINDOWS)
        HMODULE library = LoadLibraryExW(path.wstring().c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!library) {
            throw std::runtime_error("Failed to load project module: " + path.string() + " (Win32 error " + std::to_string(GetLastError()) + "; the module exists, but a dependent DLL may be missing)");
        }

        return library;
#else
        void *library = dlopen(path.string().c_str(), RTLD_NOW);
        if (!library) {
            throw std::runtime_error("Failed to load project module: " + path.string() + " (" + dlerror() + ")");
        }

        return library;
#endif
    }

    void ProjectInstance::closeLibrary(void *library) {
        if (!library) {
            return;
        }

#if defined(ATLAS_PLATFORM_WINDOWS)
        FreeLibrary(static_cast<HMODULE>(library));
#else
        dlclose(library);
#endif
    }

    void *ProjectInstance::loadSymbol(void *library, const char *symbolName, const std::filesystem::path &libraryPath) {
#if defined(_WIN32)
        void *symbol = reinterpret_cast<void *>(GetProcAddress(static_cast<HMODULE>(library), symbolName));
        if (!symbol) {
            throw std::runtime_error("Failed to load symbol '" + std::string(symbolName) + "' from " + libraryPath.string() + " (Win32 error " + std::to_string(GetLastError()) + ")");
        }

        return symbol;
#else
        dlerror();
        void *symbol = dlsym(library, symbolName);
        const char *error = dlerror();
        if (error) {
            throw std::runtime_error("Failed to load symbol '" + std::string(symbolName) + "' from " + libraryPath.string() + " (" + error + ")");
        }

        return symbol;
#endif
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
