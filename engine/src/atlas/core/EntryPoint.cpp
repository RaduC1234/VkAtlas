#include "EntryPoint.hpp"

#include "Application.hpp"
#include "Log.hpp"

#ifdef AVALON_PLATFORM_WINDOWS
#endif
extern "C" __declspec(dllexport) void runAtlas() {
    Atlas::Log::init();
    AT_INFO("Starting Engine...")
    Atlas::Application app{{}};
    app.run();
}
#ifdef AVALON_PLATFORM_ANDROID
extern "C" void android_main(struct android_app *pApp) {
    Engine engine;
    engine.Init();

    while (true) {
        int events;
        android_poll_source *pSource;
        if (ALooper_pollAll(0, nullptr, &events, (void**)&pSource) >= 0) {
            if (pSource) {
                pSource->process(pApp, pSource);
            }
        }

        engine.Update();
        engine.Render();
    }

    engine.Shutdown();
}
#endif
