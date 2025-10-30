#pragma once

#ifdef _WIN32
#if defined(AVALON_BUILD_SHARED)
#define AVALON_API __declspec(dllexport)
#elif defined(AVALON_USE_SHARED)
#define AVALON_API __declspec(dllimport)
#else
#define AVALON_API
#endif
#elif defined(__ANDROID__) || defined(__linux__)
#if defined(AVALON_BUILD_SHARED) || defined(AVALON_USE_SHARED)
#define AVALON_API __attribute__((visibility("default")))
#else
#define AVALON_API
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define AVALON_API
#else
#define AVALON_API
#endif

#ifdef __cplusplus
#define EXTERN_C extern "C"
#else
#define EXTERN_C
#endif

#ifndef BIT
#define BIT(x) (1 << (x))
#endif
