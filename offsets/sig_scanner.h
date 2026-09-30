#pragma once
#include <Windows.h>
#include <string>
#include <vector>
#include <optional>
#include <sstream>
#include <iomanip>

class SigScanner {
public:
    static SigScanner& get() {
        static SigScanner instance;
        return instance;
    }

    // set the base module to scan (default: RobloxPlayerBeta.exe)
    void set_module(const std::string& name = "RobloxPlayerBeta.exe") {
        HMODULE hmod = GetModuleHandleA(name.c_str());
        if (!hmod) return;

        auto* dos = reinterpret_cast<PIMAGE_DOS_HEADER>(hmod);
        auto* nt  = reinterpret_cast<PIMAGE_NT_HEADERS>(
            reinterpret_cast<uintptr_t>(hmod) + dos->e_lfanew);

        base_ = reinterpret_cast<uintptr_t>(hmod);
        size_ = nt->OptionalHeader.SizeOfImage;
    }

    // scan for IDA-style pattern e.g. "48 8B 05 ? ? ? ? 48 85 C0"
    // returns address of first byte of match, or 0
    uintptr_t find(const std::string& pattern) const {
        auto bytes = parse_pattern(pattern);
        if (bytes.empty() || base_ == 0) return 0;

        auto* data = reinterpret_cast<const uint8_t*>(base_);

        for (size_t i = 0; i < size_ - bytes.size(); i++) {
            bool match = true;
            for (size_t j = 0; j < bytes.size(); j++) {
                if (bytes[j].has_value() && data[i + j] != bytes[j].value()) {
                    match = false;
                    break;
                }
            }
            if (match) return base_ + i;
        }
        return 0;
    }

    // resolve a relative call/jmp — dereferences the 4-byte relative offset at addr+offset
    static uintptr_t resolve_rel32(uintptr_t addr, int offset = 1) {
        int32_t rel = *reinterpret_cast<int32_t*>(addr + offset);
        return addr + offset + 4 + rel;
    }

    // dereference a RIP-relative lea — common pattern in x64
    static uintptr_t resolve_rip(uintptr_t addr, int offset = 3) {
        int32_t rel = *reinterpret_cast<int32_t*>(addr + offset);
        return addr + offset + 4 + rel;
    }

    uintptr_t base() const { return base_; }
    size_t    size() const { return size_; }

private:
    SigScanner() { set_module(); }

    uintptr_t base_ = 0;
    size_t    size_ = 0;

    static std::vector<std::optional<uint8_t>> parse_pattern(const std::string& pat) {
        std::vector<std::optional<uint8_t>> result;
        std::istringstream ss(pat);
        std::string token;
        while (ss >> token) {
            if (token == "?" || token == "??") {
                result.push_back(std::nullopt);
            } else {
                result.push_back(static_cast<uint8_t>(std::stoul(token, nullptr, 16)));
            }
        }
        return result;
    }
};
