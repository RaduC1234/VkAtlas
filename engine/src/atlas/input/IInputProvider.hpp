#pragma once
#include "Keyboard.hpp"
#include "Mouse.hpp"
#include <utility>

namespace Atlas {

// Swap this at runtime to redirect all Keyboard/Mouse queries away from GLFW.
// Implementations: GlfwInputProvider (default, implicit), PipeInputProvider, AndroidInputProvider, …
class IInputProvider {
public:
    virtual ~IInputProvider() = default;

    virtual bool isKeyPressed(KeyCode code) = 0;
    virtual bool isButtonPressed(MouseCode code) = 0;
    virtual std::pair<double, double> getCursorPosition() = 0;
    virtual bool isDragging() = 0;
    virtual double getScrollX() = 0;
    virtual double getScrollY() = 0;
};

} // namespace Atlas
