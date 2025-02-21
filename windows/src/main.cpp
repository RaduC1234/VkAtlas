#include <windows.h>
#include <iostream>

typedef void (*RunAtlas)();

int main() {
    constexpr auto dllName = "atlas_engine.dll";

    const HMODULE engineDLL = LoadLibrary(dllName);
    if (!engineDLL) {
        std::cerr << "Failed to load "<< dllName << " (Error Code: " << GetLastError() << ")\n";
        return 1;
    }

    RunAtlas runEngine = reinterpret_cast<RunAtlas>(GetProcAddress(engineDLL, "runAtlas"));

    if (!runEngine) {
        std::cerr << "Failed to find function!" << std::endl;
        FreeLibrary(engineDLL);
        return 1;
    }

    runEngine();

    FreeLibrary(engineDLL);

    return 0;
}
