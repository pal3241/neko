#include "../include/module_base.h"
#include "../offsets/offset_manager.h"
#include "../offsets/sig_scanner.h"

// roblox disables loadstring in player scripts
// we re-implement it by calling luaL_loadbuffer directly
// and pushing the result function onto the stack

typedef int  (*luaL_loadbuffer_t)(lua_State*, const char*, size_t, const char*);
typedef int  (*lua_pcall_t)(lua_State*, int, int, int);
typedef void (*lua_pushcclosure_t)(lua_State*, lua_CFunction, int);
typedef int  (*lua_type_t)(lua_State*, int);
typedef const char* (*lua_tostring_t)(lua_State*, int, size_t*);

static luaL_loadbuffer_t g_loadbuffer = nullptr;

static int executor_loadstring(lua_State* L) {
    size_t len;
    const char* src = luaL_checklstring(L, 1, &len);

    // optional chunk name
    const char* chunkname = "@loadstring";
    if (lua_gettop(L) >= 2 && lua_type(L, 2) == LUA_TSTRING)
        chunkname = lua_tostring(L, 2);

    if (!g_loadbuffer) {
        lua_pushnil(L);
        lua_pushstring(L, "loadstring backend not available");
        return 2;
    }

    int res = g_loadbuffer(L, src, len, chunkname);
    if (res != 0) {
        // error message is on top of stack
        lua_pushnil(L);
        lua_insert(L, -2); // nil, errmsg
        return 2;
    }

    // function is on top of stack
    return 1;
}

class LoadstringModule : public ModuleBase {
public:
    bool init(lua_State* L) override {
        // find luaL_loadbuffer in roblox's binary
        auto& om = OffsetManager::get();
        if (om.has("loadstring")) {
            // resolve the relative call to get the actual function
            uintptr_t addr = om.get("loadstring");
            addr = SigScanner::get().resolve_rel32(addr);
            g_loadbuffer = reinterpret_cast<luaL_loadbuffer_t>(addr);
        }

        // fallback: try GetProcAddress (stripped in most builds)
        if (!g_loadbuffer) {
            g_loadbuffer = reinterpret_cast<luaL_loadbuffer_t>(
                GetProcAddress(GetModuleHandleA("RobloxPlayerBeta.exe"),
                               "luaL_loadbuffer"));
        }

        // register our loadstring into the lua environment
        lua_pushcfunction(L, executor_loadstring);
        lua_setglobal(L, "loadstring");

        // also put it in _G explicitly
        lua_getglobal(L, "_G");
        if (!lua_isnil(L, -1)) {
            lua_pushcfunction(L, executor_loadstring);
            lua_setfield(L, -2, "loadstring");
        }
        lua_pop(L, 1);

        return true;
    }

    void shutdown() override { g_loadbuffer = nullptr; }
    const char* name()    override { return "loadstring"; }
    const char* version() override { return "1.0.0"; }
};

extern "C" __declspec(dllexport) ModuleBase* create_module() {
    return new LoadstringModule();
}
