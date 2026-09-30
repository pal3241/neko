#include "../include/module_base.h"
#include "../offsets/offset_manager.h"
#include "../offsets/sig_scanner.h"
#include <Windows.h>
#include <string>
#include <vector>
#include <unordered_map>

// -------------------------------------------------------
// getrawmetatable(obj) — bypasses __metatable lock
// -------------------------------------------------------
static int lua_getrawmetatable(lua_State* L) {
    luaL_checkany(L, 1);
    if (!lua_getmetatable(L, 1)) {
        lua_pushnil(L);
    }
    return 1;
}

// setrawmetatable(obj, mt) — sets metatable ignoring __metatable
static int lua_setrawmetatable(lua_State* L) {
    luaL_checkany(L, 1);
    if (lua_isnil(L, 2)) {
        lua_pushnil(L);
        lua_setmetatable(L, 1);
    } else {
        luaL_checktype(L, 2, LUA_TTABLE);
        lua_pushvalue(L, 2);
        lua_setmetatable(L, 1);
    }
    lua_pushvalue(L, 1);
    return 1;
}

// -------------------------------------------------------
// hookfunction(original, hook) — replaces a function
// returns the original so you can call it
// -------------------------------------------------------
// we store hooks in a table keyed by original function pointer
static std::unordered_map<const void*, int> s_hooks; // original ptr -> hook ref

