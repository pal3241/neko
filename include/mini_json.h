#pragma once
#include <string>
#include <unordered_map>
#include <sstream>

// minimal json parser for our two use cases:
// 1. parse {"key":"pattern"} from signatures.json
// 2. parse {"key":"0xADDR"} from offsets.json
namespace mini_json {

// parse {"key": "value", ...} — values are strings
inline std::unordered_map<std::string,std::string> parse_object(const std::string& json) {
    std::unordered_map<std::string,std::string> result;
    size_t i = 0;
    auto skip = [&]() { while (i < json.size() && (json[i]==' '||json[i]=='\n'||json[i]=='\r'||json[i]=='\t')) i++; };
    auto read_str = [&]() -> std::string {
        if (i >= json.size() || json[i] != '"') return "";
        i++; // skip opening "
        std::string s;
        while (i < json.size() && json[i] != '"') {
            if (json[i] == '\\') { i++; } // skip escape
            s += json[i++];
        }
        i++; // skip closing "
        return s;
    };

    skip(); if (i < json.size() && json[i] == '{') i++;
    while (i < json.size()) {
        skip();
        if (json[i] == '}') break;
        if (json[i] == ',') { i++; continue; }
        std::string key = read_str();
        skip(); if (i < json.size() && json[i] == ':') i++;
        skip();
        std::string val = read_str();
        if (!key.empty()) result[key] = val;
    }
    return result;
}

// parse {"key": "0xHEXVALUE"} into uintptr_t map
inline std::unordered_map<std::string,uintptr_t> parse_hex_map(const std::string& json) {
    auto str_map = parse_object(json);
    std::unordered_map<std::string,uintptr_t> result;
    for (auto& [k,v] : str_map) {
        try {
            std::string hex = v;
            if (hex.substr(0,2) == "0x" || hex.substr(0,2) == "0X")
                hex = hex.substr(2);
            result[k] = static_cast<uintptr_t>(std::stoull(hex, nullptr, 16));
        } catch (...) {}
    }
    return result;
}

} // namespace mini_json
