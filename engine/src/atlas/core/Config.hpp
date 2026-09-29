#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>

namespace Atlas {
    class Config {
    public:
        static Config &get();

        std::optional<std::filesystem::path> resolveMount(const std::string &mount,const std::filesystem::path &relativePath);
        void reload();

    private:
        Config() = default;

        void ensureLoaded();
        void loadFile(const std::filesystem::path &file, const std::filesystem::path &dir);

        std::unordered_map<std::string, std::filesystem::path> table_;
        bool loaded_ = false;
    };
}
