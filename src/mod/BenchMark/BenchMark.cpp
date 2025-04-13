#include "BenchMark.hpp"
#include <functional>
namespace native_ac {
BenchMark::BenchMark(std::function<void(Timer&)> callback) : mCallBack(callback) {}
BenchMark::~BenchMark() { mCallBack(mTimer); }
} // namespace native_ac