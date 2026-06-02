#include "DesktopInputProvider.hpp"
#include "input/Keyboard.hpp"
#include "input/Mouse.hpp"

namespace Atlas {

DesktopInputProvider &DesktopInputProvider::instance() {
    static DesktopInputProvider s;
    return s;
}

bool DesktopInputProvider::isKeyPressed(KeyCode code) {
    return code < static_cast<KeyCode>(Keyboard::keyPressed.size()) && Keyboard::keyPressed[code];
}

bool DesktopInputProvider::isButtonPressed(MouseCode code) {
    return Mouse::buttonPressed[code];
}

std::pair<double, double> DesktopInputProvider::getCursorPosition() {
    return {Mouse::xPos, Mouse::yPos};
}

bool DesktopInputProvider::isDragging() {
    return Mouse::dragging;
}

double DesktopInputProvider::getScrollX() {
    return Mouse::scrollXOffset;
}

double DesktopInputProvider::getScrollY() {
    return Mouse::scrollYOffset;
}

} // namespace Atlas
