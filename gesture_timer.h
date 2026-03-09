#pragma once
#include <chrono>
#include "gesture_ctl.h"

class GestureTimer {
public:
    static constexpr int HOLD_MS   = 400;
    static constexpr int REPEAT_MS = 200;

    bool update(Gesture g) {
        auto now = std::chrono::steady_clock::now();

        if (g != current) {
            current   = g;
            startTime = now;
            lastFire  = now;
            return false;
        }
        if (g == Gesture::NONE) return false;

        int elapsed       = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime).count();
        int sinceLastFire = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFire).count();

        if (elapsed >= HOLD_MS && sinceLastFire >= REPEAT_MS) {
            lastFire = now;
            return true;
        }
        return false;
    }

    Gesture current = Gesture::NONE;

private:
    std::chrono::steady_clock::time_point startTime;
    std::chrono::steady_clock::time_point lastFire;
    // ← no fired bool here
};