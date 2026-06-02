#pragma once

#include <cstdint>
#include <array>
#include <utility>

using MouseCode = uint16_t;

namespace Atlas {
    class IInputProvider;

    class Mouse {
    public:
        static constexpr uint32_t MOUSE_BUTTONS = 8;

        enum : MouseCode {
            Button0 = 0,
            Button1 = 1,
            Button2 = 2,
            Button3 = 3,
            Button4 = 4,
            Button5 = 5,
            Button6 = 6,
            Button7 = 7,

            ButtonLast = Button7,
            ButtonLeft = Button0,
            ButtonRight = Button1,
            ButtonMiddle = Button2
        };

        static void setProvider(IInputProvider *p);
        static bool isButtonPressed(MouseCode code);
        static bool isDragging();
        static std::pair<double, double> getCursorPosition();
        static double getScrollX();
        static double getScrollY();

    private:
        static IInputProvider *provider;
        static std::array<bool, MOUSE_BUTTONS> buttonPressed;
        static double xPos, yPos;
        static double scrollXOffset, scrollYOffset;
        static bool dragging;

        friend class DesktopInputProvider;
#ifdef _WIN32
    friend class DesktopWindow;
#endif
    };
}
