#include "utils/DynamicLibrary.hpp"

#include <stdexcept>
#include <string>

#if defined(ATLAS_PLATFORM_WINDOWS)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace Atlas {
    void *DynamicLibrary::open(const std::filesystem::path &path) {
        if (!std::filesystem::exists(path)) {
            throw std::runtime_error("Library does not exist: " + path.string());
        }

#if defined(ATLAS_PLATFORM_WINDOWS)
        HMODULE lib = LoadLibraryExW(path.wstring().c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!lib) {
            throw std::runtime_error("Failed to load library: " + path.string() +
                                     " (Win32 error " + std::to_string(GetLastError()) + ")");
        }
        return lib;
#else
        void *lib = dlopen(path.string().c_str(), RTLD_NOW);
        if (!lib) {
            throw std::runtime_error("Failed to load library: " + path.string() +
                                     " (" + dlerror() + ")");
        }
        return lib;
#endif
    }

    void DynamicLibrary::close(void *library) {
        if (!library) return;
#if defined(ATLAS_PLATFORM_WINDOWS)
        FreeLibrary(static_cast<HMODULE>(library));
#else
        dlclose(library);
#endif
    }

    void *DynamicLibrary::loadSymbol(void *library, const char *symbol, const std::filesystem::path &libraryPath) {
#if defined(ATLAS_PLATFORM_WINDOWS)
        void *sym = reinterpret_cast<void *>(GetProcAddress(static_cast<HMODULE>(library), symbol));
        if (!sym) {
            throw std::runtime_error("Failed to load symbol '" + std::string(symbol) +
                                     "' from " + libraryPath.string() +
                                     " (Win32 error " + std::to_string(GetLastError()) + ")");
        }
        return sym;
#else
        dlerror();
        void *sym = dlsym(library, symbol);
        const char *error = dlerror();
        if (error) {
            throw std::runtime_error("Failed to load symbol '" + std::string(symbol) + "' from " + libraryPath.string() + " (" + error + ")");
        }
        return sym;
#endif
    }
}
