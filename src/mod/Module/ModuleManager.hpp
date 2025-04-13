#pragma once
#include "mod/Module/Module.hpp"
#include <functional>
#include <memory>
#include <parallel_hashmap/phmap.h>
#include <string_view>
#include <vector>


namespace native_ac {

namespace detail {
using ModuleFactory = std::function<std::unique_ptr<native_ac::Module>()>;
std::vector<ModuleFactory>& GetGlobalModuleRegistry();
struct ModuleRegistrar {
    ModuleRegistrar(ModuleFactory factory);
};
} // namespace detail

class ModuleManager {
private:
    phmap::flat_hash_map<std::string, std::unique_ptr<Module>> mModuleList;

public:
    static ModuleManager& getInstance();
    void                  forEachModule(std::function<bool(Module*)>);
    Module*               getModuleByName(std::string_view moduleName);
    bool                  addModule(std::string_view name, std::unique_ptr<Module> module);
    bool                  addModule(std::unique_ptr<Module> module);
    void                  registerModulesFromRegistry();

private:
    ModuleManager() = default;
};
} // namespace native_ac

#define REGISTER_MODULE(ModuleClass)                                                                                   \
    namespace {                                                                                                        \
    auto nac_module_factory = []() -> std::unique_ptr<native_ac::Module> { return std::make_unique<ModuleClass>(); };  \
    static native_ac::detail::ModuleRegistrar nac_registrar(nac_module_factory);                                       \
    }