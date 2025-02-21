#pragma once

#ifdef ATLAS_PLATFORM_WINDOWS
    #ifdef AVALON_BUILD_SHARED
        #define AVALON_API __declspec(dllexport)
    #else
        #define AVALON_API __declspec(dllimport)
    #endif
#else
    #define AVALON_API
#endif

#ifdef __cplusplus
    #define EXTERN_C extern "C"
#else
    #define EXTERN_C
#endif

