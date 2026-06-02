#include "Mouse.hpp"
#include "IInputProvider.hpp"
#include <cassert>

namespace Atlas {
    std::array<bool, Mouse::MOUSE_BUTTONS> Mouse::buttonPressed = {};
    double Mouse::xPos = 0;
    double Mouse::yPos = 0;
    double Mouse::scrollXOffset = 0;
    double Mouse::scrollYOffset = 0;
    bool Mouse::dragging = false;
    IInputProvider *Mouse::provider = nullptr;

    void Mouse::setProvider(IInputProvider *p) { provider = p; }

    bool Mouse::isButtonPressed(MouseCode code) {
        assert(provider != nullptr && "Call Mouse::setProvider() before querying input");
        return provider->isButtonPressed(code);
    }

    bool Mouse::isDragging() {
        assert(provider != nullptr && "Call Mouse::setProvider() before querying input");
        return provider->isDragging();
    }

    std::pair<double, double> Mouse::getCursorPosition() {
        assert(provider != nullptr && "Call Mouse::setProvider() before querying input");
        return provider->getCursorPosition();
    }

    double Mouse::getScrollX() {
        assert(provider != nullptr && "Call Mouse::setProvider() before querying input");
        return provider->getScrollX();
    }

    double Mouse::getScrollY() {
        assert(provider != nullptr && "Call Mouse::setProvider() before querying input");
        return provider->getScrollY();
    }
}
