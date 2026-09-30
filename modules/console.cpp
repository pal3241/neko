#include "../include/module_base.h"
#include "../core/pipe_server.h"
#include <string>
#include <sstream>
#include <ctime>

static std::string timestamp() {
    time_t t = time(nullptr);
    char buf[16];
    strftime(buf, sizeof(buf), "%H:%M:%S", localtime(&t));
    return std::string(buf);
}

static std::string collect_args(lua_State* L, const std::string& prefix) {
    std::ostringstream ss;
    ss << "[" << timestamp() << "] " << prefix;
    int n = lua_gettop(L);
    for (int i = 1; i <= n; i++) {
        if (i > 1) ss << "\t";
        switch (lua_type(L, i)) {
        case LUA_TSTRING:  ss << lua_tostring(L, i); break;
        case LUA_TNUMBER:  ss << lua_tostring(L, i); break;
        case LUA_TBOOLEAN: ss << (lua_toboolean(L, i) ? "true" : "false"); break;
        case LUA_TNIL:     ss << "nil"; break;
        default:           ss << lua_typename(L, lua_type(L, i)); break;
        }
    }
    return ss.str();
}

static int hook_print(lua_State* L) {
    std::string msg = collect_args(L, "");
    PipeServer::get().send_console("{\"type\":\"print\",\"msg\":\"" + msg + "\"}");
    return 0;
}

static int hook_warn(lua_State* L) {
    std::string msg = collect_args(L, "[WARN] ");
    PipeServer::get().send_console("{\"type\":\"warn\",\"msg\":\"" + msg + "\"}");
    return 0;
}

static int hook_error(lua_State* L) {
    std::string msg = collect_args(L, "[ERR] ");
    PipeServer::get().send_console("{\"type\":\"error\",\"msg\":\"" + msg + "\"}");
    return 0;
}

// executor-side print that always goes to UI even if roblox swallows it
static int naiko_print(lua_State* L) {
    return hook_print(L);
}

class ConsoleModule : public ModuleBase {
public:
    bool init(lua_State* L) override {
        lua_pushcfunction(L, hook_print);  lua_setglobal(L, "print");
        lua_pushcfunction(L, hook_warn);   lua_setglobal(L, "warn");
        lua_pushcfunction(L, hook_error);  lua_setglobal(L, "error");

        // executor-specific
        lua_pushcfunction(L, naiko_print); lua_setglobal(L, "rconsoleprint");

        // announce ready
        PipeServer::get().send_console(
            "{\"type\":\"status\",\"msg\":\"executor ready\"}");

        return true;
    }

    void shutdown() override {}
    const char* name()    override { return "console"; }
    const char* version() override { return "1.0.0"; }
};

extern "C" __declspec(dllexport) ModuleBase* create_module() {
    return new ConsoleModule();
}
