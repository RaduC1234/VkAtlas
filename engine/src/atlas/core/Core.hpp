#pragma once

#include <nlohmann/json.hpp>
#include <glm/glm.hpp>

#ifdef _WIN32
#if defined(AVALON_BUILD_SHARED)
#define AVALON_API __declspec(dllexport)
#elif defined(AVALON_USE_SHARED)
#define AVALON_API __declspec(dllimport)
#else
#define AVALON_API
#endif
#elif defined(__ANDROID__) || defined(__linux__)
#if defined(AVALON_BUILD_SHARED) || defined(AVALON_USE_SHARED)
#define AVALON_API __attribute__((visibility("default")))
#else
#define AVALON_API
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define AVALON_API
#else
#define AVALON_API
#endif

#ifdef __cplusplus
#define EXTERN_C extern "C"
#else
#define EXTERN_C
#endif

#ifndef BIT
#define BIT(x) (1 << (x))
#endif

namespace nlohmann {

    template <>
    struct adl_serializer<glm::vec2> {
        static void to_json(json& j, const glm::vec2& v) {
            j = json{{"x", v.x}, {"y", v.y}};
        }

        static void from_json(const json& j, glm::vec2& v) {
            j.at("x").get_to(v.x);
            j.at("y").get_to(v.y);
        }
    };

    template <>
    struct adl_serializer<glm::vec3> {
        static void to_json(json& j, const glm::vec3& v) {
            j = json{{"x", v.x}, {"y", v.y}, {"z", v.z}};
        }

        static void from_json(const json& j, glm::vec3& v) {
            j.at("x").get_to(v.x);
            j.at("y").get_to(v.y);
            j.at("z").get_to(v.z);
        }
    };

    template <>
    struct adl_serializer<glm::vec4> {
        static void to_json(json& j, const glm::vec4& v) {
            j = json{{"x", v.x}, {"y", v.y}, {"z", v.z}, {"w", v.w}};
        }

        static void from_json(const json& j, glm::vec4& v) {
            j.at("x").get_to(v.x);
            j.at("y").get_to(v.y);
            j.at("z").get_to(v.z);
            j.at("w").get_to(v.w);
        }
    };

}