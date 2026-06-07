#pragma once

#ifdef ATLAS_PLATFORM_ANDROID

#include <array>
#include <utility>

#include <game-activity/native_app_glue/android_native_app_glue.h>

#include "input/IInputProvider.hpp"
#include "input/Mouse.hpp"

namespace Atlas {
    class AndroidInputProvider final : public IInputProvider {
    public:
        static AndroidInputProvider &instance();

        // Call once per frame from AndroidWindow::pollEvents()
        void processEvents(android_app *app);

        bool isKeyPressed(KeyCode code) override { return false; }
        bool isButtonPressed(MouseCode code) override;
        std::pair<double, double> getCursorPosition() override;
        bool isDragging() override;
        double getScrollX() override { return 0.0; }
        double getScrollY() override;

    private:
        AndroidInputProvider() = default;

        bool touched_   = false;
        bool wasTouched_ = false;
        double x_ = 0.0;
        double y_ = 0.0;
        double scrollY_ = 0.0;
    };
}

#endif
