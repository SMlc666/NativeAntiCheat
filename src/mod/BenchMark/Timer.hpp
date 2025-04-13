#pragma once
#include <chrono>
namespace native_ac {
class Timer {
    std::chrono::steady_clock::time_point start_time_;
    std::chrono::steady_clock::duration   accumulated_{0};
    bool                                  is_paused_{false};

public:
    Timer();
    void start();
    void reset();
    void pause();
    void resume();

    template <typename Duration = std::chrono::milliseconds>
    Duration elapsed() const {
        if (is_paused_) {
            return std::chrono::duration_cast<Duration>(accumulated_);
        }
        return std::chrono::duration_cast<Duration>(accumulated_ + (std::chrono::steady_clock::now() - start_time_));
    }
};
} // namespace native_ac