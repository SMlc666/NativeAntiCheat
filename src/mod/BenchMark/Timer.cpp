#include "Timer.hpp"
namespace native_ac {
Timer::Timer() : start_time_(std::chrono::steady_clock::now()) {}

void Timer::start() {
    start_time_  = std::chrono::steady_clock::now();
    accumulated_ = std::chrono::steady_clock::duration::zero();
    is_paused_   = false;
}

void Timer::reset() { start(); }

void Timer::pause() {
    if (!is_paused_) {
        accumulated_ += std::chrono::steady_clock::now() - start_time_;
        is_paused_    = true;
    }
}

void Timer::resume() {
    if (is_paused_) {
        start_time_ = std::chrono::steady_clock::now();
        is_paused_  = false;
    }
}
} // namespace native_ac