#include "../include/module_base.h"
#include "../core/lua_state.h"
#include <Windows.h>
#include <winhttp.h>
#include <string>
#include <sstream>
#pragma comment(lib, "winhttp.lib")

// -------------------------------------------------------
// simple WinHTTP GET — no libcurl dependency
// -------------------------------------------------------
static std::string winhttp_get(const std::string& url, int* status_out = nullptr) {
    // parse url
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256]{}, path[2048]{};
    uc.lpszHostName    = host; uc.dwHostNameLength    = 256;
    uc.lpszUrlPath     = path; uc.dwUrlPathLength     = 2048;

    std::wstring wurl(url.begin(), url.end());
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) return "";

    HINTERNET session = WinHttpOpen(L"NaikoExecutor/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, nullptr, nullptr, 0);
    if (!session) return "";

    HINTERNET conn = WinHttpConnect(session, host, uc.nPort, 0);
    if (!conn) { WinHttpCloseHandle(session); return ""; }

    DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET req = WinHttpOpenRequest(conn, L"GET", path,
        nullptr, nullptr, nullptr, flags);
    if (!req) { WinHttpCloseHandle(conn); WinHttpCloseHandle(session); return ""; }

    WinHttpSendRequest(req, nullptr, 0, nullptr, 0, 0, 0);
    WinHttpReceiveResponse(req, nullptr);

    if (status_out) {
        DWORD status = 0, size = sizeof(status);
        WinHttpQueryHeaders(req,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            nullptr, &status, &size, nullptr);
        *status_out = static_cast<int>(status);
    }

    std::string result;
    DWORD avail = 0;
    while (WinHttpQueryDataAvailable(req, &avail) && avail > 0) {
        std::string buf(avail, '\0');
        DWORD read = 0;
        WinHttpReadData(req, &buf[0], avail, &read);
        result.append(buf, 0, read);
    }

    WinHttpCloseHandle(req);
    WinHttpCloseHandle(conn);
    WinHttpCloseHandle(session);
    return result;
}

// -------------------------------------------------------
// lua bindings
// -------------------------------------------------------

// game:HttpGet(url) — scripts expect this on the game object
// we register it as a global HttpGet too for compatibility
static int lua_HttpGet(lua_State* L) {
    // HttpGet(url) or HttpGet(self, url)
    int url_idx = 1;
    // if first arg is userdata (self / game object), skip it
    // lua_type(L, 1) == LUA_TUSERDATA => url is at index 2
    if (lua_type(L, 1) != LUA_TSTRING) url_idx = 2;

    const char* url = luaL_checkstring(L, url_idx);
    int status = 0;
    std::string body = winhttp_get(url, &status);

    if (status < 200 || status >= 300) {
        lua_pushnil(L);
        lua_pushstring(L, ("HTTP error: " + std::to_string(status)).c_str());
        return 2;
    }

    lua_pushlstring(L, body.c_str(), body.size());
    return 1;
}

// http.request({Url=..., Method=..., Headers=..., Body=...})
// returns {Body=..., StatusCode=..., Success=...}
static int lua_http_request(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);

    lua_getfield(L, 1, "Url");
    const char* url = luaL_checkstring(L, -1);
    lua_pop(L, 1);

    int status = 0;
    std::string body = winhttp_get(url, &status); // GET only for now

    lua_newtable(L);
    lua_pushboolean(L, status >= 200 && status < 300);
    lua_setfield(L, -2, "Success");
    lua_pushinteger(L, status);
    lua_setfield(L, -2, "StatusCode");
    lua_pushlstring(L, body.c_str(), body.size());
    lua_setfield(L, -2, "Body");

    return 1;
}

// -------------------------------------------------------
// module
// -------------------------------------------------------
class HttpModule : public ModuleBase {
public:
    bool init(lua_State* L) override {
        // register global HttpGet
        lua_pushcfunction(L, lua_HttpGet);
        lua_setglobal(L, "HttpGet");

        // register http table
        lua_newtable(L);
        lua_pushcfunction(L, lua_http_request);
        lua_setfield(L, -2, "request");
        lua_setglobal(L, "http");

        // also hook onto the game object's HttpGet method
        // so game:HttpGet() works too
        lua_getglobal(L, "game");
        if (!lua_isnil(L, -1)) {
            lua_pushcfunction(L, lua_HttpGet);
            lua_setfield(L, -2, "HttpGet");
        }
        lua_pop(L, 1);

        return true;
    }

    void shutdown() override {}
    const char* name()    override { return "http"; }
    const char* version() override { return "1.0.0"; }
};

extern "C" __declspec(dllexport) ModuleBase* create_module() {
    return new HttpModule();
}
