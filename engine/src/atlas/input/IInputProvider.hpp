#pragma once
#include "Keyboard.hpp"
#include "Mouse.hpp"
#include <utility>

namespace Atlas {
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
