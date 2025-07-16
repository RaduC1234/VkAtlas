#include "Keyboard.hpp"

namespace Atlas {
    std::vector<bool> Keyboard::keyPressed(349, false);
    std::queue<uint32_t> Keyboard::keyTyped;
    bool Keyboard::nativeInput{false};

    bool Keyboard::isKeyPressed(KeyCode code) {
        if (code < keyPressed.size()) {
            return keyPressed[code];
        }
        return false;
    }
}
