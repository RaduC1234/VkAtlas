#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <iostream>

typedef void (*RunAtlas)();

int main() {
#ifdef _WIN32
    constexpr auto libName = "atlas_engine.dll";
    HMODULE engineLib = LoadLibrary(libName);
    if (!engineLib) {
        std::cerr << "Failed to load " << libName << " (Error: " << GetLastError() << ")\n";
        return 1;
    }

    auto runEngine = reinterpret_cast<RunAtlas>(GetProcAddress(engineLib, "runAtlas"));
    if (!runEngine) {
        std::cerr << "Failed to find function!\n";
        FreeLibrary(engineLib);
        return 1;
    }

    runEngine();
    FreeLibrary(engineLib);
#else
    constexpr auto libName = "libatlas_engine.so";
    void* engineLib = dlopen(libName, RTLD_LAZY);
    if (!engineLib) {
        std::cerr << "Failed to load " << libName << ": " << dlerror() << "\n";
        return 1;
    }

    dlerror(); // Clear errors
    auto runEngine = reinterpret_cast<RunAtlas>(dlsym(engineLib, "runAtlas"));

    const char* error = dlerror();
    if (error) {
        std::cerr << "Failed to find function: " << error << "\n";
        dlclose(engineLib);
        return 1;
    }

    runEngine();
    dlclose(engineLib);
#endif

    return 0;
}
