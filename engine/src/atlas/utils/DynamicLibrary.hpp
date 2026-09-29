#pragma once
#include <filesystem>

namespace Atlas {
    class DynamicLibrary {
    public:
        DynamicLibrary(const std::filesystem::path &path);

        DynamicLibrary(const DynamicLibrary &) = delete;
        DynamicLibrary &operator=(const DynamicLibrary &) = delete;

        DynamicLibrary(DynamicLibrary &&other) noexcept;
        DynamicLibrary &operator=(DynamicLibrary &&other) noexcept;

        ~DynamicLibrary();

        /**
         * Look up a symbol and cast it to the requested function/data pointer type.
         *
         * Usage:
         *   auto fn = lib.getSymbol<int(*)(float)>("my_func");
         *   fn(1.0f);
         */
        template<typename T>
        T getSymbol(const std::string &name) const {
            static_assert(
                std::is_pointer_v<T>,
                "T must be a pointer type (function pointer or data pointer)"
            );
            return reinterpret_cast<T>(loadSymbol(name.c_str()));
        }

        /** Returns the raw handle (HMODULE on Windows, void* elsewhere). */
        [[nodiscard]] void *nativeHandle() const noexcept { return handle_; }

        /** Returns true when the library was loaded successfully and not yet released. */
        [[nodiscard]] explicit operator bool() const noexcept { return handle_ != nullptr; }

        static constexpr const char *extension() {
#if defined(ATLAS_PLATFORM_WINDOWS)
            return ".dll";
#elif defined(ATLAS_PLATFORM_MACOS)
            return ".dylib";
#else
            return ".so";
#endif
        }

    private:
        void *loadSymbol(const char *symbol) const;
        void close() noexcept;

        void *handle_{nullptr};
        std::filesystem::path path_;
    };
} // Atlas
