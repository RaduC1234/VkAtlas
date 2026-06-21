#include "utils/OSUtils.hpp"

#include <stdexcept>
#include <string>

#include "core/Log.hpp"

#if defined(ATLAS_PLATFORM_WINDOWS)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace Atlas {
    void *OSUtils::openDynamicLibrary(const std::filesystem::path &path) {
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

    void OSUtils::closeDynamicLibrary(void *library) {
        if (!library) return;
#if defined(ATLAS_PLATFORM_WINDOWS)
        FreeLibrary(static_cast<HMODULE>(library));
#else
        dlclose(library);
#endif
    }

    void *OSUtils::loadSymbol(void *library, const char *symbol, const std::filesystem::path &libraryPath) {
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

    void OSUtils::startProcessAsync(const std::filesystem::path &name, const std::string &args) {
#if defined(ATLAS_PLATFORM_WINDOWS)
        STARTUPINFOA si{};
        si.cb = sizeof(si);

        PROCESS_INFORMATION pi{};
        std::string cmdLine = "\"" + name.string() + "\" " + args;

        if (CreateProcessA( // https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessa
            nullptr,        // [in, optional]      LPCSTR                lpApplicationName,
            cmdLine.data(), // [in, out, optional] LPSTR                 lpCommandLine,
            nullptr,        // [in, optional]      LPSECURITY_ATTRIBUTES lpProcessAttributes,
            nullptr,        // [in, optional]      LPSECURITY_ATTRIBUTES lpThreadAttributes,
            FALSE,          // [in]                BOOL                  bInheritHandles,
            0,              // [in]                DWORD                 dwCreationFlags,
            nullptr,        // [in, optional]      LPVOID                lpEnvironment,
            nullptr,        // [in, optional]      LPCSTR                lpCurrentDirectory,
            &si,            // [in]                LPSTARTUPINFOA        lpStartupInfo,
            &pi             // [out]               LPPROCESS_INFORMATION lpProcessInformation
        )) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        } else {
            throw std::runtime_error(std::format("Process {} with arguments: {} failed: Error message: {}", name.string(), args, GetLastError()));
        }
#endif
    }
}
