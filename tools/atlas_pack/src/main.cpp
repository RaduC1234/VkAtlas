#include "AtlasPack.hpp"

#include <exception>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <ranges>
#include <stdexcept>
#include <string>
#include <vector>

namespace Atlas::Pack::Cli {
    void printUsage() {
        std::cout
            << "AtlasPack POC compiler\n\n"
            << "Usage:\n"
            << "  atlas_pack pack --root <dir> --out <file.atlaspack> [--mount /Game] [files...]\n"
            << "  atlas_pack info <file.atlaspack>\n"
            << "  atlas_pack list <file.atlaspack> [--type Texture]\n"
            << "  atlas_pack extract <file.atlaspack> <asset-path-or-id> <output-file>\n\n"
            << "Examples:\n"
            << "  atlas_pack pack --root assets --out build/content/game.atlaspack\n"
            << "  atlas_pack pack --root .atlas/cache --mount /Game --out build/content/game.atlaspack textures/wall.ktx2\n"
            << "  atlas_pack list build/content/game.atlaspack --type Texture\n"
            << "  atlas_pack extract build/content/game.atlaspack /Game/textures/wall.ktx2 debug/wall.ktx2\n";
    }

    bool hasArg(const std::vector<std::string> &args, const std::string &name) {
        return std::ranges::find(args, name) != args.end();
    }

    std::string takeOption(std::vector<std::string> &args, const std::string &name, const bool required = false) {
        const auto it = std::ranges::find(args, name);
        if (it == args.end()) {
            if (required) {
                throw std::runtime_error("Missing required option: " + name);
            }
            return {};
        }

        const auto valueIt = std::next(it);
        if (valueIt == args.end() || valueIt->starts_with("--")) {
            throw std::runtime_error("Missing value for option: " + name);
        }

        std::string value = *valueIt;
        args.erase(it, std::next(valueIt));
        return value;
    }

    int runPack(std::vector<std::string> args) {
        PackOptions options{};
        options.outputPath = takeOption(args, "--out", true);

        if (const std::string root = takeOption(args, "--root"); !root.empty()) {
            options.rootPath = root;
        }

        if (const std::string mount = takeOption(args, "--mount"); !mount.empty()) {
            options.mountPoint = mount;
        }

        if (hasArg(args, "--no-recursive")) {
            options.recursive = false;
            args.erase(std::ranges::find(args, "--no-recursive"));
        }

        for (const auto &arg: args) {
            if (arg.starts_with("--")) {
                throw std::runtime_error("Unknown option: " + arg);
            }
            options.inputPaths.emplace_back(arg);
        }

        writePack(options);
        return 0;
    }

    int runList(std::vector<std::string> args) {
        if (args.empty()) {
            throw std::runtime_error("list requires a pack path");
        }

        const std::filesystem::path packPath = args.front();
        args.erase(args.begin());
        const std::string type = takeOption(args, "--type");
        if (!args.empty()) {
            throw std::runtime_error("Unexpected argument for list: " + args.front());
        }

        listPackAssets(packPath, type);
        return 0;
    }
}

int main(const int argc, char **argv) {
    try {
        std::vector<std::string> args(argv + 1, argv + argc);
        if (args.empty() || args.front() == "--help" || args.front() == "-h") {
            Atlas::Pack::Cli::printUsage();
            return args.empty() ? 1 : 0;
        }

        const std::string command = args.front();
        args.erase(args.begin());

        if (command == "pack") {
            return Atlas::Pack::Cli::runPack(std::move(args));
        }

        if (command == "info") {
            if (args.size() != 1) {
                throw std::runtime_error("info requires exactly one pack path");
            }
            Atlas::Pack::printPackInfo(args[0]);
            return 0;
        }

        if (command == "list") {
            return Atlas::Pack::Cli::runList(std::move(args));
        }

        if (command == "extract") {
            if (args.size() != 3) {
                throw std::runtime_error("extract requires: <pack> <asset-path-or-id> <output-file>");
            }
            Atlas::Pack::extractPackAsset(args[0], args[1], args[2]);
            return 0;
        }

        throw std::runtime_error("Unknown command: " + command);
    } catch (const std::exception &e) {
        std::cerr << "atlas_pack: " << e.what() << "\n\n";
        Atlas::Pack::Cli::printUsage();
        return 1;
    }
}
