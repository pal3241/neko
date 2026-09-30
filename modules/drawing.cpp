#include "../include/module_base.h"
#include "../core/pipe_server.h"
#include <Windows.h>
#include <d3d11.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <atomic>
#pragma comment(lib, "d3d11.lib")

// -------------------------------------------------------
// Drawing object types
// -------------------------------------------------------
enum class DrawType { Line, Circle, Square, Quad, Triangle, Text, Image };

struct Color4 { float r, g, b, a; };

struct DrawObject {
    DrawType type;
    bool     visible  = true;
    bool     removed  = false;
    Color4   color    = {1,1,1,1};
    float    thickness= 1.f;
    float    transparency = 0.f; // 0=opaque 1=fully transparent
    int      zindex   = 0;

    // Line
    float x1=0,y1=0,x2=0,y2=0;

    // Circle
    float cx=0,cy=0,radius=50.f;
    int   num_sides=32;
    bool  filled=false;

    // Square / Quad
    float sx=0,sy=0,sw=100,sh=100;

    // Triangle / Quad points
    float px[4]={}, py[4]={};

    // Text
    std::string text;
    std::string font = "UI";
    float font_size  = 13.f;
    bool  bold=false, italic=false, underline=false, strikethrough=false;
    bool  outline=false;
    Color4 outline_color={0,0,0,1};
    bool  center=false;

    // raw id
    int id = 0;
};

// -------------------------------------------------------
// Object registry
// -------------------------------------------------------
static std::atomic<int> s_next_id{1};
static std::unordered_map<int, std::shared_ptr<DrawObject>> s_objects;

static std::shared_ptr<DrawObject> new_object(DrawType t) {
    auto obj = std::make_shared<DrawObject>();
    obj->type = t;
    obj->id   = s_next_id++;
    s_objects[obj->id] = obj;
    return obj;
}

// -------------------------------------------------------
// Helper — push object as userdata
// We store the id as a light userdata (int cast to void*)
// -------------------------------------------------------
static void push_drawing_object(lua_State* L, int id);
static int  get_object_id(lua_State* L, int idx);

// -------------------------------------------------------
// Lua metamethods for Drawing objects
// -------------------------------------------------------
static const char* DRAWING_MT = "DrawingObject";

static int drawing_index(lua_State* L) {
    int id = get_object_id(L, 1);
    auto it = s_objects.find(id);
    if (it == s_objects.end()) { lua_pushnil(L); return 1; }
    auto& o = *it->second;
    const char* key = luaL_checkstring(L, 2);

    // common
    if (!strcmp(key,"Visible"))       { lua_pushboolean(L, o.visible);   return 1; }
    if (!strcmp(key,"ZIndex"))        { lua_pushinteger(L, o.zindex);    return 1; }
    if (!strcmp(key,"Transparency"))  { lua_pushnumber(L, o.transparency); return 1; }
    if (!strcmp(key,"Color")) {
        lua_newtable(L);
        lua_pushnumber(L, o.color.r*255); lua_setfield(L,-2,"R");
        lua_pushnumber(L, o.color.g*255); lua_setfield(L,-2,"G");
        lua_pushnumber(L, o.color.b*255); lua_setfield(L,-2,"B");
        return 1;
    }
    if (!strcmp(key,"Thickness"))     { lua_pushnumber(L, o.thickness);  return 1; }

    // line
    if (!strcmp(key,"From")) {
        lua_newtable(L);
        lua_pushnumber(L,o.x1); lua_setfield(L,-2,"X");
        lua_pushnumber(L,o.y1); lua_setfield(L,-2,"Y");
        return 1;
    }
    if (!strcmp(key,"To")) {
        lua_newtable(L);
        lua_pushnumber(L,o.x2); lua_setfield(L,-2,"X");
        lua_pushnumber(L,o.y2); lua_setfield(L,-2,"Y");
        return 1;
    }

    // circle
    if (!strcmp(key,"Position")) {
        lua_newtable(L);
        lua_pushnumber(L,o.cx); lua_setfield(L,-2,"X");
        lua_pushnumber(L,o.cy); lua_setfield(L,-2,"Y");
        return 1;
    }
    if (!strcmp(key,"Radius"))    { lua_pushnumber(L,o.radius);    return 1; }
    if (!strcmp(key,"NumSides"))  { lua_pushinteger(L,o.num_sides); return 1; }
    if (!strcmp(key,"Filled"))    { lua_pushboolean(L,o.filled);   return 1; }

    // square
    if (!strcmp(key,"Size")) {
        lua_newtable(L);
        lua_pushnumber(L,o.sw); lua_setfield(L,-2,"X");
        lua_pushnumber(L,o.sh); lua_setfield(L,-2,"Y");
        return 1;
    }

    // text
    if (!strcmp(key,"Text"))      { lua_pushstring(L,o.text.c_str()); return 1; }
    if (!strcmp(key,"Font"))      { lua_pushstring(L,o.font.c_str()); return 1; }
    if (!strcmp(key,"Size"))      { lua_pushnumber(L,o.font_size);   return 1; }
    if (!strcmp(key,"Bold"))      { lua_pushboolean(L,o.bold);       return 1; }
    if (!strcmp(key,"Italic"))    { lua_pushboolean(L,o.italic);     return 1; }
    if (!strcmp(key,"Outline"))   { lua_pushboolean(L,o.outline);    return 1; }
    if (!strcmp(key,"Center"))    { lua_pushboolean(L,o.center);     return 1; }

    // Remove method
    if (!strcmp(key,"Remove")) {
        lua_pushcfunction(L, [](lua_State* L2) -> int {
            int id2 = get_object_id(L2, 1);
            auto it2 = s_objects.find(id2);
            if (it2 != s_objects.end()) it2->second->removed = true;
            return 0;
        });
        return 1;
    }

    lua_pushnil(L);
    return 1;
}

