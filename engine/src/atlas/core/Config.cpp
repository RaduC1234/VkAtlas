#include "Config.hpp"

#include <fstream>

#include <nlohmann/json.hpp>

#include "Log.hpp"

namespace Atlas {
    Config &Config::get() {
        static Config instance;
        return instance;
    }

    void Config::reload() {
        loaded_ = false;
        table_.clear();
    }

    void Config::ensureLoaded() {
        if (loaded_) {
            return;
        }
        loaded_ = true;

        for (auto dir = std::filesystem::current_path(); !dir.empty(); dir = dir.parent_path()) {
            const auto candidate = dir / "config.json";
            if (std::filesystem::exists(candidate)) {
                this->loadFile(candidate, dir);
                return;
            }

            if (dir == dir.root_path()) {
                break;
            }
        }
    }

    void Config::loadFile(const std::filesystem::path &file, const std::filesystem::path &dir) {
        try {
            std::ifstream f(file);
            const auto json = nlohmann::json::parse(f);
            for (const auto &[key, value]: json.items()) {
                if (value.is_string()) {
                    std::filesystem::path p(value.get<std::string>());
                    if (p.is_relative()) {
                        p = (dir / p).lexically_normal();
                    }
                    table_[key] = p;
                    AT_INFO("Config: mount '##{}' -> '{}'", key, p.string());
                }
            }
        } catch (const std::exception &e) {
            AT_ERROR("Config: failed to parse {}: {}", file.string(), e.what());
        }
    }

    std::optional<std::filesystem::path> Config::resolveMount(const std::string &mount, const std::filesystem::path &relativePath) {
        ensureLoaded();
        if (const auto it = table_.find(mount); it != table_.end()) {
            return (it->second / relativePath).lexically_normal();
        }
        return std::nullopt;
    }
}
