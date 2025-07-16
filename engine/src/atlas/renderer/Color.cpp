#include "Color.hpp"

namespace Atlas {
    Color::Color(const std::string &hex) {

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
}