// gesture_timer.h
#pragma once
#include <chrono>
#include "gesture_ctl.h"

class GestureTimer {
public:
    // how long a gesture must be held before it fires
    static constexpr int HOLD_MS = 500;

    // call once per frame with the current gesture
    // returns true on the frame the hold threshold is crossed
    bool update(Gesture g) {
        auto now = std::chrono::steady_clock::now();

        if (g != current) {
            current   = g;
            startTime = now;
            fired     = false;
            return false;
        }

        if (!fired && g != Gesture::NONE) {
            int elapsed = std::chrono::duration_cast<std::chrono::milliseconds>
                          (now - startTime).count();
            if (elapsed >= HOLD_MS) {
                fired = true;   // only fires once per hold
                return true;
            }
        }
        return false;
    }

    Gesture current = Gesture::NONE;

private:
    std::chrono::steady_clock::time_point startTime;
    bool fired = false;
};