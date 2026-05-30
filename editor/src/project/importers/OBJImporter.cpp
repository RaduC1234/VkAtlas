#include "project/importers/OBJImporter.hpp"

#define GLM_ENABLE_EXPERIMENTAL
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <tiny_obj_loader.h>
#include <unordered_map>
#include <utility>

#include <glm/gtx/hash.hpp>

#include "core/Log.hpp"
#include "entity/Object.hpp"
#include "stb_image.h"

namespace Atlas::Editor::OBJImport {
    constexpr float OBJ_EPSILON = 1.0e-8f;

    struct OBJVertexHasher {
        size_t operator()(const Mesh::Vertex &v) const noexcept {
            size_t seed = 0;
            const auto combine = [&seed](const size_t value) {
                seed ^= value + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            };

            combine(std::hash<glm::vec3>{}(v.position));
            combine(std::hash<glm::vec3>{}(v.color));
            combine(std::hash<glm::vec3>{}(v.normal));
            combine(std::hash<glm::vec2>{}(v.uv));
            combine(std::hash<glm::vec4>{}(v.tangent));
            return seed;
        }
    };

    struct OBJMaterialBucket {
        int materialId = -1;
        std::unordered_map<Mesh::Vertex, uint32_t, OBJVertexHasher> uniqueVertices;
        std::vector<Mesh::Vertex> vertices;
        std::vector<uint32_t> indices;
    };

    bool objHasLength(const glm::vec3 &value) {
        return glm::dot(value, value) > OBJ_EPSILON;
    }

    bool objHasLength(const glm::vec2 &value) {
        return glm::dot(value, value) > OBJ_EPSILON;
    }

    glm::vec3 objFallbackNormal(const glm::vec3 &normal) {
        return objHasLength(normal) ? glm::normalize(normal) : glm::vec3(0.0f, 1.0f, 0.0f);
    }

    glm::vec4 objFallbackTangent(const glm::vec3 &normal) {
        const glm::vec3 n = objFallbackNormal(normal);
        const glm::vec3 helper = std::abs(n.y) < 0.999f
                                     ? glm::vec3(0.0f, 1.0f, 0.0f)
                                     : glm::vec3(1.0f, 0.0f, 0.0f);
        return glm::vec4(glm::normalize(glm::cross(helper, n)), 1.0f);
    }

    glm::vec3 objMaterialDiffuseColor(const tinyobj::material_t &material) {
        glm::vec3 diffuse{
            material.diffuse[0],
            material.diffuse[1],
            material.diffuse[2]
        };

        if (!std::isfinite(diffuse.x) || !std::isfinite(diffuse.y) || !std::isfinite(diffuse.z)) {
            return glm::vec3(0.8f);
        }

        diffuse = glm::max(diffuse, glm::vec3(0.0f));
        if (glm::dot(diffuse, diffuse) <= OBJ_EPSILON && material.diffuse_texname.empty()) {
            return glm::vec3(0.8f);
        }

        return glm::min(diffuse, glm::vec3(1.0f));
    }

    float objMaterialAlpha(const tinyobj::material_t &material) {
        if (!std::isfinite(material.dissolve)) {
            return 1.0f;
        }

        return glm::clamp(material.dissolve, 0.0f, 1.0f);
    }

