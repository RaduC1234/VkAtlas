#pragma once

#include "core/Application.hpp"
#include "core/Log.hpp"

#include <exception>
#include <iostream>

#ifdef ATLAS_PLATFORM_ANDROID

#include <game-activity/native_app_glue/android_native_app_glue.h>
#include <android/log.h>

void android_main(android_app *app) {
    try {
        Atlas::Log::init();

        Atlas::Application *application = Atlas::CreateApplication({0, nullptr, app});
        application->run();
        delete application;
    } catch (const std::exception &error) {
        __android_log_print(ANDROID_LOG_ERROR, "AtlasRuntime", "Atlas failed: %s", error.what());
    }
}

#else

int main(int argc, char **argv) {
    try {
        Atlas::Log::init();

        Atlas::Application *application = Atlas::CreateApplication({argc, argv});
        application->run();
        delete application;
    } catch (const std::exception &error) {
        std::cerr << "Atlas failed: " << error.what() << '\n';
        system("pause");
        return 1;
    }

    return 0;
}

#endif
