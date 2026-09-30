#pragma once
#include <vector>
#include <memory>
#include <string>
#include <unordered_map>
#include <functional>
#include "../include/module_base.h"

struct lua_State;

class ModuleLoader {
public:
    static ModuleLoader& get() {
        static ModuleLoader instance;
        return instance;
    }

    // register a module manually (used at compile time)
    void register_module(std::shared_ptr<ModuleBase> mod) {
        modules_.push_back(mod);
        log_("[ModuleLoader] registered: " + std::string(mod->name()) + " v" + mod->version());
    }

    // init all registered modules against lua state
    void init_all(lua_State* L) {
        L_ = L;
        for (auto& mod : modules_) {
            if (!mod->is_alive) continue;
            try {
                bool ok = mod->init(L);
                if (ok) {
                    log_("[" + std::string(mod->name()) + "] loaded v" + mod->version());
                } else {
                    mod->is_alive = false;
                    log_("[" + std::string(mod->name()) + "] init returned false — skipping");
                }
            } catch (...) {
                mod->is_alive = false;
                log_("[" + std::string(mod->name()) + "] crashed on init — module down");
            }
        }
    }

    // called every tick — modules can hook into this
    void tick_all() {
        for (auto& mod : modules_) {
            if (!mod->is_alive) continue;
            try {
                mod->tick();
            } catch (...) {
                mod->is_alive = false;
                log_("[" + std::string(mod->name()) + "] crashed on tick — module down");
            }
        }
    }

    // called when roblox version changes
    void notify_update() {
        for (auto& mod : modules_) {
            if (!mod->is_alive) continue;
            try {
                mod->on_roblox_update();
            } catch (...) {
                // non-fatal
            }
        }
    }

    // shutdown all
    void shutdown_all() {
        for (auto& mod : modules_) {
            try { mod->shutdown(); } catch (...) {}
        }
        modules_.clear();
    }

    // revive a dead module by name (after you hot-swap the .cpp and relink)
    bool revive(const std::string& name) {
        for (auto& mod : modules_) {
            if (std::string(mod->name()) == name) {
                mod->is_alive = true;
                if (L_) mod->init(L_);
                log_("[ModuleLoader] revived: " + name);
                return true;
            }
        }
        return false;
    }

    void set_logger(std::function<void(const std::string&)> fn) { log_ = fn; }

    const std::vector<std::shared_ptr<ModuleBase>>& modules() const { return modules_; }

private:
    ModuleLoader() {
        log_ = [](const std::string& s) {
            OutputDebugStringA((s + "\n").c_str());
        };
    }

    std::vector<std::shared_ptr<ModuleBase>> modules_;
    std::function<void(const std::string&)> log_;
    lua_State* L_ = nullptr;
};