    void objFinalizeVertexFrames(std::vector<Mesh::Vertex> &vertices, const std::vector<uint32_t> &indices) {
        if (vertices.empty()) {
            return;
        }

        std::vector<glm::vec3> normalSums(vertices.size(), glm::vec3(0.0f));
        std::vector<glm::vec3> tangentSums(vertices.size(), glm::vec3(0.0f));
        std::vector<glm::vec3> bitangentSums(vertices.size(), glm::vec3(0.0f));

        for (size_t i = 0; i + 2 < indices.size(); i += 3) {
            const uint32_t i0 = indices[i + 0];
            const uint32_t i1 = indices[i + 1];
            const uint32_t i2 = indices[i + 2];

            if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size()) {
                continue;
            }

            const glm::vec3 p0 = vertices[i0].position;
            const glm::vec3 p1 = vertices[i1].position;
            const glm::vec3 p2 = vertices[i2].position;
            const glm::vec3 edge1 = p1 - p0;
            const glm::vec3 edge2 = p2 - p0;

            const glm::vec3 faceNormal = glm::cross(edge1, edge2);
            if (objHasLength(faceNormal)) {
                const glm::vec3 n = glm::normalize(faceNormal);
                normalSums[i0] += n;
                normalSums[i1] += n;
                normalSums[i2] += n;
            }

            const glm::vec2 uv0 = vertices[i0].uv;
            const glm::vec2 uv1 = vertices[i1].uv;
            const glm::vec2 uv2 = vertices[i2].uv;
            const glm::vec2 deltaUv1 = uv1 - uv0;
            const glm::vec2 deltaUv2 = uv2 - uv0;
            const float determinant = deltaUv1.x * deltaUv2.y - deltaUv1.y * deltaUv2.x;

            if (std::abs(determinant) > OBJ_EPSILON && (objHasLength(deltaUv1) || objHasLength(deltaUv2))) {
                const float invDet = 1.0f / determinant;
                const glm::vec3 tangent = (edge1 * deltaUv2.y - edge2 * deltaUv1.y) * invDet;
                const glm::vec3 bitangent = (edge2 * deltaUv1.x - edge1 * deltaUv2.x) * invDet;

                tangentSums[i0] += tangent;
                tangentSums[i1] += tangent;
                tangentSums[i2] += tangent;
                bitangentSums[i0] += bitangent;
                bitangentSums[i1] += bitangent;
                bitangentSums[i2] += bitangent;
            }
        }

        for (size_t i = 0; i < vertices.size(); ++i) {
            auto &vertex = vertices[i];

            if (!objHasLength(vertex.normal)) {
                vertex.normal = objFallbackNormal(normalSums[i]);
            } else {
                vertex.normal = glm::normalize(vertex.normal);
            }

            glm::vec3 tangent = tangentSums[i];
            if (objHasLength(tangent)) {
                tangent = tangent - vertex.normal * glm::dot(vertex.normal, tangent);
                if (objHasLength(tangent)) {
                    tangent = glm::normalize(tangent);
                    const float handedness = glm::dot(glm::cross(vertex.normal, tangent), bitangentSums[i]) < 0.0f ? -1.0f : 1.0f;
                    vertex.tangent = glm::vec4(tangent, handedness);
                    continue;
                }
            }

            vertex.tangent = objFallbackTangent(vertex.normal);
        }
    }

    Mesh::Vertex objMakeVertex(const tinyobj::attrib_t &attrib, const tinyobj::index_t &index) {
        Mesh::Vertex vertex{};

        if (index.vertex_index >= 0) {
            const size_t vertexOffset = static_cast<size_t>(index.vertex_index) * 3;
            if (vertexOffset + 2 < attrib.vertices.size()) {
                vertex.position = glm::vec3(
                    attrib.vertices[vertexOffset + 0],
                    attrib.vertices[vertexOffset + 1],
                    attrib.vertices[vertexOffset + 2]);
            }

            if (!attrib.colors.empty() && vertexOffset + 2 < attrib.colors.size()) {
                vertex.color = glm::vec3(
                    attrib.colors[vertexOffset + 0],
                    attrib.colors[vertexOffset + 1],
                    attrib.colors[vertexOffset + 2]);
            } else {
                vertex.color = glm::vec3(1.0f);
            }
        }

        if (index.normal_index >= 0) {
            const size_t normalOffset = static_cast<size_t>(index.normal_index) * 3;
            if (normalOffset + 2 < attrib.normals.size()) {
                vertex.normal = glm::vec3(
                    attrib.normals[normalOffset + 0],
                    attrib.normals[normalOffset + 1],
                    attrib.normals[normalOffset + 2]);
            }
        }

        if (index.texcoord_index >= 0) {
            const size_t texcoordOffset = static_cast<size_t>(index.texcoord_index) * 2;
            if (texcoordOffset + 1 < attrib.texcoords.size()) {
                vertex.uv = glm::vec2(
                    attrib.texcoords[texcoordOffset + 0],
                    attrib.texcoords[texcoordOffset + 1]);
            }
        }

        vertex.tangent = objFallbackTangent(vertex.normal);
        return vertex;
    }

    void objAppendVertex(
        const Mesh::Vertex &vertex,
        std::vector<Mesh::Vertex> &vertices,
        std::vector<uint32_t> &indices,
        std::unordered_map<Mesh::Vertex, uint32_t, OBJVertexHasher> &uniqueVertices) {
        auto it = uniqueVertices.find(vertex);
        if (it == uniqueVertices.end()) {
            const uint32_t index = static_cast<uint32_t>(vertices.size());
            it = uniqueVertices.emplace(vertex, index).first;
            vertices.push_back(vertex);
        }

        indices.push_back(it->second);
    }

    AssetHandle<Texture> objLoadTexture(AssetManager &assets, const std::filesystem::path &virtualPath, const VkFormat format) {
        const std::filesystem::path fullPath = assets.rootPath() / virtualPath;

        int width = 0;
        int height = 0;
        int channels = 0;
        stbi_uc *pixels = stbi_load(fullPath.string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
        if (!pixels) {
            AT_WARN("OBJImporter: failed to load texture {}: {}", virtualPath.generic_string(), stbi_failure_reason());
            return AssetHandle<Texture>::invalid();
        }

        const size_t size = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
        const auto *begin = reinterpret_cast<const std::byte *>(pixels);
        std::vector<std::byte> bytes(begin, begin + size);
        stbi_image_free(pixels);

        return assets.store<Texture>(
            std::make_shared<Texture>(bytes, static_cast<uint32_t>(width), static_cast<uint32_t>(height), format, VK_SAMPLER_ADDRESS_MODE_REPEAT),
            virtualPath.generic_string());
    }

    OBJMaterialBucket &objBucketForMaterial(
        std::vector<OBJMaterialBucket> &buckets,
        std::unordered_map<int, size_t> &materialToBucket,
        const int materialId) {
        const auto it = materialToBucket.find(materialId);
        if (it != materialToBucket.end()) {
            return buckets[it->second];
        }

        const size_t bucketIndex = buckets.size();
        materialToBucket.emplace(materialId, bucketIndex);
        buckets.push_back(OBJMaterialBucket{materialId});
        return buckets.back();
    }

}

