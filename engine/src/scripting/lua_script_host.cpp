#include <vespera/scripting/lua_script_host.hpp>

#include <vespera/assets/asset_catalog.hpp>
#include <vespera/audio/audio.hpp>
#include <vespera/core/log.hpp>
#include <vespera/core/version.hpp>
#include <vespera/input/input.hpp>
#include <vespera/scene/component_access.hpp>
#include <vespera/scene/scene.hpp>
#include <vespera/ui/ui_surface.hpp>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <initializer_list>
#include <utility>

namespace vespera {
namespace {

char kHostRegistryKey;

std::filesystem::path catalog_relative_path(const AssetCatalog& catalog, std::string_view value) {
    if (value.empty()) return {};
    std::filesystem::path path(value);
    std::error_code ec;
    if (path.is_absolute()) {
        const auto relative = std::filesystem::relative(path, catalog.root(), ec);
        if (!ec) return relative.lexically_normal();
        return path.lexically_normal();
    }
    path = path.lexically_normal();
    if (!path.empty()) {
        auto it = path.begin();
        if (it != path.end() && *it == catalog.root().filename()) {
            std::filesystem::path stripped;
            for (++it; it != path.end(); ++it) stripped /= *it;
            if (!stripped.empty()) return stripped.lexically_normal();
        }
    }
    return path;
}

struct HostContext {
    Scene* scene = nullptr;
    InputSystem* input = nullptr;
    AudioSystem* audio = nullptr;
    AssetCatalog* assets = nullptr;
    UiSurface* ui = nullptr;
    std::filesystem::path current_scene_path;
    std::optional<LuaSceneLoadRequest> scene_request;
};

HostContext* host(lua_State* state) {
    lua_pushlightuserdata(state, &kHostRegistryKey);
    lua_gettable(state, LUA_REGISTRYINDEX);
    auto* result = static_cast<HostContext*>(lua_touserdata(state, -1));
    lua_pop(state, 1);
    return result;
}

std::string_view checked_string(lua_State* state, int index) {
    std::size_t length = 0;
    const char* value = luaL_checklstring(state, index, &length);
    return {value ? value : "", length};
}

void push_vec2(lua_State* state, Vec2 value) {
    lua_createtable(state, 2, 2);
    lua_pushnumber(state, value.x); lua_rawseti(state, -2, 1);
    lua_pushnumber(state, value.z); lua_rawseti(state, -2, 2);
    lua_pushnumber(state, value.x); lua_setfield(state, -2, "x");
    lua_pushnumber(state, value.z); lua_setfield(state, -2, "y");
}

void push_vec3(lua_State* state, Vec3 value) {
    lua_createtable(state, 3, 3);
    lua_pushnumber(state, value.x); lua_rawseti(state, -2, 1);
    lua_pushnumber(state, value.z); lua_rawseti(state, -2, 2);
    lua_pushnumber(state, value.z); lua_rawseti(state, -2, 3);
    lua_pushnumber(state, value.x); lua_setfield(state, -2, "x");
    lua_pushnumber(state, value.z); lua_setfield(state, -2, "y");
    lua_pushnumber(state, value.z); lua_setfield(state, -2, "z");
}

void push_color(lua_State* state, const std::array<float, 4>& value) {
    lua_createtable(state, 4, 4);
    static constexpr const char* names[] = {"r", "g", "b", "a"};
    for (int i = 0; i < 4; ++i) {
        lua_pushnumber(state, value[static_cast<std::size_t>(i)]); lua_rawseti(state, -2, i + 1);
        lua_pushnumber(state, value[static_cast<std::size_t>(i)]); lua_setfield(state, -2, names[i]);
    }
}

float table_number(lua_State* state, int index, const char* field, int array_index) {
    index = lua_absindex(state, index);
    lua_getfield(state, index, field);
    if (lua_isnumber(state, -1)) {
        const float result = static_cast<float>(lua_tonumber(state, -1));
        lua_pop(state, 1);
        return result;
    }
    lua_pop(state, 1);
    lua_rawgeti(state, index, array_index);
    const float result = static_cast<float>(luaL_checknumber(state, -1));
    lua_pop(state, 1);
    return result;
}

std::optional<BuiltinPropertyType> property_type(std::string_view component_key, std::string_view property_key) {
    const auto* component = builtin_component_info(component_key);
    if (!component) return std::nullopt;
    for (const auto& property : builtin_component_properties(component->type)) {
        if (property.key == property_key) return property.type;
    }
    return std::nullopt;
}

int l_log_info(lua_State* state) { log::info(std::string(checked_string(state, 1))); return 0; }
int l_log_warn(lua_State* state) { log::warn(std::string(checked_string(state, 1))); return 0; }
int l_log_error(lua_State* state) { log::error(std::string(checked_string(state, 1))); return 0; }

int l_input_value(lua_State* state) {
    auto* ctx = host(state); if (!ctx || !ctx->input) { lua_pushnumber(state, 0); return 1; }
    lua_pushnumber(state, ctx->input->action_value(checked_string(state, 1))); return 1;
}
int l_input_down(lua_State* state) {
    auto* ctx = host(state); lua_pushboolean(state, ctx && ctx->input && ctx->input->action_down(checked_string(state, 1))); return 1;
}
int l_input_pressed(lua_State* state) {
    auto* ctx = host(state); lua_pushboolean(state, ctx && ctx->input && ctx->input->action_pressed(checked_string(state, 1))); return 1;
}
int l_input_released(lua_State* state) {
    auto* ctx = host(state); lua_pushboolean(state, ctx && ctx->input && ctx->input->action_released(checked_string(state, 1))); return 1;
}

int l_scene_find(lua_State* state) {
    auto* ctx = host(state); Entity* entity = ctx && ctx->scene ? ctx->scene->find_entity_by_name(checked_string(state, 1)) : nullptr;
    if (!entity) lua_pushnil(state); else lua_pushinteger(state, static_cast<lua_Integer>(entity->id));
    return 1;
}
int l_scene_find_tag(lua_State* state) {
    auto* ctx = host(state); Entity* entity = ctx && ctx->scene ? ctx->scene->find_entity_with_tag(checked_string(state, 1)) : nullptr;
    if (!entity) lua_pushnil(state); else lua_pushinteger(state, static_cast<lua_Integer>(entity->id));
    return 1;
}
int l_scene_current(lua_State* state) {
    auto* ctx = host(state); if (!ctx) { lua_pushnil(state); return 1; }
    const auto text = ctx->current_scene_path.generic_string(); lua_pushlstring(state, text.data(), text.size()); return 1;
}
int l_scene_load(lua_State* state) {
    auto* ctx = host(state); if (!ctx) { lua_pushboolean(state, false); return 1; }
    const auto path = checked_string(state, 1);
    if (path.empty()) { lua_pushboolean(state, false); return 1; }
    ctx->scene_request = LuaSceneLoadRequest{std::filesystem::path(std::string(path)), false};
    lua_pushboolean(state, true); return 1;
}
int l_scene_reload(lua_State* state) {
    auto* ctx = host(state); if (!ctx || ctx->current_scene_path.empty()) { lua_pushboolean(state, false); return 1; }
    ctx->scene_request = LuaSceneLoadRequest{ctx->current_scene_path, true};
    lua_pushboolean(state, true); return 1;
}

Entity* checked_entity(lua_State* state, int index = 1) {
    auto* ctx = host(state); if (!ctx || !ctx->scene) return nullptr;
    const auto id = static_cast<SceneObjectId>(luaL_checkinteger(state, index));
    return ctx->scene->find_entity(id);
}
int l_entity_exists(lua_State* state) { lua_pushboolean(state, checked_entity(state) != nullptr); return 1; }
int l_entity_name(lua_State* state) {
    auto* e = checked_entity(state); if (!e) { lua_pushnil(state); return 1; }
    lua_pushlstring(state, e->name.data(), e->name.size()); return 1;
}
int l_entity_set_name(lua_State* state) {
    auto* e = checked_entity(state); if (!e) { lua_pushboolean(state, false); return 1; }
    e->name = std::string(checked_string(state, 2)); lua_pushboolean(state, true); return 1;
}
int l_entity_enabled(lua_State* state) { auto* e = checked_entity(state); lua_pushboolean(state, e && e->enabled); return 1; }
int l_entity_set_enabled(lua_State* state) {
    auto* e = checked_entity(state); if (!e) { lua_pushboolean(state, false); return 1; }
    e->enabled = lua_toboolean(state, 2) != 0; lua_pushboolean(state, true); return 1;
}
int l_entity_position(lua_State* state) { auto* e = checked_entity(state); if (!e) { lua_pushnil(state); return 1; } push_vec3(state, e->transform.position); return 1; }
int l_entity_set_position(lua_State* state) {
    auto* e = checked_entity(state); if (!e) { lua_pushboolean(state, false); return 1; }
    e->transform.position = {static_cast<float>(luaL_checknumber(state, 2)), static_cast<float>(luaL_checknumber(state, 3)), static_cast<float>(luaL_checknumber(state, 4))};
    lua_pushboolean(state, true); return 1;
}
int l_entity_rotation(lua_State* state) { auto* e = checked_entity(state); if (!e) { lua_pushnil(state); return 1; } push_vec3(state, e->transform.rotation); return 1; }
int l_entity_set_rotation(lua_State* state) {
    auto* e = checked_entity(state); if (!e) { lua_pushboolean(state, false); return 1; }
    e->transform.rotation = {static_cast<float>(luaL_checknumber(state, 2)), static_cast<float>(luaL_checknumber(state, 3)), static_cast<float>(luaL_checknumber(state, 4))};
    lua_pushboolean(state, true); return 1;
}
int l_entity_scale(lua_State* state) { auto* e = checked_entity(state); if (!e) { lua_pushnil(state); return 1; } push_vec3(state, e->transform.scale); return 1; }
int l_entity_set_scale(lua_State* state) {
    auto* e = checked_entity(state); if (!e) { lua_pushboolean(state, false); return 1; }
    e->transform.scale = {static_cast<float>(luaL_checknumber(state, 2)), static_cast<float>(luaL_checknumber(state, 3)), static_cast<float>(luaL_checknumber(state, 4))};
    lua_pushboolean(state, true); return 1;
}
int l_entity_get_property(lua_State* state) {
    auto* e = checked_entity(state); if (!e) { lua_pushnil(state); return 1; }
    const auto value = get_builtin_component_property(*e, checked_string(state, 2), checked_string(state, 3));
    if (!value) { lua_pushnil(state); return 1; }
    std::visit([&](const auto& item) {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, bool>) lua_pushboolean(state, item);
        else if constexpr (std::is_same_v<T, float>) lua_pushnumber(state, item);
        else if constexpr (std::is_same_v<T, std::string>) lua_pushlstring(state, item.data(), item.size());
        else if constexpr (std::is_same_v<T, Vec2>) push_vec2(state, item);
        else if constexpr (std::is_same_v<T, Vec3>) push_vec3(state, item);
        else if constexpr (std::is_same_v<T, std::array<float, 4>>) push_color(state, item);
        else if constexpr (std::is_same_v<T, TextureId>) lua_pushinteger(state, static_cast<lua_Integer>(item));
    }, *value);
    return 1;
}
int l_entity_set_property(lua_State* state) {
    auto* e = checked_entity(state); if (!e) { lua_pushboolean(state, false); return 1; }
    const std::string component(checked_string(state, 2));
    const std::string property(checked_string(state, 3));
    const auto type = property_type(component, property);
    if (!type) { lua_pushboolean(state, false); return 1; }
    BuiltinPropertyValue value;
    switch (*type) {
        case BuiltinPropertyType::Bool: value = lua_toboolean(state, 4) != 0; break;
        case BuiltinPropertyType::Float: value = static_cast<float>(luaL_checknumber(state, 4)); break;
        case BuiltinPropertyType::String: value = std::string(checked_string(state, 4)); break;
        case BuiltinPropertyType::Texture: value = static_cast<TextureId>(luaL_checkinteger(state, 4)); break;
        case BuiltinPropertyType::Vec2:
            luaL_checktype(state, 4, LUA_TTABLE);
            value = Vec2{table_number(state, 4, "x", 1), table_number(state, 4, "y", 2)}; break;
        case BuiltinPropertyType::Vec3:
            luaL_checktype(state, 4, LUA_TTABLE);
            value = Vec3{table_number(state, 4, "x", 1), table_number(state, 4, "y", 2), table_number(state, 4, "z", 3)}; break;
        case BuiltinPropertyType::Color4:
            luaL_checktype(state, 4, LUA_TTABLE);
            value = std::array<float,4>{table_number(state, 4, "r", 1), table_number(state, 4, "g", 2), table_number(state, 4, "b", 3), table_number(state, 4, "a", 4)}; break;
    }
    lua_pushboolean(state, set_builtin_component_property(*e, component, property, value)); return 1;
}

int l_ui_exists(lua_State* state) { auto* ctx=host(state); lua_pushboolean(state, ctx && ctx->ui && ctx->ui->exists(checked_string(state,1))); return 1; }
int l_ui_text(lua_State* state) { auto* ctx=host(state); if(!ctx||!ctx->ui){lua_pushnil(state);return 1;} auto v=ctx->ui->text(checked_string(state,1)); lua_pushlstring(state,v.data(),v.size()); return 1; }
int l_ui_set_text(lua_State* state) { auto* ctx=host(state); lua_pushboolean(state, ctx&&ctx->ui&&ctx->ui->set_text(checked_string(state,1),checked_string(state,2))); return 1; }
int l_ui_value(lua_State* state) { auto* ctx=host(state); if(!ctx||!ctx->ui){lua_pushnil(state);return 1;} auto v=ctx->ui->value(checked_string(state,1)); lua_pushlstring(state,v.data(),v.size()); return 1; }
int l_ui_set_value(lua_State* state) { auto* ctx=host(state); lua_pushboolean(state, ctx&&ctx->ui&&ctx->ui->set_value(checked_string(state,1),checked_string(state,2))); return 1; }
int l_ui_set_visible(lua_State* state) { auto* ctx=host(state); lua_pushboolean(state, ctx&&ctx->ui&&ctx->ui->set_visible(checked_string(state,1),lua_toboolean(state,2)!=0)); return 1; }
int l_ui_set_disabled(lua_State* state) { auto* ctx=host(state); lua_pushboolean(state, ctx&&ctx->ui&&ctx->ui->set_disabled(checked_string(state,1),lua_toboolean(state,2)!=0)); return 1; }
int l_ui_set_property(lua_State* state) { auto* ctx=host(state); lua_pushboolean(state, ctx&&ctx->ui&&ctx->ui->set_property(checked_string(state,1),checked_string(state,2),checked_string(state,3))); return 1; }
int l_ui_set_class(lua_State* state) { auto* ctx=host(state); lua_pushboolean(state, ctx&&ctx->ui&&ctx->ui->set_class(checked_string(state,1),checked_string(state,2),lua_toboolean(state,3)!=0)); return 1; }
int l_ui_click(lua_State* state) { auto* ctx=host(state); lua_pushboolean(state, ctx&&ctx->ui&&ctx->ui->click(checked_string(state,1))); return 1; }
int l_ui_consume_clicks(lua_State* state) { auto* ctx=host(state); lua_pushinteger(state, ctx&&ctx->ui ? ctx->ui->consume_clicks(checked_string(state,1)) : 0); return 1; }

const AssetRecord* resolve_asset(HostContext* ctx, std::string_view value) {
    if (!ctx || !ctx->assets || value.empty()) return nullptr;
    if (const auto* by_id = ctx->assets->find_by_id(value)) return by_id;
    const auto relative = catalog_relative_path(*ctx->assets, value);
    return ctx->assets->find(relative.generic_string());
}
int l_assets_resolve(lua_State* state) {
    auto* ctx=host(state); const auto* record=resolve_asset(ctx,checked_string(state,1));
    if(!record){lua_pushnil(state);return 1;}
    lua_createtable(state,0,4);
    lua_pushlstring(state,record->asset_id.data(),record->asset_id.size()); lua_setfield(state,-2,"id");
    auto rel=record->relative_path.generic_string(); lua_pushlstring(state,rel.data(),rel.size()); lua_setfield(state,-2,"path");
    auto abs=record->absolute_path.generic_string(); lua_pushlstring(state,abs.data(),abs.size()); lua_setfield(state,-2,"absolute");
    const auto kind=asset_kind_name(record->kind); lua_pushlstring(state,kind.data(),kind.size()); lua_setfield(state,-2,"kind");
    return 1;
}
int l_audio_play(lua_State* state) {
    auto* ctx=host(state); if(!ctx||!ctx->audio){lua_pushnil(state);return 1;}
    const auto* record=resolve_asset(ctx,checked_string(state,1));
    if(!record || record->kind != AssetKind::Audio){lua_pushnil(state);return 1;}
    const float volume=static_cast<float>(luaL_optnumber(state,2,1.0));
    const bool loop=lua_toboolean(state,3)!=0;
    const auto handle=ctx->audio->play(record->absolute_path,volume,loop);
    if(handle==kInvalidAudioVoice) lua_pushnil(state); else lua_pushinteger(state,static_cast<lua_Integer>(handle));
    return 1;
}
int l_audio_stop(lua_State* state) { auto* ctx=host(state); lua_pushboolean(state,ctx&&ctx->audio&&ctx->audio->stop(static_cast<AudioVoiceHandle>(luaL_checkinteger(state,1)))); return 1; }

void set_function(lua_State* state, const char* name, lua_CFunction fn) { lua_pushcfunction(state, fn); lua_setfield(state, -2, name); }
void add_namespace(lua_State* state, const char* name, std::initializer_list<std::pair<const char*, lua_CFunction>> functions) {
    lua_newtable(state);
    for (const auto& [fn_name, fn] : functions) set_function(state, fn_name, fn);
    lua_setfield(state, -2, name);
}

void install_api(lua_State* state) {
    lua_newtable(state);
    lua_pushlstring(state, kEngineVersion.data(), kEngineVersion.size()); lua_setfield(state, -2, "version");
    add_namespace(state,"Log",{{"info",l_log_info},{"warn",l_log_warn},{"error",l_log_error}});
    add_namespace(state,"Input",{{"value",l_input_value},{"down",l_input_down},{"pressed",l_input_pressed},{"released",l_input_released}});
    add_namespace(state,"Scene",{{"find",l_scene_find},{"find_tag",l_scene_find_tag},{"current",l_scene_current},{"load",l_scene_load},{"reload",l_scene_reload}});
    add_namespace(state,"Entity",{{"exists",l_entity_exists},{"name",l_entity_name},{"set_name",l_entity_set_name},{"enabled",l_entity_enabled},{"set_enabled",l_entity_set_enabled},{"position",l_entity_position},{"set_position",l_entity_set_position},{"rotation",l_entity_rotation},{"set_rotation",l_entity_set_rotation},{"scale",l_entity_scale},{"set_scale",l_entity_set_scale},{"get_property",l_entity_get_property},{"set_property",l_entity_set_property}});
    add_namespace(state,"UI",{{"exists",l_ui_exists},{"text",l_ui_text},{"set_text",l_ui_set_text},{"value",l_ui_value},{"set_value",l_ui_set_value},{"set_visible",l_ui_set_visible},{"set_disabled",l_ui_set_disabled},{"set_property",l_ui_set_property},{"set_class",l_ui_set_class},{"click",l_ui_click},{"consume_clicks",l_ui_consume_clicks}});
    add_namespace(state,"Assets",{{"resolve",l_assets_resolve}});
    add_namespace(state,"Audio",{{"play",l_audio_play},{"stop",l_audio_stop}});
    lua_setglobal(state,"Vespera");
}

bool call_optional(lua_State* state, const char* name, int argument_count, std::string& error) {
    lua_getglobal(state, name);
    if (lua_isnil(state, -1)) { lua_pop(state, 1); if(argument_count) lua_pop(state, argument_count); return true; }
    if (!lua_isfunction(state, -1)) { lua_pop(state, 1); if(argument_count) lua_pop(state, argument_count); error=std::string(name)+" exists but is not a function"; return false; }
    if (argument_count > 0) lua_insert(state, -1 - argument_count);
    if (lua_pcall(state, argument_count, 0, 0) != LUA_OK) {
        const char* message=lua_tostring(state,-1); error=message?message:"unknown Lua error"; lua_pop(state,1); return false;
    }
    return true;
}

void open_safe_libraries(lua_State* state) {
    struct Library { const char* name; lua_CFunction open; };
    static constexpr Library libraries[] = {
        {LUA_GNAME, luaopen_base}, {LUA_TABLIBNAME, luaopen_table}, {LUA_STRLIBNAME, luaopen_string},
        {LUA_MATHLIBNAME, luaopen_math}, {LUA_UTF8LIBNAME, luaopen_utf8}, {LUA_COLIBNAME, luaopen_coroutine},
    };
    for (const auto& library : libraries) { luaL_requiref(state, library.name, library.open, 1); lua_pop(state,1); }
    // The base library includes file-loading helpers. Lua is a project/runtime
    // scripting layer, not an arbitrary host-filesystem escape hatch.
    lua_pushnil(state); lua_setglobal(state,"dofile");
    lua_pushnil(state); lua_setglobal(state,"loadfile");
}

} // namespace

