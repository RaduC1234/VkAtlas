#include "ProjectManifest.hpp"

#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace Atlas {
    namespace {
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
    }

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

        manifest.name = readString(data, "name", manifest.name);
        manifest.engineVersion = readString(data, "engineVersion", manifest.engineVersion);
        manifest.startupScene = readString(data, "startupScene", manifest.startupScene);
        manifest.assetRoot = readString(data, "assetRoot", manifest.assetRoot);
        manifest.codeModule = readString(data, "codeModule", manifest.codeModule);

        if (const auto scenes = data.find("scenes"); scenes != data.end()) {
            if (!scenes->is_array()) {
                throw std::runtime_error("Project manifest field must be an array: scenes");
            }

            for (const auto &scene: *scenes) {
                if (!scene.is_string()) {
                    throw std::runtime_error("Project manifest scenes entries must be strings: " + manifestPath.string());
                }

                manifest.scenes.push_back(scene.get<std::string>());
            }
        }

        if (manifest.startupScene.empty()) {
            throw std::runtime_error("Project manifest is missing startupScene: " + manifestPath.string());
        }

        if (manifest.scenes.empty()) {
            manifest.scenes.push_back(manifest.startupScene);
        }

        return manifest;
    }
}
