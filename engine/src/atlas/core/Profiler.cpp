#include "Profiler.hpp"

namespace Atlas {
    void Profiler::setThreadName(const char *name) {
#if defined(ATLAS_PROFILE_CPU)
        tracy::SetThreadName(name);
#else
        static_cast<void>(name);
#endif
    }
} // Atlas
