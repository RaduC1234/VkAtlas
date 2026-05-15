#include "project/ProjectInstance.hpp"

#include <stdexcept>
#include <string>

#include "asset/AssetManager.hpp"
#include "renderer/Renderer.hpp"

#if defined(ATLAS_PLATFORM_WINDOWS)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace Atlas {
    namespace {
        std::filesystem::path absolutePath(const std::filesystem::path &path) {
            if (path.is_absolute()) {
                return path.lexically_normal();
            }

            return std::filesystem::absolute(path).lexically_normal();
        }

        std::filesystem::path projectRelativePath(const std::filesystem::path &projectRoot, const std::filesystem::path &path) {
            if (path.is_absolute()) {
                return path.lexically_normal();
            }

            return (projectRoot / path).lexically_normal();
        }

        void *openLibrary(const std::filesystem::path &path) {
            if (!std::filesystem::exists(path)) {
                throw std::runtime_error("Project module does not exist: " + path.string());
            }

#if defined(ATLAS_PLATFORM_WINDOWS)
            HMODULE library = LoadLibraryExW(path.wstring().c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
            if (!library) {
                throw std::runtime_error("Failed to load project module: " + path.string() + " (Win32 error " + std::to_string(GetLastError()) + "; the module exists, but a dependent DLL may be missing)");
            }

            return reinterpret_cast<void *>(library);
#else
            void *library = dlopen(path.string().c_str(), RTLD_NOW);
            if (!library) {
                throw std::runtime_error("Failed to load project module: " + path.string() + " (" + dlerror() + ")");
            }

            return library;
#endif
        }

        void closeLibrary(void *library) {
            if (!library) {
                return;
            }

#if defined(ATLAS_PLATFORM_WINDOWS)
            FreeLibrary(static_cast<HMODULE>(library));
#else
            dlclose(library);
#endif
        }

        void *loadSymbol(void *library, const char *symbolName, const std::filesystem::path &libraryPath) {
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
    }

    ProjectInstance::~ProjectInstance() {
        unload();
    }

    void ProjectInstance::load(Renderer &renderer, AssetManager &assets, const std::filesystem::path &manifestPath, const std::filesystem::path &moduleOverride) {
        unload();

        try {
            const auto manifestAbsolutePath = absolutePath(manifestPath);
            projectManifest = loadProjectManifest(manifestAbsolutePath);
            projectRoot = manifestAbsolutePath.parent_path();
            assetRoot = projectRelativePath(projectRoot, projectManifest.assetRoot);

            if (moduleOverride.empty() && projectManifest.codeModule.empty()) {
                throw std::runtime_error("Project manifest is missing codeModule: " + manifestAbsolutePath.string());
            }

            projectModulePath = moduleOverride.empty()
                                    ? projectRelativePath(projectRoot, projectManifest.codeModule)
                                    : absolutePath(moduleOverride);

            assets.setRootPath(assetRoot);

            projectLibrary = openLibrary(projectModulePath);
            const auto createProjectModule = reinterpret_cast<CreateProjectModuleFn>(loadSymbol(projectLibrary, CREATE_PROJECT_MODULE_SYMBOL, projectModulePath));
            destroyProjectModule = reinterpret_cast<DestroyProjectModuleFn>(loadSymbol(projectLibrary, DESTROY_PROJECT_MODULE_SYMBOL, projectModulePath));

            projectModule = createProjectModule();
            if (!projectModule) {
                throw std::runtime_error("Project module factory returned null: " + projectModulePath.string());
            }

            projectContext = std::make_unique<ProjectContext>(ProjectContext{
                renderer,
                assets,
                projectManifest,
                projectRoot,
                assetRoot
            });

            projectModule->onProjectLoaded(*projectContext);
            currentScene = projectModule->createScene(*projectContext, projectManifest.startupScene);
            if (!currentScene) {
                throw std::runtime_error(
                    "Project module returned null scene '" + projectManifest.startupScene +
                    "' from " + projectModulePath.string() +
                    " loaded by manifest " + manifestAbsolutePath.string()
                );
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

        projectModule = nullptr;
        destroyProjectModule = nullptr;
        projectContext.reset();

        if (projectLibrary) {
            closeLibrary(projectLibrary);
            projectLibrary = nullptr;
        }
    }
}