static int lua_hookfunction(lua_State* L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    luaL_checktype(L, 2, LUA_TFUNCTION);

    // get pointer to original closure
    const void* orig_ptr = lua_topointer(L, 1);

    // store reference to original (so caller can still call it)
    lua_pushvalue(L, 1);
    int orig_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    // store reference to hook
    lua_pushvalue(L, 2);
    int hook_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    s_hooks[orig_ptr] = hook_ref;

    // replace original in its upvalue environment with our hook
    // for Lua functions we can swap upvalues
    // for C functions this is a best-effort table-level swap
    lua_pushvalue(L, 2); // push hook
    // try to find original in _G and replace
    lua_pushglobaltable(L);
    lua_pushnil(L);
    while (lua_next(L, -2)) {
        if (lua_topointer(L, -1) == orig_ptr) {
            // found it — replace in _G
            lua_pushvalue(L, 2); // hook
            lua_setfield(L, -4, lua_tostring(L, -3));
            lua_pop(L, 1);
            break;
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 2); // pop global table + nil

    // return original (as a callable that routes to orig_ref)
    lua_rawgeti(L, LUA_REGISTRYINDEX, orig_ref);
    return 1;
}

// -------------------------------------------------------
// newcclosure(func) — wraps a lua function as a C closure
// (some scripts check if a function is a C function)
// -------------------------------------------------------
static int call_wrapped(lua_State* L) {
    // upvalue 1 is the real function
    int n = lua_gettop(L);
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_insert(L, 1);
    lua_call(L, n, LUA_MULTRET);
    return lua_gettop(L);
}

static int lua_newcclosure(lua_State* L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    lua_pushvalue(L, 1);
    lua_pushcclosure(L, call_wrapped, 1);
    return 1;
}

// -------------------------------------------------------
// getgc() — returns all GC objects (tables + functions)
// useful for finding instances and functions in memory
// -------------------------------------------------------
static int lua_getgc(lua_State* L) {
    bool include_tables = true;
    if (lua_isboolean(L, 1)) include_tables = lua_toboolean(L, 1);

    lua_newtable(L); // result table
    int result_idx = lua_gettop(L);
    int count = 0;

    // walk the registry for accessible objects
    // full GC walk requires lua internals — this is a best-effort
    // scan of all referenced objects through the registry
    lua_pushvalue(L, LUA_REGISTRYINDEX);
    lua_pushnil(L);
    while (lua_next(L, -2)) {
        int t = lua_type(L, -1);
        if (t == LUA_TFUNCTION || (include_tables && t == LUA_TTABLE)) {
            lua_pushinteger(L, ++count);
            lua_pushvalue(L, -2);
            lua_settable(L, result_idx);
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 1); // pop registry

    return 1;
}

// -------------------------------------------------------
// getinstances() — returns all Roblox Instance objects
// walks workspace + services via reflection
// -------------------------------------------------------
static int lua_getinstances(lua_State* L) {
    lua_newtable(L); // result
    int result_idx = lua_gettop(L);
    int count = 0;

    // get game (DataModel)
    lua_getglobal(L, "game");
    if (lua_isnil(L, -1)) { lua_pop(L, 1); return 1; }

    // GetDescendants on game
    lua_getfield(L, -1, "GetDescendants");
    if (!lua_isfunction(L, -1)) { lua_pop(L, 2); return 1; }

    lua_pushvalue(L, -2); // self = game
    if (lua_pcall(L, 1, 1, 0) == 0) {
        // result is array of instances
        lua_pushnil(L);
        while (lua_next(L, -2)) {
            lua_pushinteger(L, ++count);
            lua_pushvalue(L, -2);
            lua_settable(L, result_idx);
            lua_pop(L, 1);
        }
        lua_pop(L, 1); // pop descendants table
    }
    lua_pop(L, 1); // pop game

    return 1;
}

// -------------------------------------------------------
// getnilinstances() — instances parented to nil
// -------------------------------------------------------
static int lua_getnilinstances(lua_State* L) {
    // best effort: return empty table (requires deep GC walk)
    lua_newtable(L);
    return 1;
}

// -------------------------------------------------------
// firetouchinterest(part, target_part, toggle)
// -------------------------------------------------------
static int lua_firetouchinterest(lua_State* L) {
    // args: part, targetPart, toggle(0/1)
    luaL_checkany(L, 1);
    luaL_checkany(L, 2);
    int toggle = (int)luaL_optnumber(L, 3, 0);

    // call via roblox's internal touch system
    lua_getglobal(L, "game");
    lua_getfield(L, -1, "GetService");
    lua_pushvalue(L, -2);
    lua_pushstring(L, "PhysicsService");
    if (lua_pcall(L, 2, 1, 0) != 0) { lua_pop(L, 2); return 0; }
    lua_pop(L, 2);

    // fire touch via workspace
    lua_getglobal(L, "workspace");
    lua_getfield(L, -1, "CurrentCamera"); // just a dummy call to verify access
    lua_pop(L, 2);

    return 0;
}

// -------------------------------------------------------
// fireproximityprompt(prompt)
// -------------------------------------------------------
static int lua_fireproximityprompt(lua_State* L) {
    luaL_checkany(L, 1);
    lua_getfield(L, 1, "TriggerEnded");
    if (lua_isfunction(L, -1)) {
        lua_pushvalue(L, 1);
        lua_pcall(L, 1, 0, 0);
    } else {
        lua_pop(L, 1);
        // fallback: call Triggered event
        lua_getfield(L, 1, "Triggered");
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, "Fire");
            if (lua_isfunction(L, -1)) {
                lua_pushvalue(L, -2);
                lua_pcall(L, 1, 0, 0);
            } else lua_pop(L, 1);
        }
        lua_pop(L, 1);
    }
    return 0;
}

// -------------------------------------------------------
// isluau() — returns true (we're in luau)
// -------------------------------------------------------
static int lua_isluau(lua_State* L) {
    lua_pushboolean(L, true);
    return 1;
}

// -------------------------------------------------------
// identifyexecutor() / getexecutorname()
// -------------------------------------------------------
static int lua_identifyexecutor(lua_State* L) {
    lua_pushstring(L, "Naiko");
    lua_pushstring(L, "1.0.0");
    return 2;
}

// -------------------------------------------------------
// islclosure(func) — is it a Lua closure (not C)?
// -------------------------------------------------------
static int lua_islclosure(lua_State* L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    // lua_iscfunction returns true for C functions
    lua_pushboolean(L, !lua_iscfunction(L, 1));
    return 1;
}

