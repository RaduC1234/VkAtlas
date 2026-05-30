#include "AtlasPack.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace Atlas::Pack {
    namespace Internal {
        constexpr std::array<char, 8> HeaderMagic{'A', 'T', 'L', 'S', 'P', 'C', 'K', '1'};
        constexpr std::array<char, 8> FooterMagic{'A', 'P', 'C', 'K', 'E', 'N', 'D', '1'};
        constexpr uint32_t PackVersion = 1;
        constexpr uint64_t ChunkAlignment = 4096;
        constexpr uint64_t HeaderSize = 32;
        constexpr uint64_t FooterSize = 40;
        constexpr uint64_t FnvaOffset = 14695981039346656037ull;
        constexpr uint64_t FnvaPrime = 1099511628211ull;

        std::string toLower(std::string value) {
            std::ranges::transform(value, value.begin(), [](const unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            return value;
        }

        std::string normalizeSlashes(std::string value) {
            std::ranges::replace(value, '\\', '/');
            return value;
        }

        std::string normalizeMountPoint(std::string value) {
            value = normalizeSlashes(std::move(value));
            if (value.empty()) {
                return "/Game";
            }
            if (value.front() != '/') {
                value.insert(value.begin(), '/');
            }
            while (value.size() > 1 && value.back() == '/') {
                value.pop_back();
            }
            return value;
        }

        uint64_t hashBytes(const void *data, const size_t size, uint64_t hash = FnvaOffset) {
            const auto *bytes = static_cast<const uint8_t *>(data);
            for (size_t i = 0; i < size; ++i) {
                hash ^= bytes[i];
                hash *= FnvaPrime;
            }
            return hash;
        }

        uint64_t hashString(const std::string_view text) {
            return hashBytes(text.data(), text.size());
        }

        uint64_t hashFile(const std::filesystem::path &path) {
            std::ifstream file(path, std::ios::binary);
            if (!file) {
                throw std::runtime_error("Failed to open file for hashing: " + path.string());
            }

            std::array<char, 64 * 1024> buffer{};
            uint64_t hash = FnvaOffset;
            while (file) {
                file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
                const auto count = file.gcount();
                if (count > 0) {
                    hash = hashBytes(buffer.data(), static_cast<size_t>(count), hash);
                }
            }
            return hash;
        }

        std::string hex64(const uint64_t value) {
            std::ostringstream out;
            out << "0x" << std::hex << std::setw(16) << std::setfill('0') << value;
            return out.str();
        }

        void writeU32(std::ostream &out, uint32_t value) {
            for (int i = 0; i < 4; ++i) {
                out.put(static_cast<char>((value >> (i * 8)) & 0xffu));
            }
        }

        void writeU64(std::ostream &out, uint64_t value) {
            for (int i = 0; i < 8; ++i) {
                out.put(static_cast<char>((value >> (i * 8)) & 0xffu));
            }
        }

        uint32_t readU32(std::istream &in) {
            uint32_t value = 0;
            for (int i = 0; i < 4; ++i) {
                const int byte = in.get();
                if (byte == EOF) {
                    throw std::runtime_error("Unexpected end of file while reading uint32");
                }
                value |= static_cast<uint32_t>(static_cast<uint8_t>(byte)) << (i * 8);
            }
            return value;
        }

        uint64_t readU64(std::istream &in) {
            uint64_t value = 0;
            for (int i = 0; i < 8; ++i) {
                const int byte = in.get();
                if (byte == EOF) {
                    throw std::runtime_error("Unexpected end of file while reading uint64");
                }
                value |= static_cast<uint64_t>(static_cast<uint8_t>(byte)) << (i * 8);
            }
            return value;
        }

        void writeHeader(std::ostream &out, const uint64_t packageId, const uint64_t buildId) {
            out.write(HeaderMagic.data(), static_cast<std::streamsize>(HeaderMagic.size()));
            writeU32(out, PackVersion);
            writeU32(out, 0);
            writeU64(out, packageId);
            writeU64(out, buildId);
        }

        void writeFooter(std::ostream &out, const uint64_t indexOffset, const uint64_t indexSize, const uint64_t indexHash) {
            out.write(FooterMagic.data(), static_cast<std::streamsize>(FooterMagic.size()));
            writeU32(out, PackVersion);
            writeU32(out, 0);
            writeU64(out, indexOffset);
            writeU64(out, indexSize);
            writeU64(out, indexHash);
        }

        struct Footer {
            uint64_t indexOffset = 0;
            uint64_t indexSize = 0;
            uint64_t indexHash = 0;
        };

        Footer readFooter(std::istream &in, const uint64_t fileSize) {
            if (fileSize < HeaderSize + FooterSize) {
                throw std::runtime_error("File is too small to be an Atlas pack");
            }

            in.seekg(static_cast<std::streamoff>(fileSize - FooterSize), std::ios::beg);

            std::array<char, 8> magic{};
            in.read(magic.data(), static_cast<std::streamsize>(magic.size()));
            if (magic != FooterMagic) {
                throw std::runtime_error("Invalid Atlas pack footer magic");
            }

            const uint32_t version = readU32(in);
            (void) readU32(in);
            if (version != PackVersion) {
                throw std::runtime_error("Unsupported Atlas pack version: " + std::to_string(version));
            }

            Footer footer{};
            footer.indexOffset = readU64(in);
            footer.indexSize = readU64(in);
            footer.indexHash = readU64(in);
            if (footer.indexOffset + footer.indexSize > fileSize - FooterSize) {
                throw std::runtime_error("Atlas pack index range is outside the file");
            }
            return footer;
        }

        uint64_t tellp(std::ostream &out) {
            const auto pos = out.tellp();
            if (pos < 0) {
                throw std::runtime_error("Failed to query output stream position");
            }
            return static_cast<uint64_t>(pos);
        }

        void padToAlignment(std::ostream &out, const uint64_t alignment) {
            const uint64_t position = tellp(out);
            const uint64_t padding = (alignment - (position % alignment)) % alignment;
            static constexpr std::array<char, 4096> zeros{};

            uint64_t remaining = padding;
            while (remaining > 0) {
                const auto count = static_cast<std::streamsize>(std::min<uint64_t>(remaining, zeros.size()));
                out.write(zeros.data(), count);
                remaining -= static_cast<uint64_t>(count);
            }
        }

        std::string inferAssetType(const std::filesystem::path &path) {
            const std::string ext = toLower(path.extension().string());
            if (ext == ".ktx2" || ext == ".ktx") return "Texture";
            if (ext == ".atlasmesh" || ext == ".mesh") return "Mesh";
            if (ext == ".atlasmat" || ext == ".mat") return "Material";
            if (ext == ".atlasmap" || ext == ".atlasscene" || ext == ".scene") return "Map";
            if (ext == ".spv") return "Shader";
            if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".hdr" || ext == ".exr") return "SourceTexture";
            if (ext == ".glb" || ext == ".gltf" || ext == ".obj") return "SourceModel";
            return "Raw";
        }

        std::vector<std::filesystem::path> collectInputs(const PackOptions &options) {
            std::vector<std::filesystem::path> files;

            if (options.inputPaths.empty()) {
                if (!std::filesystem::exists(options.rootPath)) {
                    throw std::runtime_error("Root path does not exist: " + options.rootPath.string());
                }

                if (!std::filesystem::is_directory(options.rootPath)) {
                    throw std::runtime_error("Root path must be a directory when no input files are provided: " + options.rootPath.string());
                }

                if (options.recursive) {
                    for (const auto &entry: std::filesystem::recursive_directory_iterator(options.rootPath)) {
                        if (entry.is_regular_file()) {
                            files.push_back(entry.path());
                        }
                    }
                } else {
                    for (const auto &entry: std::filesystem::directory_iterator(options.rootPath)) {
                        if (entry.is_regular_file()) {
                            files.push_back(entry.path());
                        }
                    }
                }
            } else {
                for (const auto &input: options.inputPaths) {
                    const auto candidate = input.is_absolute() ? input : options.rootPath / input;
                    if (std::filesystem::is_directory(candidate)) {
                        for (const auto &entry: std::filesystem::recursive_directory_iterator(candidate)) {
                            if (entry.is_regular_file()) {
                                files.push_back(entry.path());
                            }
                        }
                    } else if (std::filesystem::is_regular_file(candidate)) {
                        files.push_back(candidate);
                    } else {
                        throw std::runtime_error("Input path does not exist or is not a regular file: " + candidate.string());
                    }
                }
            }

            std::ranges::sort(files, [](const auto &left, const auto &right) {
                return left.generic_string() < right.generic_string();
            });
            return files;
        }

        std::string logicalPathFor(const std::filesystem::path &root, const std::filesystem::path &file, const std::string &mountPoint) {
            std::error_code ec;
            std::filesystem::path relative = std::filesystem::relative(file, root, ec);
            if (ec || relative.empty() || relative.generic_string().starts_with("..")) {
                relative = file.filename();
            }

            std::string rel = normalizeSlashes(relative.generic_string());
            while (!rel.empty() && rel.front() == '/') {
                rel.erase(rel.begin());
            }

            return normalizeMountPoint(mountPoint) + "/" + rel;
        }

        void copyFileToStream(const std::filesystem::path &filePath, std::ostream &out) {
            std::ifstream input(filePath, std::ios::binary);
            if (!input) {
                throw std::runtime_error("Failed to open input file: " + filePath.string());
            }

            std::array<char, 64 * 1024> buffer{};
            while (input) {
                input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
                const auto count = input.gcount();
                if (count > 0) {
                    out.write(buffer.data(), count);
                }
            }
        }

        std::string packageNameFor(const std::filesystem::path &path) {
            std::string name = path.stem().string();
            return name.empty() ? "atlaspack" : name;
        }

        uint64_t parseHex64(std::string text) {
            text = toLower(std::move(text));
            if (text.starts_with("0x")) {
                text = text.substr(2);
            }

            uint64_t value = 0;
            std::istringstream in(text);
            in >> std::hex >> value;
            if (!in) {
                throw std::runtime_error("Invalid hex id: " + text);
            }
            return value;
        }

        const nlohmann::json *findAsset(const nlohmann::json &index, const std::string &query) {
            const std::string normalizedQuery = normalizeSlashes(query);
            const bool byId = toLower(normalizedQuery).starts_with("0x");
            const uint64_t queryId = byId ? parseHex64(normalizedQuery) : 0;

            for (const auto &asset: index.at("assets")) {
                if (byId) {
                    if (parseHex64(asset.at("id").get<std::string>()) == queryId) {
                        return &asset;
                    }
                    continue;
                }

                const auto path = asset.at("path").get<std::string>();
                const auto source = asset.value("source", std::string{});
                if (path == normalizedQuery || source == normalizedQuery || path.ends_with(normalizedQuery)) {
                    return &asset;
                }
            }

            return nullptr;
        }

        const nlohmann::json *findChunk(const nlohmann::json &index, const std::string &chunkId) {
            const uint64_t target = parseHex64(chunkId);
            for (const auto &chunk: index.at("chunks")) {
                if (parseHex64(chunk.at("id").get<std::string>()) == target) {
                    return &chunk;
                }
            }
            return nullptr;
        }
    }

    void writePack(const PackOptions &inputOptions) {
        PackOptions options = inputOptions;
        options.mountPoint = Internal::normalizeMountPoint(options.mountPoint);
        if (options.outputPath.empty()) {
            throw std::runtime_error("Missing output path");
        }

        const auto files = Internal::collectInputs(options);
        if (files.empty()) {
            throw std::runtime_error("No files to pack");
        }

        if (options.outputPath.has_parent_path()) {
            std::filesystem::create_directories(options.outputPath.parent_path());
        }

        std::ofstream out(options.outputPath, std::ios::binary);
        if (!out) {
            throw std::runtime_error("Failed to open output pack: " + options.outputPath.string());
        }

        Internal::writeHeader(out, 0, 0);
        Internal::padToAlignment(out, Internal::ChunkAlignment);

        nlohmann::json chunks = nlohmann::json::array();
        nlohmann::json assets = nlohmann::json::array();
        uint64_t buildHash = Internal::FnvaOffset;

        for (const auto &file: files) {
            Internal::padToAlignment(out, Internal::ChunkAlignment);

            const uint64_t offset = Internal::tellp(out);
            const uint64_t size = std::filesystem::file_size(file);
            const uint64_t contentHash = Internal::hashFile(file);
            const std::string logicalPath = Internal::logicalPathFor(options.rootPath, file, options.mountPoint);
            const std::string logicalKey = Internal::toLower(logicalPath);
            const uint64_t assetId = Internal::hashString(logicalKey);
            const uint64_t chunkId = Internal::hashString(logicalKey + "#" + Internal::hex64(contentHash));

            Internal::copyFileToStream(file, out);

            std::error_code relError;
            auto relativeSource = std::filesystem::relative(file, options.rootPath, relError);
            if (relError || relativeSource.empty()) {
                relativeSource = file.filename();
            }
            const std::string sourcePath = Internal::normalizeSlashes(relativeSource.generic_string());
            const std::string type = Internal::inferAssetType(file);

            chunks.push_back({
                {"id", Internal::hex64(chunkId)},
                {"offset", offset},
                {"compressedSize", size},
                {"uncompressedSize", size},
                {"compression", "None"},
                {"hash", Internal::hex64(contentHash)},
                {"alignment", Internal::ChunkAlignment}
            });

            assets.push_back({
                {"id", Internal::hex64(assetId)},
                {"path", logicalPath},
                {"source", sourcePath},
                {"type", type},
                {"chunk", Internal::hex64(chunkId)},
                {"size", size},
                {"hash", Internal::hex64(contentHash)}
            });

            buildHash = Internal::hashBytes(&assetId, sizeof(assetId), buildHash);
            buildHash = Internal::hashBytes(&contentHash, sizeof(contentHash), buildHash);
        }

        nlohmann::json index{
            {"format", "AtlasPack"},
            {"version", Internal::PackVersion},
            {
                "package", {
                    {"name", Internal::packageNameFor(options.outputPath)},
                    {"mountPoint", options.mountPoint},
                    {"sourceRoot", Internal::normalizeSlashes(std::filesystem::absolute(options.rootPath).generic_string())},
                    {"assetCount", assets.size()},
                    {"chunkCount", chunks.size()}
                }
            },
            {"assets", assets},
            {"chunks", chunks},
            {"dependencies", nlohmann::json::array()}
        };

        const std::string indexText = index.dump(2);
        const uint64_t indexOffset = Internal::tellp(out);
        out.write(indexText.data(), static_cast<std::streamsize>(indexText.size()));
        const uint64_t indexHash = Internal::hashString(indexText);
        Internal::writeFooter(out, indexOffset, indexText.size(), indexHash);

        const uint64_t packageId = Internal::hashString(Internal::toLower(options.mountPoint + "/" + Internal::packageNameFor(options.outputPath)));
        out.seekp(0, std::ios::beg);
        Internal::writeHeader(out, packageId, buildHash);

        std::cout << "Wrote " << options.outputPath.string() << "\n"
                << "  assets: " << files.size() << "\n"
                << "  index:  " << indexText.size() << " bytes\n";
    }

    LoadedPack readPackIndex(const std::filesystem::path &packPath) {
        std::ifstream in(packPath, std::ios::binary | std::ios::ate);
        if (!in) {
            throw std::runtime_error("Failed to open pack: " + packPath.string());
        }

        const uint64_t fileSize = static_cast<uint64_t>(in.tellg());
        in.seekg(0, std::ios::beg);

        std::array<char, 8> headerMagic{};
        in.read(headerMagic.data(), static_cast<std::streamsize>(headerMagic.size()));
        if (headerMagic != Internal::HeaderMagic) {
            throw std::runtime_error("Invalid Atlas pack header magic");
        }

        const uint32_t version = Internal::readU32(in);
        (void) Internal::readU32(in);
        (void) Internal::readU64(in);
        (void) Internal::readU64(in);
        if (version != Internal::PackVersion) {
            throw std::runtime_error("Unsupported Atlas pack version: " + std::to_string(version));
        }

        const Internal::Footer footer = Internal::readFooter(in, fileSize);
        in.seekg(static_cast<std::streamoff>(footer.indexOffset), std::ios::beg);

        std::string indexText;
        indexText.resize(static_cast<size_t>(footer.indexSize));
        in.read(indexText.data(), static_cast<std::streamsize>(indexText.size()));

        if (Internal::hashString(indexText) != footer.indexHash) {
            throw std::runtime_error("Atlas pack index hash mismatch");
        }

        return {packPath, nlohmann::json::parse(indexText)};
    }

    void printPackInfo(const std::filesystem::path &packPath) {
        const LoadedPack pack = readPackIndex(packPath);
        const auto &package = pack.index.at("package");
        uint64_t totalBytes = 0;
        for (const auto &chunk: pack.index.at("chunks")) {
            totalBytes += chunk.at("uncompressedSize").get<uint64_t>();
        }

        std::cout << "AtlasPack: " << packPath.string() << "\n"
                << "  name:       " << package.value("name", "") << "\n"
                << "  mount:      " << package.value("mountPoint", "") << "\n"
                << "  assets:     " << pack.index.at("assets").size() << "\n"
                << "  chunks:     " << pack.index.at("chunks").size() << "\n"
                << "  data bytes: " << totalBytes << "\n";
    }

    void listPackAssets(const std::filesystem::path &packPath, const std::string &typeFilter) {
        const LoadedPack pack = readPackIndex(packPath);
        for (const auto &asset: pack.index.at("assets")) {
            const std::string type = asset.at("type").get<std::string>();
            if (!typeFilter.empty() && type != typeFilter) {
                continue;
            }

            std::cout << asset.at("id").get<std::string>() << "  "
                    << type << "  "
                    << asset.at("size").get<uint64_t>() << " bytes  "
                    << asset.at("path").get<std::string>() << "\n";
        }
    }

    void extractPackAsset(const std::filesystem::path &packPath, const std::string &assetQuery, const std::filesystem::path &outputPath) {
        const LoadedPack pack = readPackIndex(packPath);
        const nlohmann::json *asset = Internal::findAsset(pack.index, assetQuery);
        if (!asset) {
            throw std::runtime_error("Asset not found: " + assetQuery);
        }

        const nlohmann::json *chunk = Internal::findChunk(pack.index, asset->at("chunk").get<std::string>());
        if (!chunk) {
            throw std::runtime_error("Chunk not found for asset: " + assetQuery);
        }

        if (outputPath.has_parent_path()) {
            std::filesystem::create_directories(outputPath.parent_path());
        }

        std::ifstream in(packPath, std::ios::binary);
        if (!in) {
            throw std::runtime_error("Failed to open pack: " + packPath.string());
        }

        std::ofstream out(outputPath, std::ios::binary);
        if (!out) {
            throw std::runtime_error("Failed to open output file: " + outputPath.string());
        }

        uint64_t remaining = chunk->at("uncompressedSize").get<uint64_t>();
        in.seekg(static_cast<std::streamoff>(chunk->at("offset").get<uint64_t>()), std::ios::beg);

        std::array<char, 64 * 1024> buffer{};
        while (remaining > 0) {
            const auto count = static_cast<std::streamsize>(std::min<uint64_t>(remaining, buffer.size()));
            in.read(buffer.data(), count);
            if (in.gcount() != count) {
                throw std::runtime_error("Unexpected end of pack while extracting asset");
            }
            out.write(buffer.data(), count);
            remaining -= static_cast<uint64_t>(count);
        }

        std::cout << "Extracted " << asset->at("path").get<std::string>() << " -> " << outputPath.string() << "\n";
    }
}