namespace Atlas::Editor {
    using namespace OBJImport;

    OBJImporter::OBJImporter(AssetManager &assets) : assets(assets) {
    }

    void OBJImporter::importAsset(
        const std::string &path,
        EntityBuffer &buffer) {
        const std::filesystem::path fullPath = assets.rootPath() / path;
        const std::string materialBaseDir = fullPath.parent_path().string();

        tinyobj::attrib_t attrib;
        std::vector<tinyobj::shape_t> shapes;
        std::vector<tinyobj::material_t> materials;
        std::string warn;
        std::string err;

        if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, fullPath.string().c_str(), materialBaseDir.c_str())) {
            AT_ERROR("OBJImporter: failed to load {}: {}", path, warn + err);
            return;
        }

        if (!warn.empty()) {
            AT_WARN("OBJImporter: {}: {}", path, warn);
        }

        const std::filesystem::path virtualDir = std::filesystem::path(path).parent_path();

        std::vector<AssetHandle<Texture>> albedoHandles(materials.size());
        std::vector<AssetHandle<Texture>> normalHandles(materials.size());
        std::vector<AssetHandle<Texture>> metallicHandles(materials.size());
        std::vector<AssetHandle<Texture>> roughnessHandles(materials.size());
        std::vector<AssetHandle<Texture>> ambientHandles(materials.size());

        for (size_t matIdx = 0; matIdx < materials.size(); ++matIdx) {
            const auto &mat = materials[matIdx];

            if (!mat.diffuse_texname.empty()) {
                albedoHandles[matIdx] = objLoadTexture(assets, virtualDir / mat.diffuse_texname, VK_FORMAT_R8G8B8A8_SRGB);
            }

            const std::string &normalName = !mat.normal_texname.empty() ? mat.normal_texname : mat.bump_texname;
            if (!normalName.empty()) {
                normalHandles[matIdx] = objLoadTexture(assets, virtualDir / normalName, VK_FORMAT_R8G8B8A8_UNORM);
            }

            if (!mat.metallic_texname.empty()) {
                metallicHandles[matIdx] = objLoadTexture(assets, virtualDir / mat.metallic_texname, VK_FORMAT_R8G8B8A8_UNORM);
            }

            if (!mat.roughness_texname.empty()) {
                roughnessHandles[matIdx] = objLoadTexture(assets, virtualDir / mat.roughness_texname, VK_FORMAT_R8G8B8A8_UNORM);
            }

            if (!mat.ambient_texname.empty()) {
                ambientHandles[matIdx] = objLoadTexture(assets, virtualDir / mat.ambient_texname, VK_FORMAT_R8G8B8A8_UNORM);
            }
        }

        size_t stagedEntityCount = 0;

        for (size_t shapeIdx = 0; shapeIdx < shapes.size(); ++shapeIdx) {
            const auto &shape = shapes[shapeIdx];

            std::vector<OBJMaterialBucket> buckets;
            std::unordered_map<int, size_t> materialToBucket;

            size_t indexOffset = 0;
            for (size_t faceIdx = 0; faceIdx < shape.mesh.num_face_vertices.size(); ++faceIdx) {
                const size_t faceVertexCount = shape.mesh.num_face_vertices[faceIdx];
                if (faceVertexCount < 3 || indexOffset + faceVertexCount > shape.mesh.indices.size()) {
                    indexOffset += faceVertexCount;
                    continue;
                }

                const int materialId = faceIdx < shape.mesh.material_ids.size() ? shape.mesh.material_ids[faceIdx] : -1;
                auto &bucket = objBucketForMaterial(buckets, materialToBucket, materialId);

                std::vector<Mesh::Vertex> faceVertices;
                faceVertices.reserve(faceVertexCount);
                for (size_t vertexIdx = 0; vertexIdx < faceVertexCount; ++vertexIdx) {
                    faceVertices.push_back(objMakeVertex(attrib, shape.mesh.indices[indexOffset + vertexIdx]));
                }

                for (size_t tri = 1; tri + 1 < faceVertices.size(); ++tri) {
                    objAppendVertex(faceVertices[0], bucket.vertices, bucket.indices, bucket.uniqueVertices);
                    objAppendVertex(faceVertices[tri], bucket.vertices, bucket.indices, bucket.uniqueVertices);
                    objAppendVertex(faceVertices[tri + 1], bucket.vertices, bucket.indices, bucket.uniqueVertices);
                }

                indexOffset += faceVertexCount;
            }

            for (auto &bucket: buckets) {
                if (bucket.vertices.empty()) {
                    continue;
                }

                objFinalizeVertexFrames(bucket.vertices, bucket.indices);
                const std::string meshPath = std::filesystem::path(path).generic_string() +
                                             "#shape/" + std::to_string(shapeIdx) +
                                             "/material/" + std::to_string(bucket.materialId);
                AssetHandle<Mesh> meshHandle = assets.store<Mesh>(
                    std::make_shared<Mesh>(bucket.vertices, bucket.indices),
                    meshPath);

                const bool hasMultipleMaterials = buckets.size() > 1;

                SceneNodeComponent node{};
                node.name = shape.name.empty() ? ("Shape_" + std::to_string(shapeIdx)) : shape.name;
                if (hasMultipleMaterials) {
                    node.name += "_mat" + std::to_string(bucket.materialId);
                }
                node.parent = entt::null;
                buffer.add(node);

                TransformComponent transform{};
                transform.translation = glm::vec3(0.0f);
                transform.rotation = glm::vec3(0.0f);
                transform.scale = glm::vec3(1.0f);
                buffer.add(transform);

                ModelComponent model{};
                model.meshHandle = meshHandle;
                buffer.add(model);

                MaterialComponent material{};
                auto materialAsset = std::make_shared<Material>();
                if (bucket.materialId >= 0 && bucket.materialId < static_cast<int>(materials.size())) {
                    const auto &mat = materials[bucket.materialId];
                    materialAsset->name = mat.name.empty() ? "Material_" + std::to_string(bucket.materialId) : mat.name;
                    materialAsset->baseColor = glm::vec4(objMaterialDiffuseColor(mat), objMaterialAlpha(mat));
                    materialAsset->baseColorTexture = albedoHandles[bucket.materialId];
                    materialAsset->normalTexture = normalHandles[bucket.materialId];
                    materialAsset->metallicRoughnessTexture = metallicHandles[bucket.materialId] ? metallicHandles[bucket.materialId] : roughnessHandles[bucket.materialId];
                    materialAsset->occlusionTexture = ambientHandles[bucket.materialId];
                    if (mat.dissolve < 1.0f) {
                        materialAsset->alphaMode = AlphaMode::BLEND;
                    }
                } else {
                    materialAsset->name = "Material";
                }
                const std::string materialPath = path + "/material/" + std::to_string(bucket.materialId);
                material.materialHandle = assets.store<Material>(std::move(materialAsset), materialPath);
                buffer.add(material);
                buffer.next();

                ++stagedEntityCount;
            }
        }

        AT_INFO("OBJImporter: staged {} entities from {}", stagedEntityCount, path);
    }
}
