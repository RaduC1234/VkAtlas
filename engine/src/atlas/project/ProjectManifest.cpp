#include "ProjectManifest.hpp"

#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace Atlas {
    namespace ProjectManifestDetail {
        using Json = nlohmann::json;

        std::string readString(const Json &data, const char *key, std::string fallback = {}) {
            const auto it = data.find(key);
            if (it == data.end()) {
                return fallback;
            }

            if (!it->is_string()) {
                throw std::runtime_error(std::string("Project manifest field must be a string: ") + key);
            }

            return it->get<std::string>();
        }

        std::vector<std::string> readStringArray(const Json &data, const char *key, const std::filesystem::path &manifestPath) {
            std::vector<std::string> values;
            const auto entries = data.find(key);
            if (entries == data.end()) {
                return values;
            }

            if (!entries->is_array()) {
                throw std::runtime_error(std::string("Project manifest field must be an array: ") + key);
            }

            for (const auto &entry: *entries) {
                if (!entry.is_string()) {
                    throw std::runtime_error("Project manifest " + std::string(key) + " entries must be strings: " + manifestPath.string());
                }

                values.push_back(entry.get<std::string>());
            }

            return values;
        }
    }

    using ProjectManifestDetail::Json;

    ProjectManifest loadProjectManifest(const std::filesystem::path &manifestPath) {
        std::ifstream file(manifestPath);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open project manifest: " + manifestPath.string());
        }

        ProjectManifest manifest{};
        Json data;

        try {
            file >> data;
        } catch (const Json::exception &error) {
            throw std::runtime_error("Failed to parse project manifest JSON: " + manifestPath.string() + " (" + error.what() + ")");
        }

        if (!data.is_object()) {
            throw std::runtime_error("Project manifest root must be a JSON object: " + manifestPath.string());
        }

        manifest.name = ProjectManifestDetail::readString(data, "name", manifest.name);
        manifest.engineVersion = ProjectManifestDetail::readString(data, "engineVersion", manifest.engineVersion);
        manifest.startupLevel = ProjectManifestDetail::readString(data, "startupLevel", manifest.startupLevel);
        manifest.assetRoot = ProjectManifestDetail::readString(data, "assetRoot", manifest.assetRoot);
        manifest.codeModule = ProjectManifestDetail::readString(data, "codeModule", manifest.codeModule);
        manifest.levels = ProjectManifestDetail::readStringArray(data, "levels", manifestPath);

        if (manifest.startupLevel.empty()) {
            throw std::runtime_error("Project manifest is missing startupLevel: " + manifestPath.string());
        }

        if (manifest.levels.empty()) {
            manifest.levels.push_back(manifest.startupLevel);
        }

        return manifest;
    }
}
