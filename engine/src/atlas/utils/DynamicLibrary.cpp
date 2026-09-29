#include "DynamicLibrary.hpp"

#if defined(ATLAS_PLATFORM_WINDOWS)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace Atlas {
    DynamicLibrary::DynamicLibrary(const std::filesystem::path &path)
        : path_(path) {
        if (!std::filesystem::exists(path)) {
            throw std::runtime_error("Library does not exist: " + path.string());
        }

#if defined(ATLAS_PLATFORM_WINDOWS)
        handle_ = static_cast<void *>(
            LoadLibraryExW(path.wstring().c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH)
        );

        if (!handle_)
            throw std::runtime_error(
                "Failed to load library: " + path.string() +
                " (Win32 error " + std::to_string(GetLastError()) + ")"
            );
#else
        handle_ = dlopen(path.string().c_str(), RTLD_NOW);
        if (!handle_)
            throw std::runtime_error(
                "Failed to load library: " + path.string() +
                " (" + dlerror() + ")"
            );
#endif
    }

    DynamicLibrary::DynamicLibrary(DynamicLibrary &&other) noexcept
        : handle_(other.handle_)
          , path_(std::move(other.path_)) {
        other.handle_ = nullptr;
    }

    DynamicLibrary &DynamicLibrary::operator=(DynamicLibrary &&other) noexcept {
        if (this != &other) {
            close();
            handle_ = other.handle_;
            path_ = std::move(other.path_);
            other.handle_ = nullptr;
        }
        return *this;
    }

    DynamicLibrary::~DynamicLibrary() {
        close();
    }

    void *DynamicLibrary::loadSymbol(const char *symbol) const {
#if defined(ATLAS_PLATFORM_WINDOWS)
        void *sym = reinterpret_cast<void *>(
            GetProcAddress(static_cast<HMODULE>(handle_), symbol)
        );
        if (!sym)
            throw std::runtime_error(
                "Failed to load symbol '" + std::string(symbol) +
                "' from " + path_.string() +
                " (Win32 error " + std::to_string(GetLastError()) + ")"
            );
        return sym;
#else
        dlerror(); // clear any previous error
        void *sym = dlsym(handle_, symbol);
        const char *err = dlerror();
        if (err)
            throw std::runtime_error(
                "Failed to load symbol '" + std::string(symbol) +
                "' from " + path_.string() +
                " (" + err + ")"
            );
        return sym;
#endif
    }

    void DynamicLibrary::close() noexcept {
        if (!handle_) return;
#if defined(ATLAS_PLATFORM_WINDOWS)
        FreeLibrary(static_cast<HMODULE>(handle_));
#else
        dlclose(handle_);
#endif
        handle_ = nullptr;
    }
} // Atlas