struct LuaScriptHost::Impl {
    lua_State* state = nullptr;
    HostContext bridge;
    LuaScriptHostConfig config;
    LuaScriptHostStatus status;
    bool update_enabled = false;

    void close_state(bool call_stop) {
        if (!state) return;
        if (call_stop && status.running) {
            std::string error;
            if (!call_optional(state,"Stop",0,error)) log::warn("Lua Stop error: "+error);
        }
        lua_close(state); state=nullptr; status.initialized=false; status.running=false; update_enabled=false;
    }

    bool load() {
        close_state(false);
        state=luaL_newstate();
        if(!state){status.message="could not allocate Lua state";return false;}
        open_safe_libraries(state);
        lua_pushlightuserdata(state,&kHostRegistryKey); lua_pushlightuserdata(state,&bridge); lua_settable(state,LUA_REGISTRYINDEX);
        install_api(state);
        const auto entry=config.entry_script.lexically_normal().string();
        if(luaL_loadfilex(state,entry.c_str(),nullptr)!=LUA_OK){const char* m=lua_tostring(state,-1);status.message="Lua load failed: "+std::string(m?m:"unknown error");lua_pop(state,1);close_state(false);return false;}
        if(lua_pcall(state,0,0,0)!=LUA_OK){const char* m=lua_tostring(state,-1);status.message="Lua entry failed: "+std::string(m?m:"unknown error");lua_pop(state,1);close_state(false);return false;}
        status.available=true; status.initialized=true; status.entry_script=config.entry_script; status.message="Lua entry loaded: "+config.entry_script.filename().string();
        update_enabled=true; return true;
    }
};

