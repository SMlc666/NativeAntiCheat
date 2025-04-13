#pragma once
#include <string>
namespace native_ac {
class Module {
private:
    std::string mName;

public:
    std::string  getName();
    virtual bool load()    = 0;
    virtual bool enable()  = 0;
    virtual bool disable() = 0;
    Module(std::string name) : mName(name) {}
    virtual ~Module() {}
};
} // namespace native_ac