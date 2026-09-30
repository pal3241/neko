#pragma once
#include <string>
#include <functional>

// forward declare
struct lua_State;

class ModuleBase {
public:
    virtual ~ModuleBase() = default;

    // called once after injection — register your lua functions here
    virtual bool init(lua_State* L) = 0;

    // called on detach or module reload
    virtual void shutdown() = 0;

    // human-readable name shown in console
    virtual const char* name() = 0;

    // version string e.g. "1.0.0"
    virtual const char* version() = 0;

    // if false, module is skipped silently
    bool is_alive = true;

    // optional: called every tick if you need it
    virtual void tick() {}

    // optional: called when roblox version changes
    virtual void on_roblox_update() {}
};

// every module .cpp must export this
// extern "C" ModuleBase* create_module();
