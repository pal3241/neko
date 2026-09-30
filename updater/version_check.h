#pragma once
#include <Windows.h>
#include <string>
#include <fstream>
#include <functional>
#include <thread>
#include <atomic>
#include "../offsets/offset_manager.h"
#include "../core/module_loader.h"

class VersionCheck {
public:
    static VersionCheck& get() {
        static VersionCheck instance;
        return instance;
    }

    void start(std::function<void(const std::string&)> on_update = nullptr) {
        on_update_ = on_update;
        running_   = true;

        // grab current version on start
        current_version_ = get_roblox_version();
        log_("[VersionCheck] current version: " + current_version_);
        save_version(current_version_);

        // poll in background every 30s
        watcher_ = std::thread([this]() {
            while (running_) {
                std::this_thread::sleep_for(std::chrono::seconds(30));
                check();
            }
        });
        watcher_.detach();
    }

    void stop() { running_ = false; }

    void check() {
        std::string v = get_roblox_version();
        if (v.empty() || v == current_version_) return;

        log_("[VersionCheck] roblox updated! " + current_version_ + " -> " + v);
        current_version_ = v;
        save_version(v);

        // rescan offsets against new binary
        log_("[VersionCheck] rescanning signatures...");
        bool ok = OffsetManager::get().rescan();

        if (ok) {
            log_("[VersionCheck] rescan OK — notifying modules");
            ModuleLoader::get().notify_update();
        } else {
            log_("[VersionCheck] rescan FAILED — some sigs may be broken");
            log_("[VersionCheck] check for updated signatures.json on github");
        }

        if (on_update_) on_update_(v);
    }

    const std::string& version() const { return current_version_; }

    void set_logger(std::function<void(const std::string&)> fn) { log_ = fn; }

private:
    VersionCheck() {
        log_ = [](const std::string& s) { OutputDebugStringA((s + "\n").c_str()); };
    }

    // reads version string from roblox exe resources or file
    std::string get_roblox_version() {
        // method 1: read from AppSettings.xml or version file
        char path[MAX_PATH];
        GetModuleFileNameA(GetModuleHandleA("RobloxPlayerBeta.exe"), path, MAX_PATH);
        std::string exe_path(path);

        // strip exe name, look for version file
        size_t last = exe_path.find_last_of("\\/");
        std::string dir = exe_path.substr(0, last);
        std::ifstream ver_file(dir + "\\AppSettings.xml");
        if (ver_file.is_open()) {
            std::string line;
            while (std::getline(ver_file, line)) {
                size_t start = line.find("<Version>");
                if (start != std::string::npos) {
                    start += 9;
                    size_t end = line.find("</Version>", start);
                    if (end != std::string::npos)
                        return line.substr(start, end - start);
                }
            }
        }

        // method 2: hash the exe itself
        HMODULE hmod = GetModuleHandleA("RobloxPlayerBeta.exe");
        if (!hmod) return "";

        auto* dos = reinterpret_cast<PIMAGE_DOS_HEADER>(hmod);
        auto* nt  = reinterpret_cast<PIMAGE_NT_HEADERS>(
            reinterpret_cast<uintptr_t>(hmod) + dos->e_lfanew);

        // use TimeDateStamp as version proxy — changes every build
        return std::to_string(nt->FileHeader.TimeDateStamp);
    }

    void save_version(const std::string& v) {
        std::ofstream f("offsets/last_version.txt");
        f << v;
    }

    std::string load_version() {
        std::ifstream f("offsets/last_version.txt");
        if (!f.is_open()) return "";
        return std::string(std::istreambuf_iterator<char>(f), {});
    }

    std::string current_version_;
    std::atomic<bool> running_{ false };
    std::thread watcher_;
    std::function<void(const std::string&)> on_update_;
    std::function<void(const std::string&)> log_;
};
