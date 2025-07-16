#include "Mouse.hpp"

namespace Atlas {
    std::array<bool, Mouse::MOUSE_BUTTONS> Mouse::buttonPressed = {};
    double Mouse::xPos = 0;
    double Mouse::yPos = 0;
    double Mouse::scrollXOffset = 0;
    double Mouse::scrollYOffset = 0;
    bool Mouse::dragging = false;

    bool Mouse::isButtonPressed(MouseCode code) {
        return buttonPressed[code];
    }

    bool Mouse::isDragging() {
        return dragging;
    }

    std::pair<double, double> Mouse::getCursorPosition() {
        return std::make_pair(xPos, yPos);
    }
}