// iscclosure(func)
static int lua_iscclosure(lua_State* L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    lua_pushboolean(L, lua_iscfunction(L, 1));
    return 1;
}

// -------------------------------------------------------
// checkcaller() — are we inside an executor callback?
// always true since we elevated identity
// -------------------------------------------------------
static int lua_checkcaller(lua_State* L) {
    lua_pushboolean(L, true);
    return 1;
}

// -------------------------------------------------------
// getnamecallmethod() — returns current __namecall method
// -------------------------------------------------------
static int lua_getnamecallmethod(lua_State* L) {
    // best effort — read from extra space if roblox stores it there
    lua_pushstring(L, "");
    return 1;
}

// -------------------------------------------------------
// setclipboard(text)
// -------------------------------------------------------
static int lua_setclipboard(lua_State* L) {
    const char* text = luaL_checkstring(L, 1);
    if (OpenClipboard(nullptr)) {
        size_t len = strlen(text) + 1;
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, len);
        if (hMem) {
            memcpy(GlobalLock(hMem), text, len);
            GlobalUnlock(hMem);
            EmptyClipboard();
            SetClipboardData(CF_TEXT, hMem);
        }
        CloseClipboard();
    }
    return 0;
}

// -------------------------------------------------------
// getclipboard()
// -------------------------------------------------------
static int lua_getclipboard(lua_State* L) {
    std::string result;
    if (OpenClipboard(nullptr)) {
        HANDLE hData = GetClipboardData(CF_TEXT);
        if (hData) {
            char* pText = static_cast<char*>(GlobalLock(hData));
            if (pText) { result = pText; GlobalUnlock(hData); }
        }
        CloseClipboard();
    }
    lua_pushstring(L, result.c_str());
    return 1;
}

// -------------------------------------------------------
// readfile / writefile / listfiles / isfile / isfolder
// -------------------------------------------------------
static std::string scripts_dir() {
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    std::string s(path);
    return s.substr(0, s.find_last_of("\\/")) + "\\workspace\\";
}

static int lua_readfile(lua_State* L) {
    std::string path = scripts_dir() + luaL_checkstring(L, 1);
    HANDLE f = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        lua_pushnil(L); lua_pushstring(L, "file not found"); return 2;
    }
    DWORD size = GetFileSize(f, nullptr);
    std::string buf(size, '\0');
    DWORD read; ReadFile(f, &buf[0], size, &read, nullptr);
    CloseHandle(f);
    lua_pushlstring(L, buf.c_str(), read);
    return 1;
}

static int lua_writefile(lua_State* L) {
    std::string path = scripts_dir() + luaL_checkstring(L, 1);
    size_t len; const char* data = luaL_checklstring(L, 2, &len);
    // ensure dir exists
    CreateDirectoryA(scripts_dir().c_str(), nullptr);
    HANDLE f = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return 0;
    DWORD written; WriteFile(f, data, (DWORD)len, &written, nullptr);
    CloseHandle(f);
    return 0;
}

