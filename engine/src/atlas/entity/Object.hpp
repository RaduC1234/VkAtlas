#pragma once

#include <memory>

#include "renderer/Model.hpp"

#include <glm/glm.hpp>

namespace Atlas {
    struct Transform {
        glm::vec3 translation{};
        glm::vec3 scale{1.0f, 1.0f, 1.0f};
        glm::vec3 rotation{};

        // Matrix corrsponds to Translate * Ry * Rx * Rz * Scale
        // Rotations correspond to Tait-bryan angles of Y(1), X(2), Z(3)
        // https://en.wikipedia.org/wiki/Euler_angles#Rotation_matrix
        glm::mat4 mat4() {
            const float c3 = glm::cos(rotation.z);
            const float s3 = glm::sin(rotation.z);
            const float c2 = glm::cos(rotation.x);
            const float s2 = glm::sin(rotation.x);
            const float c1 = glm::cos(rotation.y);
            const float s1 = glm::sin(rotation.y);
            return glm::mat4{
                {
                    scale.x * (c1 * c3 + s1 * s2 * s3),
                    scale.x * (c2 * s3),
                    scale.x * (c1 * s2 * s3 - c3 * s1),
                    0.0f,
                },
                {
                    scale.y * (c3 * s1 * s2 - c1 * s3),
                    scale.y * (c2 * c3),
                    scale.y * (c1 * c3 * s2 + s1 * s3),
                    0.0f,
                },
                {
                    scale.z * (c2 * s1),
                    scale.z * (-s2),
                    scale.z * (c1 * c2),
                    0.0f,
                },
                {translation.x, translation.y, translation.z, 1.0f}
            };
        }
    };

    struct Color {
        glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f}; // default white

        constexpr Color() = default;
        constexpr Color(glm::vec3 color) : color(glm::vec4(color, 1.0f)) {}
        constexpr Color(int r, int b, int g, int a) : color(r / 255.f, g / 255.f, b / 255.f, a / 255.f) {}
        constexpr Color(float r, float g, float b, float a = 1.0f) : color(r, g, b, a) {}

        Color(const std::string &hex) {

            if (hex.length() != 8) {
                assert("Color hex lenght must be 8");
            }

            color = glm::vec4(
                std::stoi(hex.substr(0, 2), nullptr, 16) / 255.0f,
                std::stoi(hex.substr(2, 2), nullptr, 16) / 255.0f,
                std::stoi(hex.substr(4, 2), nullptr, 16) / 255.0f,
                std::stoi(hex.substr(6, 2), nullptr, 16) / 255.0f
            );
        }

        static constexpr Color white() { return Color(1.0f, 1.0f, 1.0f, 1.0f); }
        static constexpr Color black() { return Color(0.0f, 0.0f, 0.0f, 1.0f); }
        static constexpr Color gray() { return Color{75, 75, 75}; }
        static constexpr Color red() { return Color(1.0f, 0.0f, 0.0f, 1.0f); }
        static constexpr Color green() { return Color(0.0f, 1.0f, 0.0f, 1.0f); }
        static constexpr Color blue() { return Color(0.0f, 0.0f, 1.0f, 1.0f); }
        static constexpr Color transparent() { return Color(0.0f, 0.0f, 0.0f, 0.0f); }
    };

    struct ModelComponent {
        std::shared_ptr<Model> model;
    };
}
