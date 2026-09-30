#pragma once
#include <string>
#include <unordered_map>
#include <fstream>
#include <functional>
#include "sig_scanner.h"

// simple json parser (no dependency — just what we need)
// for production swap with nlohmann/json
#include "../include/mini_json.h"

class OffsetManager {
public:
    static OffsetManager& get() {
        static OffsetManager instance;
        return instance;
    }

    bool load(const std::string& sig_path  = "offsets/signatures.json",
              const std::string& cache_path = "offsets/offsets.json") {
        sig_path_   = sig_path;
        cache_path_ = cache_path;

        // try cache first
        if (load_cache()) {
            log_("[Offsets] loaded from cache");
            return true;
        }

        // no cache — full scan
        return rescan();
    }

    // force rescan (called on roblox update)
    bool rescan() {
        log_("[Offsets] scanning signatures...");

        std::ifstream f(sig_path_);
        if (!f.is_open()) {
            log_("[Offsets] signatures.json not found");
            return false;
        }

        std::string content((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());

        auto sigs = mini_json::parse_object(content);
        bool all_ok = true;

        for (auto& [key, pattern] : sigs) {
            uintptr_t addr = SigScanner::get().find(pattern);
            if (addr == 0) {
                log_("[Offsets] FAILED: " + key + " (pattern: " + pattern + ")");
                all_ok = false;
            } else {
                offsets_[key] = addr;
                log_("[Offsets] found " + key + " @ 0x" + to_hex(addr));
            }
        }

        if (all_ok) save_cache();
        return all_ok;
    }

    uintptr_t get(const std::string& key) const {
        auto it = offsets_.find(key);
        return it != offsets_.end() ? it->second : 0;
    }

    bool has(const std::string& key) const {
        return offsets_.count(key) > 0 && offsets_.at(key) != 0;
    }

    void set_logger(std::function<void(const std::string&)> fn) { log_ = fn; }

private:
    OffsetManager() {
        log_ = [](const std::string& s) { OutputDebugStringA((s + "\n").c_str()); };
    }

    bool load_cache() {
        std::ifstream f(cache_path_);
        if (!f.is_open()) return false;

        std::string content((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());

        auto map = mini_json::parse_hex_map(content);
        if (map.empty()) return false;

        for (auto& [k, v] : map) offsets_[k] = v;
        return true;
    }

    void save_cache() {
        std::ofstream f(cache_path_);
        f << "{\n";
        size_t i = 0;
        for (auto& [k, v] : offsets_) {
            f << "  \"" << k << "\": \"0x" << to_hex(v) << "\"";
            if (++i < offsets_.size()) f << ",";
            f << "\n";
        }
        f << "}\n";
    }

    static std::string to_hex(uintptr_t v) {
        std::ostringstream ss;
        ss << std::hex << std::uppercase << v;
        return ss.str();
    }

    std::unordered_map<std::string, uintptr_t> offsets_;
    std::string sig_path_, cache_path_;
    std::function<void(const std::string&)> log_;
};
