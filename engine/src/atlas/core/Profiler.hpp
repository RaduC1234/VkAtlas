#pragma once

#if defined(ATLAS_PROFILE_CPU)
#include <tracy/Tracy.hpp>
#endif

#if defined(ATLAS_PROFILE_GPU)
#include <vulkan/vulkan.h>
#include <tracy/TracyVulkan.hpp>
#endif

namespace Atlas {
    class Profiler {
    public:
        static void setThreadName(const char *name);
    };
} // Atlas

#define ATLAS_PROFILE_CONCAT_INNER(a, b) a##b
#define ATLAS_PROFILE_CONCAT(a, b) ATLAS_PROFILE_CONCAT_INNER(a, b)

#if defined(ATLAS_PROFILE_CPU)
#define ATLAS_PROFILE_SCOPE(name) ZoneNamedN(ATLAS_PROFILE_CONCAT(atlasProfileZone, __LINE__), name, true)
#define ATLAS_PROFILE_SCOPE_DYNAMIC(name) ZoneTransientN(ATLAS_PROFILE_CONCAT(atlasProfileZone, __LINE__), name, true)
#define ATLAS_PROFILE_FUNCTION() ZoneNamed(ATLAS_PROFILE_CONCAT(atlasProfileZone, __LINE__), true)
#define ATLAS_PROFILE_FRAME() FrameMark
#define ATLAS_PROFILE_THREAD(name) ::Atlas::Profiler::setThreadName(name)
#else
#define ATLAS_PROFILE_SCOPE(name) static_cast<void>(0)
#define ATLAS_PROFILE_SCOPE_DYNAMIC(name) static_cast<void>(name)
#define ATLAS_PROFILE_FUNCTION() static_cast<void>(0)
#define ATLAS_PROFILE_FRAME() static_cast<void>(0)
#define ATLAS_PROFILE_THREAD(name) static_cast<void>(name)
#endif

#if defined(ATLAS_PROFILE_GPU)
#define ATLAS_PROFILE_GPU_ZONE(ctx, cmd, name) TracyVkZone(ctx, cmd, name)
#define ATLAS_PROFILE_GPU_ZONE_DYNAMIC(ctx, cmd, name) TracyVkZoneTransient(ctx, ATLAS_PROFILE_CONCAT(atlasProfileGpuZone, __LINE__), cmd, name, true)
#define ATLAS_PROFILE_GPU_COLLECT(ctx, cmd)    TracyVkCollect(ctx, cmd)
#else
#define ATLAS_PROFILE_GPU_ZONE(ctx, cmd, name) static_cast<void>(0)
#define ATLAS_PROFILE_GPU_ZONE_DYNAMIC(ctx, cmd, name) static_cast<void>(name)
#define ATLAS_PROFILE_GPU_COLLECT(ctx, cmd)    static_cast<void>(0)
#endif
