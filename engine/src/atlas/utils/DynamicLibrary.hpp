#pragma once

#include <filesystem>

namespace Atlas {
    class DynamicLibrary {
    public:
        static void *open(const std::filesystem::path &path);
        static void close(void *library);
        static void *loadSymbol(void *library, const char *symbol, const std::filesystem::path &libraryPath);

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
