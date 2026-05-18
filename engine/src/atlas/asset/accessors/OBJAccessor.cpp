#include "OBJAccessor.hpp"

#define GLM_ENABLE_EXPERIMENTAL
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <tiny_obj_loader.h>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <glm/gtx/hash.hpp>

#include "core/Log.hpp"
#include "entity/Object.hpp"
#include "stb_image.h"

namespace Atlas {
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

    std::vector<std::byte> objToBytes(const std::string &text) {
        std::vector<std::byte> bytes;
        bytes.reserve(text.size());
        for (const char ch: text) {
            bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));
        }
        return bytes;
    }

    std::string objSanitizeName(const std::string &name, const std::string &fallback) {
        const std::string &source = name.empty() ? fallback : name;
        std::string result;
        result.reserve(source.size());

        for (const char ch: source) {
            if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '-' || ch == '.') {
                result.push_back(ch);
            } else if (std::isspace(static_cast<unsigned char>(ch))) {
                result.push_back('_');
            }
        }

        return result.empty() ? fallback : result;
    }

    void objAppendEntityAndChildren(
        const entt::entity entity,
        const entt::registry &registry,
        std::vector<entt::entity> &outEntities,
        std::unordered_set<entt::id_type> &visited) {
        if (entity == entt::null || !registry.valid(entity)) {
            return;
        }

        const entt::id_type entityId = entt::to_integral(entity);
        if (!visited.insert(entityId).second) {
            return;
        }

        outEntities.push_back(entity);

        if (const auto *node = registry.try_get<SceneNodeComponent>(entity)) {
            for (const entt::entity child: node->children) {
                objAppendEntityAndChildren(child, registry, outEntities, visited);
            }
        }
    }

    AssetHandle<Texture> objLoadTexture(AssetManager &assets, const std::filesystem::path &virtualPath, const VkFormat format) {
        const std::filesystem::path fullPath = assets.rootPath() / virtualPath;

        int width = 0;
        int height = 0;
        int channels = 0;
        stbi_uc *pixels = stbi_load(fullPath.string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
        if (!pixels) {
            AT_WARN("OBJAccessor: failed to load texture {}: {}", virtualPath.generic_string(), stbi_failure_reason());
            return AssetHandle<Texture>::invalid();
        }

        const size_t size = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
        const auto *begin = reinterpret_cast<const std::byte *>(pixels);
        std::vector<std::byte> bytes(begin, begin + size);
        stbi_image_free(pixels);

        return assets.createTexture(std::move(bytes), static_cast<uint32_t>(width), static_cast<uint32_t>(height), format, VK_SAMPLER_ADDRESS_MODE_REPEAT);
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

    OBJAccessor::OBJAccessor(AssetManager &assets, ExecutorService &service) : assets(assets), executor(service) {
    }

    std::vector<entt::entity> OBJAccessor::importAsset(
        const std::string &path,
        entt::registry &registry,
        entt::entity parentEntity) {
        const std::filesystem::path fullPath = assets.rootPath() / path;
        const std::string materialBaseDir = fullPath.parent_path().string();

        tinyobj::attrib_t attrib;
        std::vector<tinyobj::shape_t> shapes;
        std::vector<tinyobj::material_t> materials;
        std::string warn;
        std::string err;

        if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, fullPath.string().c_str(), materialBaseDir.c_str())) {
            AT_ERROR("OBJAccessor: failed to load {}: {}", path, warn + err);
            return {};
        }

        if (!warn.empty()) {
            AT_WARN("OBJAccessor: {}: {}", path, warn);
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

        std::vector<entt::entity> entities;

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
                AssetHandle<Mesh> meshHandle = assets.createMesh(std::move(bucket.vertices), std::move(bucket.indices));

                const bool hasMultipleMaterials = buckets.size() > 1;
                auto entity = registry.create();

                auto &node = registry.emplace<SceneNodeComponent>(entity);
                node.name = shape.name.empty() ? ("Shape_" + std::to_string(shapeIdx)) : shape.name;
                if (hasMultipleMaterials) {
                    node.name += "_mat" + std::to_string(bucket.materialId);
                }
                node.parent = parentEntity;

                if (parentEntity != entt::null && registry.valid(parentEntity)) {
                    if (auto *parentNode = registry.try_get<SceneNodeComponent>(parentEntity)) {
                        parentNode->children.push_back(entity);
                    }
                }

                auto &transform = registry.emplace<TransformComponent>(entity);
                transform.translation = glm::vec3(0.0f);
                transform.rotation = glm::vec3(0.0f);
                transform.scale = glm::vec3(1.0f);

                auto &model = registry.emplace<ModelComponent>(entity);
                model.meshHandle = meshHandle;

                auto &material = registry.emplace<MaterialComponent>(entity);
                if (bucket.materialId >= 0 && bucket.materialId < static_cast<int>(materials.size())) {
                    const auto &mat = materials[bucket.materialId];
                    material.baseColor = glm::vec4(mat.diffuse[0], mat.diffuse[1], mat.diffuse[2], mat.dissolve);
                    material.albedoTexture = albedoHandles[bucket.materialId];
                    material.normalMap = normalHandles[bucket.materialId];
                    material.metallicRoughnessMap = metallicHandles[bucket.materialId] ? metallicHandles[bucket.materialId] : roughnessHandles[bucket.materialId];
                    material.ambientOcclusion = ambientHandles[bucket.materialId];
                    material.alphaMasked = mat.dissolve < 1.0f;
                    material.transparent = mat.dissolve < 1.0f;
                }

                entities.push_back(entity);
            }
        }

        AT_INFO("OBJAccessor: created {} entities from {}", entities.size(), path);
        return entities;
    }

    std::vector<std::byte> OBJAccessor::exportAsset(
        const std::vector<entt::entity> &entities,
        const entt::registry &registry) {
        std::ostringstream obj;
        obj << std::fixed << std::setprecision(6);
        obj << "# Atlas OBJ export\n";

        std::vector<entt::entity> exportEntities;
        std::unordered_set<entt::id_type> visited;
        for (const entt::entity entity: entities) {
            objAppendEntityAndChildren(entity, registry, exportEntities, visited);
        }

        uint32_t vertexOffset = 1;
        uint32_t texcoordOffset = 1;
        uint32_t normalOffset = 1;
        uint32_t exportedMeshes = 0;

        for (const entt::entity entity: exportEntities) {
            const auto *model = registry.try_get<ModelComponent>(entity);
            if (!model || !model->meshHandle) {
                continue;
            }

            const Mesh *mesh = model->meshHandle.get();
            if (!mesh || mesh->vertices().empty()) {
                continue;
            }

            const auto &vertices = mesh->vertices();
            const auto &indices = mesh->indices();
            const auto *transform = registry.try_get<TransformComponent>(entity);
            const auto *material = registry.try_get<MaterialComponent>(entity);
            const auto *node = registry.try_get<SceneNodeComponent>(entity);

            const std::string entityName = objSanitizeName(
                node ? node->name : "",
                "Entity_" + std::to_string(entt::to_integral(entity)));

            glm::mat4 modelMatrix(1.0f);
            glm::mat3 normalMatrix(1.0f);
            bool reverseWinding = false;
            if (transform) {
                modelMatrix = transform->mat4();
                normalMatrix = transform->normalMatrix();
                reverseWinding = glm::determinant(glm::mat3(modelMatrix)) < 0.0f;
            }

            const glm::vec4 baseColor = material ? material->baseColor : glm::vec4(1.0f);

            obj << "\n";
            obj << "o " << entityName << "\n";
            obj << "# baseColor " << baseColor.r << " " << baseColor.g << " " << baseColor.b << " " << baseColor.a << "\n";

            for (const auto &vertex: vertices) {
                const glm::vec3 position = glm::vec3(modelMatrix * glm::vec4(vertex.position, 1.0f));
                const glm::vec3 color = glm::clamp(vertex.color * glm::vec3(baseColor), glm::vec3(0.0f), glm::vec3(1.0f));
                obj << "v "
                        << position.x << " " << position.y << " " << position.z << " "
                        << color.r << " " << color.g << " " << color.b << "\n";
            }

            for (const auto &vertex: vertices) {
                obj << "vt " << vertex.uv.x << " " << vertex.uv.y << "\n";
            }

            for (const auto &vertex: vertices) {
                glm::vec3 normal = normalMatrix * objFallbackNormal(vertex.normal);
                normal = objFallbackNormal(normal);
                obj << "vn " << normal.x << " " << normal.y << " " << normal.z << "\n";
            }

            const auto writeFaceVertex = [&](const uint32_t index) {
                const uint32_t objVertex = vertexOffset + index;
                const uint32_t objTexcoord = texcoordOffset + index;
                const uint32_t objNormal = normalOffset + index;
                obj << objVertex << "/" << objTexcoord << "/" << objNormal;
            };

            if (!indices.empty()) {
                for (size_t index = 0; index + 2 < indices.size(); index += 3) {
                    obj << "f ";
                    if (reverseWinding) {
                        writeFaceVertex(indices[index + 0]);
                        obj << " ";
                        writeFaceVertex(indices[index + 2]);
                        obj << " ";
                        writeFaceVertex(indices[index + 1]);
                    } else {
                        writeFaceVertex(indices[index + 0]);
                        obj << " ";
                        writeFaceVertex(indices[index + 1]);
                        obj << " ";
                        writeFaceVertex(indices[index + 2]);
                    }
                    obj << "\n";
                }
            } else {
                for (uint32_t index = 0; index + 2 < vertices.size(); index += 3) {
                    obj << "f ";
                    if (reverseWinding) {
                        writeFaceVertex(index + 0);
                        obj << " ";
                        writeFaceVertex(index + 2);
                        obj << " ";
                        writeFaceVertex(index + 1);
                    } else {
                        writeFaceVertex(index + 0);
                        obj << " ";
                        writeFaceVertex(index + 1);
                        obj << " ";
                        writeFaceVertex(index + 2);
                    }
                    obj << "\n";
                }
            }

            vertexOffset += static_cast<uint32_t>(vertices.size());
            texcoordOffset += static_cast<uint32_t>(vertices.size());
            normalOffset += static_cast<uint32_t>(vertices.size());
            ++exportedMeshes;
        }

        AT_INFO("OBJAccessor: exported {} meshes from {} requested entities", exportedMeshes, entities.size());
        return objToBytes(obj.str());
    }
}
