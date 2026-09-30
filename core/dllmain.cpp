#include <Windows.h>
#include <thread>
#include <memory>

#include "lua_state.h"
#include "pipe_server.h"
#include "module_loader.h"
#include "../offsets/offset_manager.h"
#include "../offsets/sig_scanner.h"
#include "../updater/version_check.h"

// modules — include them all here
// to add a new module: create modules/yourmodule.cpp, add it below
#include "../modules/console.cpp"
#include "../modules/http.cpp"
#include "../modules/loadstring.cpp"

// -------------------------------------------------------
// shared logger — sends to both pipe and debug output
// -------------------------------------------------------
static void logger(const std::string& msg) {
    OutputDebugStringA((msg + "\n").c_str());
    PipeServer::get().send_console("{\"type\":\"status\",\"msg\":\"" + msg + "\"}");
}

// -------------------------------------------------------
// main executor thread
// -------------------------------------------------------
static void executor_main() {
    // give roblox time to finish initializing
    Sleep(2000);

    // wire up logger everywhere
    auto log = logger;
    OffsetManager::get().set_logger(log);
    LuaStateManager::get().set_logger(log);
    PipeServer::get().set_logger(log);
    VersionCheck::get().set_logger(log);
    ModuleLoader::get().set_logger(log);

    log("[Naiko] executor starting...");

    // 1. start pipe server so UI can connect immediately
    PipeServer::get().start();

    // 2. scan roblox binary for offsets
    SigScanner::get().set_module();
    bool offsets_ok = OffsetManager::get().load();
    if (!offsets_ok) {
        log("[Naiko] WARNING: some offsets failed — executor may be degraded");
    }

    // 3. grab lua state + elevate identity
    bool lua_ok = LuaStateManager::get().init();
    if (!lua_ok) {
        log("[Naiko] FATAL: could not acquire lua state");
        return;
    }

    // 4. register modules
    ModuleLoader::get().register_module(std::make_shared<ConsoleModule>());
    ModuleLoader::get().register_module(std::make_shared<HttpModule>());
    ModuleLoader::get().register_module(std::make_shared<LoadstringModule>());

    // init all modules against our lua state
    ModuleLoader::get().init_all(LuaStateManager::get().state());

    // 5. start version watcher — auto-rescan on roblox update
    VersionCheck::get().start([](const std::string& ver) {
        logger("[Naiko] roblox updated to: " + ver);
        logger("[Naiko] offsets rescanned — modules notified");
    });

    // 6. wire up execute handler from UI
    PipeServer::get().on_execute([](const std::string& script) {
        logger("[Naiko] executing script (" + std::to_string(script.size()) + " bytes)");
        bool ok = LuaStateManager::get().execute(script);
        if (!ok) logger("[Naiko] execution failed");
    });

    log("[Naiko] ready. waiting for scripts.");

    // keep thread alive
    while (true) {
        ModuleLoader::get().tick_all();
        Sleep(50);
    }
}

// -------------------------------------------------------
// DLL entry point
// -------------------------------------------------------
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        // spawn executor on its own thread so DllMain returns fast
        std::thread(executor_main).detach();
    }
    return TRUE;
}
