#include "ProjectCreator.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/Config.hpp"
#include "utils/DynamicLibrary.hpp"

namespace Atlas::Editor {
    struct Marker {
        std::string_view name;
        std::string value;
    };

    constexpr const char *engineVersion = "0.1";
    constexpr const char *startupLevel = "MainLevel";
    constexpr std::string_view templateSuffix = ".template";
    constexpr const char *moduleExtension = DynamicLibrary::extension();

    static std::filesystem::path absoluteNormalizedPath(const std::filesystem::path &path) {
        if (path.is_absolute()) return path.lexically_normal();
        return std::filesystem::absolute(path).lexically_normal();
    }

    static std::string trim(std::string value) {
        const auto first = std::ranges::find_if(value, [](unsigned char ch) { return !std::isspace(ch); });
        const auto last = std::find_if(value.rbegin(), value.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base();
        if (first >= last) return {};
        return std::string(first, last);
    }

    static std::vector<std::string> splitWords(const std::string &value) {
        std::vector<std::string> words;
        std::string word;
        for (unsigned char ch : value) {
            if (std::isalnum(ch)) {
                word.push_back(static_cast<char>(ch));
            } else if (!word.empty()) {
                words.push_back(word);
                word.clear();
            }
        }
        if (!word.empty()) words.push_back(word);
        return words;
    }

    static std::string toPascalIdentifier(const std::string &value, std::string_view fallback) {
        const auto words = splitWords(value);
        std::string result;
        for (const auto &word : words) {
            for (size_t i = 0; i < word.size(); ++i) {
                const auto ch = static_cast<unsigned char>(word[i]);
                result.push_back(static_cast<char>(i == 0 ? std::toupper(ch) : std::tolower(ch)));
            }
        }
        if (result.empty()) result = std::string(fallback);
        if (std::isdigit(static_cast<unsigned char>(result.front()))) result.insert(0, "Project");
        return result;
    }

    static std::string toSnakeIdentifier(const std::string &value, std::string_view fallback) {
        const auto words = splitWords(value);
        std::string result;
        for (const auto &word : words) {
            if (!result.empty()) result.push_back('_');
            for (unsigned char ch : word) result.push_back(static_cast<char>(std::tolower(ch)));
        }
        if (result.empty()) result = std::string(fallback);
        if (std::isdigit(static_cast<unsigned char>(result.front()))) result.insert(0, "project_");
        return result;
    }

    static std::string readFileAsString(const std::filesystem::path &path) {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) throw std::runtime_error("Failed to open template file: " + path.string());
        return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }

