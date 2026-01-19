#pragma once

#include <string>
#include <glm/glm.hpp>

namespace Atlas {
    class Color {
    public:
        constexpr Color() = default;
        constexpr Color(const glm::vec3 color) : color(glm::vec4(color, 1.0f)) {}
        constexpr Color(int r, int b, int g, int a) : color(r / 255.f, g / 255.f, b / 255.f, a / 255.f) {}
        constexpr Color(float r, float g, float b, float a = 1.0f) : color(r, g, b, a) {}

        Color(const std::string &hex);

        constexpr operator glm::vec4() const { return color; }

        constexpr operator glm::vec3() const { return {color}; }

        static constexpr Color white() { return Color(1.0f, 1.0f, 1.0f, 1.0f); }
        static constexpr Color black() { return Color(0.0f, 0.0f, 0.0f, 1.0f); }
        static constexpr Color gray() { return Color{75, 75, 75}; }
        static constexpr Color red() { return Color(1.0f, 0.0f, 0.0f, 1.0f); }
        static constexpr Color green() { return Color(0.0f, 1.0f, 0.0f, 1.0f); }
        static constexpr Color blue() { return Color(0.0f, 0.0f, 1.0f, 1.0f); }
        static constexpr Color transparent() { return Color(0.0f, 0.0f, 0.0f, 0.0f); }

    private:
        glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f}; // default white
    };
}
