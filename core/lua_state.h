#pragma once
#include <Windows.h>
#include <cstdint>
#include <functional>
#include "../offsets/offset_manager.h"

// minimal lua types we need
typedef struct lua_State lua_State;
typedef int (*lua_CFunction)(lua_State* L);

// roblox identity levels
// 0 = plugin sandbox
// 2 = local script (normal player)
// 5 = core script
// 6 = roblox internal
// 8 = full trust
#define EXECUTOR_IDENTITY 6

class LuaStateManager {
public:
    static LuaStateManager& get() {
        static LuaStateManager instance;
        return instance;
    }

    bool init() {
        auto& om = OffsetManager::get();

        if (!om.has("lua_state")) {
            log_("[LuaState] lua_state offset missing");
            return false;
        }

        // grab the global lua state pointer
        uintptr_t addr = om.get("lua_state");
        // RIP-relative dereference (x64 pattern)
        lua_state_ = *reinterpret_cast<lua_State**>(
            SigScanner::get().resolve_rip(addr));

        if (!lua_state_) {
            log_("[LuaState] lua_State* is null");
            return false;
        }

        log_("[LuaState] lua_State* = 0x" + to_hex(reinterpret_cast<uintptr_t>(lua_state_)));

        elevate_identity();
        return true;
    }

    lua_State* state() const { return lua_state_; }

    // push identity to EXECUTOR_IDENTITY so we can call protected APIs
    void elevate_identity() {
        if (!lua_state_) return;

        auto& om = OffsetManager::get();
        if (!om.has("identity_level")) {
            log_("[LuaState] identity_level offset missing — skipping elevation");
            return;
        }

        uintptr_t state_addr = reinterpret_cast<uintptr_t>(lua_state_);
        uintptr_t id_offset  = om.get("identity_level") - SigScanner::get().base();

        // write identity level into the extra field roblox uses
        *reinterpret_cast<int*>(state_addr + id_offset) = EXECUTOR_IDENTITY;
        log_("[LuaState] identity elevated to " + std::to_string(EXECUTOR_IDENTITY));
    }

    // execute a raw lua string in our elevated state
    bool execute(const std::string& script) {
        if (!lua_state_) return false;

        // we call luaL_loadbuffer + lua_pcall via roblox's own lua exports
        // roblox links lua statically so we use their exports through the DLL
        using luaL_loadbuffer_t = int(*)(lua_State*, const char*, size_t, const char*);
        using lua_pcall_t       = int(*)(lua_State*, int, int, int);

        auto load = reinterpret_cast<luaL_loadbuffer_t>(
            GetProcAddress(GetModuleHandleA("RobloxPlayerBeta.exe"), "luaL_loadbuffer"));
        auto pcall = reinterpret_cast<lua_pcall_t>(
            GetProcAddress(GetModuleHandleA("RobloxPlayerBeta.exe"), "lua_pcall"));

        if (!load || !pcall) {
            // roblox strips exports — use offsets instead
            log_("[LuaState] lua exports not found, using offset call");
            return execute_via_offsets(script);
        }

        elevate_identity(); // re-elevate before each exec
        int res = load(lua_state_, script.c_str(), script.size(), "@executor");
        if (res != 0) {
            log_("[LuaState] load error: " + std::to_string(res));
            return false;
        }
        res = pcall(lua_state_, 0, -1, 0);
        if (res != 0) {
            log_("[LuaState] pcall error: " + std::to_string(res));
            return false;
        }
        return true;
    }

    void set_logger(std::function<void(const std::string&)> fn) { log_ = fn; }

private:
    LuaStateManager() {
        log_ = [](const std::string& s) { OutputDebugStringA((s + "\n").c_str()); };
    }

    bool execute_via_offsets(const std::string& script) {
        // use OffsetManager's loadstring address directly
        // this is the fallback when lua symbols are stripped
        auto& om = OffsetManager::get();
        if (!om.has("loadstring")) {
            log_("[LuaState] loadstring offset missing");
            return false;
        }

        using loadstring_t = int(*)(lua_State*, const char*, size_t, const char*, int);
        auto ls = reinterpret_cast<loadstring_t>(om.get("loadstring"));

        elevate_identity();
        int res = ls(lua_state_, script.c_str(), script.size(), "@executor", 0);
        return res == 0;
    }

    static std::string to_hex(uintptr_t v) {
        std::ostringstream ss;
        ss << std::hex << std::uppercase << v;
        return ss.str();
    }

    lua_State* lua_state_ = nullptr;
    std::function<void(const std::string&)> log_;
};