static int drawing_newindex(lua_State* L) {
    int id = get_object_id(L, 1);
    auto it = s_objects.find(id);
    if (it == s_objects.end()) return 0;
    auto& o = *it->second;
    const char* key = luaL_checkstring(L, 2);

    if (!strcmp(key,"Visible"))      { o.visible      = lua_toboolean(L,3); return 0; }
    if (!strcmp(key,"ZIndex"))       { o.zindex       = (int)luaL_checknumber(L,3); return 0; }
    if (!strcmp(key,"Transparency")) { o.transparency = (float)luaL_checknumber(L,3); return 0; }
    if (!strcmp(key,"Thickness"))    { o.thickness    = (float)luaL_checknumber(L,3); return 0; }
    if (!strcmp(key,"Text"))         { o.text         = luaL_checkstring(L,3); return 0; }
    if (!strcmp(key,"Font"))         { o.font         = luaL_checkstring(L,3); return 0; }
    if (!strcmp(key,"Bold"))         { o.bold         = lua_toboolean(L,3); return 0; }
    if (!strcmp(key,"Italic"))       { o.italic       = lua_toboolean(L,3); return 0; }
    if (!strcmp(key,"Outline"))      { o.outline      = lua_toboolean(L,3); return 0; }
    if (!strcmp(key,"Center"))       { o.center       = lua_toboolean(L,3); return 0; }
    if (!strcmp(key,"Filled"))       { o.filled       = lua_toboolean(L,3); return 0; }
    if (!strcmp(key,"Radius"))       { o.radius       = (float)luaL_checknumber(L,3); return 0; }
    if (!strcmp(key,"NumSides"))     { o.num_sides    = (int)luaL_checknumber(L,3); return 0; }

    if (!strcmp(key,"Color")) {
        lua_getfield(L,3,"R"); o.color.r = (float)lua_tonumber(L,-1)/255.f; lua_pop(L,1);
        lua_getfield(L,3,"G"); o.color.g = (float)lua_tonumber(L,-1)/255.f; lua_pop(L,1);
        lua_getfield(L,3,"B"); o.color.b = (float)lua_tonumber(L,-1)/255.f; lua_pop(L,1);
        return 0;
    }
    if (!strcmp(key,"From")) {
        lua_getfield(L,3,"X"); o.x1=(float)lua_tonumber(L,-1); lua_pop(L,1);
        lua_getfield(L,3,"Y"); o.y1=(float)lua_tonumber(L,-1); lua_pop(L,1);
        return 0;
    }
    if (!strcmp(key,"To")) {
        lua_getfield(L,3,"X"); o.x2=(float)lua_tonumber(L,-1); lua_pop(L,1);
        lua_getfield(L,3,"Y"); o.y2=(float)lua_tonumber(L,-1); lua_pop(L,1);
        return 0;
    }
    if (!strcmp(key,"Position")) {
        lua_getfield(L,3,"X"); o.cx=(float)lua_tonumber(L,-1); lua_pop(L,1);
        lua_getfield(L,3,"Y"); o.cy=(float)lua_tonumber(L,-1); lua_pop(L,1);
        // square uses same field
        o.sx=o.cx; o.sy=o.cy;
        return 0;
    }
    if (!strcmp(key,"Size")) {
        if (lua_type(L,3)==LUA_TTABLE) {
            lua_getfield(L,3,"X"); o.sw=(float)lua_tonumber(L,-1); lua_pop(L,1);
            lua_getfield(L,3,"Y"); o.sh=(float)lua_tonumber(L,-1); lua_pop(L,1);
        } else {
            o.font_size = (float)luaL_checknumber(L,3);
        }
        return 0;
    }
    return 0;
}

