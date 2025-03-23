#include "Application.hpp"
#include "AssetManager.hpp"
#include "Log.hpp"

#ifdef _WIN32
extern "C" __declspec(dllexport) void runAtlas() {
    Atlas::Log::init();
    Atlas::AssetManager::init();
    AT_INFO("Starting Engine...")
    Atlas::Application app{{}};
    app.run();
}

#elif defined(__ANDROID__)
#include <game-activity/GameActivity.cpp>
#include <game-text-input/gametextinput.cpp>

extern "C" {

#include <game-activity/native_app_glue/android_native_app_glue.c>

void android_main(android_app *app) {

    // Android glue expects this for threading
    //app->onAppCmd = handle_cmd;
    app->userData = nullptr;

    bool isInitialized = false;

    while (true) {
        int events;
        android_poll_source *source;

        // This blocks until an event or timeout occurs
        while (ALooper_pollOnce(0, nullptr, &events, (void **) &source) >= 0) {
            if (source) source->process(app, source);

            if (app->destroyRequested) {
                // Optionally clean up
                return;
            }
        }

        if (!isInitialized && app->window) {
            isInitialized = true;

            Atlas::Log::init();
            Atlas::ApplicationSpecification specification;
            specification.pNativeApp = reinterpret_cast<void*>(app);

            Atlas::Application application(specification);
            application.run();
        }
    }
}
}

#endif // __ANDROID__
