#ifndef AVALON_ENTRYPOINT_HPP
#define AVALON_ENTRYPOINT_HPP

#ifdef AVALON_PLATFORM_ANDROID
#include <jni.h>
#include <game-activity/native_app_glue/android_native_app_glue.h>

extern "C" {
    void android_main(struct android_app *pApp);
}
#endif // AVALON_PLATFORM_ANDROID

#endif // AVALON_ENTRYPOINT_HPP