    static void writeTextFile(const std::filesystem::path &path, const std::string &contents) {
        if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary);
        if (!file.is_open()) throw std::runtime_error("Failed to create file: " + path.string());
        file << contents;
        if (!file.good()) throw std::runtime_error("Failed to write file: " + path.string());
    }

    static std::filesystem::path findTemplateRoot() {
        constexpr std::string_view kRelSuffix = "templates/project";

        if (auto resolved = Config::get().resolveMount("editor", kRelSuffix)) {
            if (std::filesystem::is_directory(*resolved)) return *resolved;
        }

        for (auto dir = std::filesystem::current_path(); !dir.empty(); dir = dir.parent_path()) {
            const auto candidate = dir / "editor" / "assets" / "templates" / "project";
            if (std::filesystem::is_directory(candidate)) return candidate;
            if (dir == dir.root_path()) break;
        }

        throw std::runtime_error("Failed to find editor project template directory: editor/assets/templates/project");
    }

    static std::string replaceMarkers(std::string value, const std::vector<Marker> &markers) {
        for (const auto &marker : markers) {
            size_t pos = 0;
            while ((pos = value.find(marker.name, pos)) != std::string::npos) {
                value.replace(pos, marker.name.size(), marker.value);
                pos += marker.value.size();
            }
        }
        return value;
    }

    static std::filesystem::path renderedRelativePath(const std::filesystem::path &relativePath, const std::vector<Marker> &markers) {
        std::string rendered = replaceMarkers(relativePath.generic_string(), markers);
        if (rendered.ends_with(templateSuffix)) rendered.resize(rendered.size() - templateSuffix.size());
        return std::filesystem::path(rendered).lexically_normal();
    }

    static bool isTemplateFile(const std::filesystem::path &path) {
        return path.filename().string().ends_with(templateSuffix);
    }

    static std::vector<std::pair<std::filesystem::path, std::filesystem::path>> collectTemplateFiles(
        const std::filesystem::path &templateRoot,
        const std::filesystem::path &projectRoot,
        const std::vector<Marker> &markers) {
        std::vector<std::pair<std::filesystem::path, std::filesystem::path>> files;
        for (const auto &entry : std::filesystem::recursive_directory_iterator(templateRoot)) {
            if (!entry.is_regular_file()) continue;
            const auto rel = std::filesystem::relative(entry.path(), templateRoot);
            const auto target = (projectRoot / renderedRelativePath(rel, markers)).lexically_normal();
            if (std::filesystem::exists(target)) throw std::runtime_error("Project file already exists: " + target.string());
            files.emplace_back(entry.path(), target);
        }
        return files;
    }

    static void copyTemplateFile(const std::filesystem::path &src, const std::filesystem::path &dst, const std::vector<Marker> &markers) {
        if (dst.has_parent_path()) std::filesystem::create_directories(dst.parent_path());
        if (isTemplateFile(src)) {
            writeTextFile(dst, replaceMarkers(readFileAsString(src), markers));
            return;
        }
        std::filesystem::copy_file(src, dst, std::filesystem::copy_options::none);
    }

    static std::vector<Marker> makeMarkers(
        const std::string &projectName,
        const std::string &projectClassName,
        const std::string &targetName,
        const std::filesystem::path &manifestPath) {
        const std::string modulePath = "build/modules/" + targetName + moduleExtension;
        return {
            {"{{PROJECT_NAME}}", projectName},
            {"{{PROJECT_CLASS}}", projectClassName},
            {"{{PROJECT_TARGET}}", targetName},
            {"{{PROJECT_MODULE}}", modulePath},
            {"{{PROJECT_MANIFEST_FILE}}", manifestPath.filename().generic_string()},
            {"{{STARTUP_LEVEL}}", startupLevel},
            {"{{STARTUP_LEVEL_PATH}}", "assets/levels/" + std::string(startupLevel) + ".atlaslevel"},
            {"{{ENGINE_VERSION}}", engineVersion}
        };
    }

    ProjectCreateResult ProjectCreator::create(const ProjectCreateInfo &info) {
        if (info.manifestPath.empty()) throw std::runtime_error("Project manifest path is empty");

        const std::filesystem::path manifestPath = absoluteNormalizedPath(info.manifestPath);
        const std::filesystem::path rootPath = manifestPath.parent_path();
        if (rootPath.empty()) throw std::runtime_error("Project manifest must have a parent directory");

        std::string projectName = trim(info.name);
        if (projectName.empty()) projectName = defaultNameForPath(manifestPath);

        const std::string targetName = "atlas_" + toSnakeIdentifier(projectName, "project") + "_project";
        const std::string projectClassName = toPascalIdentifier(projectName, "Atlas") + "Project";
        const std::filesystem::path templateRoot = findTemplateRoot();
        const std::vector<Marker> markers = makeMarkers(projectName, projectClassName, targetName, manifestPath);
        const auto files = collectTemplateFiles(templateRoot, rootPath, markers);

        for (const auto &[src, dst] : files) {
            copyTemplateFile(src, dst, markers);
        }

        return {rootPath, manifestPath, projectName, targetName};
    }

    std::string ProjectCreator::defaultNameForPath(const std::filesystem::path &manifestPath) {
        const std::filesystem::path absolute = absoluteNormalizedPath(manifestPath);
        std::string name = trim(absolute.parent_path().filename().string());
        if (!name.empty()) return name;
        std::string stem = absolute.stem().string();
        if (!stem.empty()) return stem;
        return "Untitled Project";
    }
}
