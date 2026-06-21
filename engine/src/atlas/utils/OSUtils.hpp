#pragma once

#include <filesystem>

namespace Atlas {
    class OSUtils {
    public:
        static void *openDynamicLibrary(const std::filesystem::path &path);
        static void closeDynamicLibrary(void *library);
        static void *loadSymbol(void *library, const char *symbol, const std::filesystem::path &libraryPath);
        static void startProcessAsync(const std::filesystem::path &name, const std::string &args);

        static constexpr const char *extension() {
#if defined(ATLAS_PLATFORM_WINDOWS)
            return ".dll";
#elif defined(ATLAS_PLATFORM_MACOS)
            return ".dylib";
#else
            return ".so";
#endif
        }
    };
}
