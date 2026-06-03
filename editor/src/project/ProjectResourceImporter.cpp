#include "project/ProjectResourceImporter.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include "project/importers/EntityBuffer.hpp"
#include "project/importers/GLTFImporter.hpp"
#include "project/importers/IAssetImporter.hpp"
#include "project/importers/OBJImporter.hpp"

namespace Atlas::Editor::ProjectResourceImport {
    std::string sanitizeName(const std::string &value, const std::string &fallback) {
        std::string result;
        result.reserve(value.size());

        for (const unsigned char ch: value) {
            if (std::isalnum(ch)) {
                result.push_back(static_cast<char>(ch));
            } else if (ch == '_' || ch == '-') {
                result.push_back(static_cast<char>(ch));
            } else if (!result.empty() && result.back() != '_') {
                result.push_back('_');
            }
        }

        while (!result.empty() && result.back() == '_') {
            result.pop_back();
        }

        return result.empty() ? fallback : result;
    }

    std::filesystem::path resourcePath(const std::filesystem::path &assetRoot, const std::string &virtualPath) {
        return (assetRoot / std::filesystem::path(virtualPath)).lexically_normal();
    }

    std::string uniqueVirtualPath(
        const std::filesystem::path &assetRoot,
        const std::string &folder,
        const std::string &baseName,
        const std::string &extension) {
        const std::string sanitizedBase = sanitizeName(baseName, "Asset");

        for (uint32_t index = 0; index < 100000; ++index) {
            std::string name = sanitizedBase;
            if (index > 0) {
                name += "_" + std::to_string(index);
            }
            name += extension;

            const std::string virtualPath = (std::filesystem::path(folder) / name).generic_string();
            if (!std::filesystem::exists(resourcePath(assetRoot, virtualPath))) {
                return virtualPath;
            }
        }

        throw std::runtime_error("Failed to allocate unique project resource path for: " + baseName);
    }

    std::string sourceBaseName(const std::string &sourcePath) {
        const std::string stem = std::filesystem::path(sourcePath).stem().string();
        return sanitizeName(stem, "ImportedAsset");
    }

    std::string lowerExtension(const std::string &sourcePath) {
        std::string extension = std::filesystem::path(sourcePath).extension().string();
        std::ranges::transform(extension, extension.begin(), [](unsigned char value) {
            return static_cast<char>(std::tolower(value));
        });
        return extension;
    }

    std::vector<std::unique_ptr<IAssetImporter>> createImporters(AssetManager &assets) {
        std::vector<std::unique_ptr<IAssetImporter>> importers;
        importers.push_back(std::make_unique<GLTFImporter>(assets));
        importers.push_back(std::make_unique<OBJImporter>(assets));
        return importers;
    }

    IAssetImporter *findImporter(std::vector<std::unique_ptr<IAssetImporter>> &importers, const std::string &sourcePath) {
        const std::string extension = lowerExtension(sourcePath);
        for (const auto &importer: importers) {
            const auto extensions = importer->extensions();
            if (std::ranges::find(extensions, extension) != extensions.end()) {
                return importer.get();
            }
        }

        return nullptr;
    }

    std::string materialName(const Material &material, const std::string &fallback) {
        return sanitizeName(material.name, fallback);
    }

    std::string persistTexture(
        ProjectLayer &projectLayer,
        const std::string &sourceName,
        const std::string &slotName,
        AssetHandle<Texture> &handle,
        std::unordered_map<const void *, std::string> &texturePaths) {
        if (!handle.valid() || !handle.get()) {
            return {};
        }

        const void *identity = handle.identity();
        const auto existing = texturePaths.find(identity);
        if (existing != texturePaths.end()) {
            handle = projectLayer.assetManager().store<Texture>(existing->second);
            return existing->second;
        }

        const std::string texturePath = uniqueVirtualPath(
            projectLayer.project().assetsPath(),
            "textures",
            sourceName + "_" + slotName,
            ".ktx2");

        Texture::saveKtx2(*handle.get(), resourcePath(projectLayer.project().assetsPath(), texturePath).string());
        texturePaths.emplace(identity, texturePath);
        handle = projectLayer.assetManager().store<Texture>(texturePath);
        return texturePath;
    }

    void persistMaterialTextures(
        ProjectLayer &projectLayer,
        Material &material,
        const std::string &sourceName,
        std::unordered_map<const void *, std::string> &texturePaths) {
        const std::string materialBase = materialName(material, sourceName + "_Material");

        persistTexture(projectLayer, materialBase, "BaseColor", material.baseColorTexture, texturePaths);
        persistTexture(projectLayer, materialBase, "Normal", material.normalTexture, texturePaths);
        persistTexture(projectLayer, materialBase, "MetallicRoughness", material.metallicRoughnessTexture, texturePaths);
        persistTexture(projectLayer, materialBase, "Occlusion", material.occlusionTexture, texturePaths);
        persistTexture(projectLayer, materialBase, "Emissive", material.emissiveTexture, texturePaths);
    }

