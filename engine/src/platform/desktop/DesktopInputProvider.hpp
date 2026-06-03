#pragma once
#include "../../atlas/input/IInputProvider.hpp"

namespace Atlas {

class DesktopInputProvider final : public IInputProvider {
public:
    static DesktopInputProvider &instance();

    bool isKeyPressed(KeyCode code) override;
    bool isButtonPressed(MouseCode code) override;
    std::pair<double, double> getCursorPosition() override;
    bool isDragging() override;
    double getScrollX() override;
    double getScrollY() override;

private:
    DesktopInputProvider() = default;
};

} // namespace Atlas
