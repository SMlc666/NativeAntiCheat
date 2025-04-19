#pragma once
#include "mod/Module/Module.hpp"
#include <functional>
#include <parallel_hashmap/phmap.h>
#include <string_view>
#include <vector>


namespace native_ac {

namespace detail {
using ModuleFactory = std::function<native_ac::Module*()>;
std::vector<ModuleFactory>& GetGlobalModuleRegistry();
struct ModuleRegistrar {
    ModuleRegistrar(ModuleFactory factory);
};
} // namespace detail

class ModuleManager {
private:
    phmap::flat_hash_map<std::string, Module*> mModuleList;

public:
    static ModuleManager& getInstance();
    void                  forEachModule(std::function<bool(Module*)>);
    Module*               getModuleByName(std::string_view moduleName);
    bool                  addModule(std::string_view name, Module* module);
    bool                  addModule(Module* module);
    void                  registerModulesFromRegistry();

private:
    ModuleManager() = default;
};
} // namespace native_ac

#define REGISTER_MODULE(ModuleClass, InstanceGetterExpr)                                                                  \
    namespace {                                                                                                           \
    /* Factory function now returns a raw pointer obtained from InstanceGetterExpr */                                     \
    auto nac_module_factory = []() -> native_ac::Module* { return InstanceGetterExpr; };                                  \
    static native_ac::detail::ModuleRegistrar nac_registrar(nac_module_factory);                                          \
    }