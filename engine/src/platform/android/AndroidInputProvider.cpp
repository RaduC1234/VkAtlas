#include "AndroidInputProvider.hpp"

#ifdef ATLAS_PLATFORM_ANDROID

#include <game-activity/GameActivity.h>

namespace Atlas {
    AndroidInputProvider &AndroidInputProvider::instance() {
        static AndroidInputProvider s;
        return s;
    }

    void AndroidInputProvider::processEvents(android_app *app) {
        scrollY_ = 0.0;
        wasTouched_ = touched_;

        android_input_buffer *buf = android_app_swap_input_buffers(app);
        if (!buf) return;

        for (uint64_t i = 0; i < buf->motionEventsCount; ++i) {
            const GameActivityMotionEvent &ev = buf->motionEvents[i];
            const int32_t action = ev.action & AMOTION_EVENT_ACTION_MASK;
            const float ex = GameActivityPointerAxes_getX(&ev.pointers[0]);
            const float ey = GameActivityPointerAxes_getY(&ev.pointers[0]);

            switch (action) {
                case AMOTION_EVENT_ACTION_DOWN:
                    touched_ = true;
                    x_ = ex;
                    y_ = ey;
                    break;
                case AMOTION_EVENT_ACTION_MOVE:
                    x_ = ex;
                    y_ = ey;
                    break;
                case AMOTION_EVENT_ACTION_UP:
                case AMOTION_EVENT_ACTION_CANCEL:
                    touched_ = false;
                    break;
                case AMOTION_EVENT_ACTION_SCROLL:
                    scrollY_ = GameActivityPointerAxes_getAxisValue(
                        &ev.pointers[0], AMOTION_EVENT_AXIS_VSCROLL);
                    break;
                default:
                    break;
            }
        }

        android_app_clear_motion_events(buf);
        android_app_clear_key_events(buf);
    }

    bool AndroidInputProvider::isButtonPressed(MouseCode code) {
        return code == Mouse::ButtonLeft && touched_;
    }

    std::pair<double, double> AndroidInputProvider::getCursorPosition() {
        return {x_, y_};
    }

    bool AndroidInputProvider::isDragging() {
        return touched_ && wasTouched_;
    }

    double AndroidInputProvider::getScrollY() {
        return scrollY_;
    }
}

#endif