    void persistMesh(
        ProjectLayer &projectLayer,
        const std::string &sourceName,
        ModelComponent &model,
        uint32_t &meshIndex,
        std::unordered_map<const void *, std::string> &meshPaths) {
        if (!model.meshHandle.valid() || !model.meshHandle.get()) {
            return;
        }

        const void *identity = model.meshHandle.identity();
        const auto existing = meshPaths.find(identity);
        if (existing != meshPaths.end()) {
            model.meshHandle = projectLayer.assetManager().store<Mesh>(existing->second);
            return;
        }

        const std::string meshPath = uniqueVirtualPath(
            projectLayer.project().assetsPath(),
            "meshes",
            sourceName + "_Mesh_" + std::to_string(meshIndex++),
            ".atlasmesh");

        Mesh::saveFile(*model.meshHandle.get(), resourcePath(projectLayer.project().assetsPath(), meshPath).string());
        meshPaths.emplace(identity, meshPath);
        model.meshHandle = projectLayer.assetManager().store<Mesh>(meshPath);
    }

    void persistMaterial(
        ProjectLayer &projectLayer,
        const std::string &sourceName,
        MaterialComponent &component,
        uint32_t &materialIndex,
        std::unordered_map<const void *, std::string> &materialPaths,
        std::unordered_map<const void *, std::string> &texturePaths) {
        if (!component.materialHandle.valid() || !component.materialHandle.get()) {
            return;
        }

        const void *identity = component.materialHandle.identity();
        const auto existing = materialPaths.find(identity);
        if (existing != materialPaths.end()) {
            component.materialHandle = projectLayer.assetManager().store<Material>(existing->second);
            return;
        }

        Material *material = component.materialHandle.get();
        persistMaterialTextures(projectLayer, *material, sourceName, texturePaths);

        const std::string materialPath = uniqueVirtualPath(
            projectLayer.project().assetsPath(),
            "materials",
            materialName(*material, sourceName + "_Material_" + std::to_string(materialIndex++)),
            ".atlasmat");

        Material::saveFile(*material, resourcePath(projectLayer.project().assetsPath(), materialPath).string());
        materialPaths.emplace(identity, materialPath);
        component.materialHandle = projectLayer.assetManager().store<Material>(materialPath);
    }
}

namespace Atlas::Editor {
    std::vector<std::string> ProjectResourceImporter::supportedExtensions() {
        return {".gltf", ".glb", ".obj"};
    }

    std::vector<entt::entity> ProjectResourceImporter::importIntoProject(
        ProjectLayer &projectLayer,
        const std::string &sourcePath,
        entt::registry &registry) {
        auto importers = ProjectResourceImport::createImporters(projectLayer.assetManager());
        IAssetImporter *importer = ProjectResourceImport::findImporter(importers, sourcePath);
        if (!importer) {
            throw std::runtime_error("Unsupported import asset: " + sourcePath);
        }

        EntityBuffer buffer;
        importer->importAsset(sourcePath, buffer);

        std::vector<entt::entity> importedEntities = buffer.flush(registry);
        persistImportedResources(projectLayer, sourcePath, importedEntities, registry);
        return importedEntities;
    }

    void ProjectResourceImporter::persistImportedResources(
        ProjectLayer &projectLayer,
        const std::string &sourcePath,
        const std::vector<entt::entity> &entities,
        entt::registry &registry) {
        const std::string sourceName = ProjectResourceImport::sourceBaseName(sourcePath);

        std::unordered_map<const void *, std::string> meshPaths;
        std::unordered_map<const void *, std::string> materialPaths;
        std::unordered_map<const void *, std::string> texturePaths;
        uint32_t meshIndex = 0;
        uint32_t materialIndex = 0;

        for (const entt::entity entity: entities) {
            if (!registry.valid(entity)) {
                continue;
            }

            if (auto *model = registry.try_get<ModelComponent>(entity)) {
                ProjectResourceImport::persistMesh(projectLayer, sourceName, *model, meshIndex, meshPaths);
                registry.patch<ModelComponent>(entity);
            }
        }

        for (const entt::entity entity: entities) {
            if (!registry.valid(entity)) {
                continue;
            }

            if (auto *material = registry.try_get<MaterialComponent>(entity)) {
                ProjectResourceImport::persistMaterial(projectLayer, sourceName, *material, materialIndex, materialPaths, texturePaths);
                registry.patch<MaterialComponent>(entity);
            }
        }
    }
}
