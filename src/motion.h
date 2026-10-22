#pragma once
#include <algorithm>

namespace shiftwheel {
// Time-based transitions can be interrupted without a jump. No background clock.
struct Transition {
    float from = 0, to = 0;
    double started = 0, duration = 0;
    float value(double now) const {
        if (duration <= 0) return to;
        const float t = static_cast<float>(std::clamp((now - started) / duration, 0.0, 1.0));
        const float remaining = 1 - t;
        return from + (to - from) * (1 - remaining * remaining * remaining);
    }
    bool active(double now) const { return from != to && now < started + duration; }
    void snap(float target) { from = to = target; duration = 0; }
    void aim(float target, double now, double milliseconds) {
        if (target == to) return;
        from = value(now); to = target; started = now; duration = milliseconds;
    }
};
}
