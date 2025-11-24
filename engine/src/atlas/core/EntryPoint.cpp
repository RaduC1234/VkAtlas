#include "Application.hpp"
#include "Log.hpp"

#if defined(__ANDROID__)
    // Android-specific includes
    #include <game-activity/GameActivity.cpp>
    #include <game-text-input/gametextinput.cpp>

    extern "C" {
#include <game-activity/native_app_glue/android_native_app_glue.c>

        void android_main(struct android_app *app) {
            bool isInitialized = false;

            while (true) {
                int events;
                struct android_poll_source *source;

                // Wait for android to initialize
                // This blocks until an event or timeout occurs
                while (ALooper_pollOnce(0, nullptr, &events, (void **) &source) >= 0) {
                    if (source) {
                        source->process(app, source);
                    }

                    if (app->destroyRequested) {
                        // Optionally clean up
                        return;
                    }
                }

                if (!isInitialized && app->window) {
                    isInitialized = true;

                    Atlas::Log::init();
                    Atlas::ApplicationSpecification specification{};
                    specification.pNativeApp = reinterpret_cast<void*>(app);

                    Atlas::Application application(specification);
                    application.run();
                }
            }
        }
    }

#else
#ifdef _WIN32
#define ATLAS_API __declspec(dllexport)
#else
#define ATLAS_API __attribute__((visibility("default")))
#endif

extern "C" ATLAS_API void runAtlas() {
    Atlas::Log::init();
    Atlas::ApplicationSpecification specification{};
    Atlas::Application app{specification};
    app.run();
}

#endif // __ANDROID__