static int drawing_gc(lua_State* L) {
    // light userdata — nothing to free
    return 0;
}

static void push_drawing_object(lua_State* L, int id) {
    lua_pushlightuserdata(L, reinterpret_cast<void*>(static_cast<intptr_t>(id)));
    if (luaL_newmetatable(L, DRAWING_MT)) {
        lua_pushcfunction(L, drawing_index);    lua_setfield(L,-2,"__index");
        lua_pushcfunction(L, drawing_newindex); lua_setfield(L,-2,"__newindex");
        lua_pushcfunction(L, drawing_gc);       lua_setfield(L,-2,"__gc");
    }
    lua_setmetatable(L,-2);
}

static int get_object_id(lua_State* L, int idx) {
    return static_cast<int>(reinterpret_cast<intptr_t>(lua_touserdata(L, idx)));
}

// -------------------------------------------------------
// Drawing.new(type)
// -------------------------------------------------------
static int drawing_new(lua_State* L) {
    const char* type_str = luaL_checkstring(L, 1);
    DrawType t;

    if      (!strcmp(type_str,"Line"))     t = DrawType::Line;
    else if (!strcmp(type_str,"Circle"))   t = DrawType::Circle;
    else if (!strcmp(type_str,"Square"))   t = DrawType::Square;
    else if (!strcmp(type_str,"Quad"))     t = DrawType::Quad;
    else if (!strcmp(type_str,"Triangle")) t = DrawType::Triangle;
    else if (!strcmp(type_str,"Text"))     t = DrawType::Text;
    else if (!strcmp(type_str,"Image"))    t = DrawType::Image;
    else {
        lua_pushnil(L);
        lua_pushstring(L, ("unknown Drawing type: " + std::string(type_str)).c_str());
        return 2;
    }

    auto obj = new_object(t);
    push_drawing_object(L, obj->id);
    return 1;
}

// Drawing.clear() — remove all objects
static int drawing_clear(lua_State* L) {
    s_objects.clear();
    return 0;
}

// -------------------------------------------------------
// Module
// -------------------------------------------------------
class DrawingModule : public ModuleBase {
public:
    bool init(lua_State* L) override {
        lua_newtable(L);

        lua_pushcfunction(L, drawing_new);   lua_setfield(L,-2,"new");
        lua_pushcfunction(L, drawing_clear); lua_setfield(L,-2,"clear");

        // Drawing.Fonts table
        lua_newtable(L);
        lua_pushinteger(L,0); lua_setfield(L,-2,"UI");
        lua_pushinteger(L,1); lua_setfield(L,-2,"System");
        lua_pushinteger(L,2); lua_setfield(L,-2,"Plex");
        lua_pushinteger(L,3); lua_setfield(L,-2,"Monospace");
        lua_setfield(L,-2,"Fonts");

        lua_setglobal(L, "Drawing");
        return true;
    }

    void shutdown() override { s_objects.clear(); }
    const char* name()    override { return "drawing"; }
    const char* version() override { return "1.0.0"; }

    void on_roblox_update() override { s_objects.clear(); }
};

extern "C" __declspec(dllexport) ModuleBase* create_module() {
    return new DrawingModule();
}
