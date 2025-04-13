#pragma once
#include "mod/BenchMark/Timer.hpp"
#include <functional>

namespace native_ac {
class BenchMark {
private:
    std::function<void(Timer&)> mCallBack;
    Timer                       mTimer{};

public:
    BenchMark(std::function<void(Timer&)> callback);
    ~BenchMark();
};
} // namespace native_ac