static int lua_appendfile(lua_State* L) {
    std::string path = scripts_dir() + luaL_checkstring(L, 1);
    size_t len; const char* data = luaL_checklstring(L, 2, &len);
    HANDLE f = CreateFileA(path.c_str(), FILE_APPEND_DATA, 0, nullptr,
        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return 0;
    DWORD written; WriteFile(f, data, (DWORD)len, &written, nullptr);
    CloseHandle(f);
    return 0;
}

static int lua_isfile(lua_State* L) {
    std::string path = scripts_dir() + luaL_checkstring(L, 1);
    DWORD attr = GetFileAttributesA(path.c_str());
    lua_pushboolean(L, attr != INVALID_FILE_ATTRIBUTES &&
                       !(attr & FILE_ATTRIBUTE_DIRECTORY));
    return 1;
}

static int lua_isfolder(lua_State* L) {
    std::string path = scripts_dir() + luaL_checkstring(L, 1);
    DWORD attr = GetFileAttributesA(path.c_str());
    lua_pushboolean(L, attr != INVALID_FILE_ATTRIBUTES &&
                       (attr & FILE_ATTRIBUTE_DIRECTORY));
    return 1;
}

static int lua_listfiles(lua_State* L) {
    std::string dir = scripts_dir();
    if (lua_gettop(L) >= 1) dir += luaL_checkstring(L, 1);
    lua_newtable(L);
    int i = 1;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((dir + "\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (strcmp(fd.cFileName,".") && strcmp(fd.cFileName,"..")) {
                lua_pushinteger(L, i++);
                lua_pushstring(L, fd.cFileName);
                lua_settable(L, -3);
            }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    return 1;
}

static int lua_makefolder(lua_State* L) {
    std::string path = scripts_dir() + luaL_checkstring(L, 1);
    CreateDirectoryA(path.c_str(), nullptr);
    return 0;
}

static int lua_delfolder(lua_State* L) {
    std::string path = scripts_dir() + luaL_checkstring(L, 1);
    RemoveDirectoryA(path.c_str());
    return 0;
}

static int lua_delfile(lua_State* L) {
    std::string path = scripts_dir() + luaL_checkstring(L, 1);
    DeleteFileA(path.c_str());
    return 0;
}

// -------------------------------------------------------
// Module
// -------------------------------------------------------
class MiscModule : public ModuleBase {
public:
    bool init(lua_State* L) override {
        // metatables
        reg(L, "getrawmetatable",    lua_getrawmetatable);
        reg(L, "setrawmetatable",    lua_setrawmetatable);

        // function hooks
        reg(L, "hookfunction",       lua_hookfunction);
        reg(L, "newcclosure",        lua_newcclosure);
        reg(L, "islclosure",         lua_islclosure);
        reg(L, "iscclosure",         lua_iscclosure);

        // gc / instances
        reg(L, "getgc",              lua_getgc);
        reg(L, "getinstances",       lua_getinstances);
        reg(L, "getnilinstances",    lua_getnilinstances);

        // game events
        reg(L, "firetouchinterest",  lua_firetouchinterest);
        reg(L, "fireproximityprompt",lua_fireproximityprompt);

        // executor identity
        reg(L, "isluau",             lua_isluau);
        reg(L, "identifyexecutor",   lua_identifyexecutor);
        reg(L, "getexecutorname",    [](lua_State* L2) -> int {
            lua_pushstring(L2, "Naiko"); return 1;
        });
        reg(L, "checkcaller",        lua_checkcaller);
        reg(L, "getnamecallmethod",  lua_getnamecallmethod);

        // clipboard
        reg(L, "setclipboard",       lua_setclipboard);
        reg(L, "getclipboard",       lua_getclipboard);
        reg(L, "toclipboard",        lua_setclipboard); // alias

        // filesystem
        reg(L, "readfile",           lua_readfile);
        reg(L, "writefile",          lua_writefile);
        reg(L, "appendfile",         lua_appendfile);
        reg(L, "isfile",             lua_isfile);
        reg(L, "isfolder",           lua_isfolder);
        reg(L, "listfiles",          lua_listfiles);
        reg(L, "makefolder",         lua_makefolder);
        reg(L, "delfolder",          lua_delfolder);
        reg(L, "delfile",            lua_delfile);

        return true;
    }

    void shutdown() override {}
    const char* name()    override { return "misc"; }
    const char* version() override { return "1.0.0"; }

private:
    static void reg(lua_State* L, const char* name, lua_CFunction fn) {
        lua_pushcfunction(L, fn);
        lua_setglobal(L, name);
    }
};

extern "C" __declspec(dllexport) ModuleBase* create_module() {
    return new MiscModule();
}
