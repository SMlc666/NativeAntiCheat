#pragma once
#include "ll/api/io/LoggerRegistry.h"
#include "mod/NativeAntiCheat.h"
#include <nlohmann/json.hpp>
#include <string>

namespace native_ac {
class Module {
private: // variables
    std::string mName;

protected:
    bool mIsLoaded  = false;
    bool mIsEnabled = false;
    bool mShouldEnable;


protected: // vFunctions private:
    virtual bool load()    = 0;
    virtual bool enable()  = 0;
    virtual bool disable() = 0;
    virtual void from_json(const nlohmann::json& /*j*/) {};
    virtual void to_json(nlohmann::json& /*j*/) const {};

public:
    std::shared_ptr<ll::io::Logger> mLogger;

public:
    Module(std::string name, bool default_enabled = true) : mName(name), mShouldEnable(default_enabled) {
        mLogger = ll::io::LoggerRegistry::getInstance().getOrCreate(
            fmt::format("{}::{}", NativeAntiCheat::getInstance().getSelf().getLogger().getTitle(), mName)
        );
    }
    std::string getName();
    bool        loadE();                             // loadExport
    bool        enableE();                           // enableExport
    bool        disableE();                          // disableExport
    void        from_jsonE(const nlohmann::json& j); // from_jsonExport
    void        to_jsonE(nlohmann::json& j) const;   // to_jsonExport
    bool        isEnabled() const { return mIsEnabled; }
    bool        shouldEnable() const { return mShouldEnable; }


public: // virtual functions
    virtual ~Module() {}
};
} // namespace native_ac