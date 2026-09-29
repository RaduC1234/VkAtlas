#pragma once

#include <filesystem>

namespace Atlas {
    class OSServices {
    public:
        static void startProcessAsync(const std::filesystem::path &name, const std::string &args);
    };
}