LuaScriptHost::LuaScriptHost():impl_(std::make_unique<Impl>()) { impl_->status.available=true; }
LuaScriptHost::~LuaScriptHost(){shutdown();}

bool LuaScriptHost::initialize(Scene& scene, InputSystem& input, AudioSystem& audio, AssetCatalog& assets,
                               const LuaScriptHostConfig& config, UiSurface* ui_surface) {
    shutdown(); impl_->config=config; impl_->bridge={&scene,&input,&audio,&assets,ui_surface,{},{}};
    if(config.entry_script.empty()){impl_->status.available=true;impl_->status.message="no Lua entry script configured";return false;}
    return impl_->load();
}

bool LuaScriptHost::start() {
    if(!impl_->state||!impl_->status.initialized)return false;
    std::string error;
    if(!call_optional(impl_->state,"Start",0,error)){impl_->status.message="Lua Start error: "+error;impl_->update_enabled=false;log::error(impl_->status.message);return false;}
    impl_->status.running=true; impl_->status.message="Lua runtime online: "+impl_->config.entry_script.filename().string(); return true;
}

bool LuaScriptHost::update(double delta_seconds) {
    if(!impl_->state||!impl_->status.running||!impl_->update_enabled)return false;
    lua_pushnumber(impl_->state,std::clamp(delta_seconds,0.0,0.05));
    std::string error;
    if(!call_optional(impl_->state,"Update",1,error)){impl_->update_enabled=false;impl_->status.message="Lua Update error (disabled until reload): "+error;log::error(impl_->status.message);return false;}
    return true;
}

bool LuaScriptHost::reload() {
    if(impl_->config.entry_script.empty())return false;
    const auto scene_path=impl_->bridge.current_scene_path;
    if(!impl_->load())return false;
    impl_->bridge.current_scene_path=scene_path;
    return start();
}

void LuaScriptHost::shutdown(){ if(!impl_)return; impl_->close_state(true); impl_->bridge={}; impl_->status={}; impl_->status.available=true; }
void LuaScriptHost::set_current_scene_path(const std::filesystem::path& path){impl_->bridge.current_scene_path=path;}
std::optional<LuaSceneLoadRequest> LuaScriptHost::take_scene_load_request(){auto r=std::move(impl_->bridge.scene_request);impl_->bridge.scene_request.reset();return r;}
const LuaScriptHostStatus& LuaScriptHost::status() const{return impl_->status;}

} // namespace vespera
