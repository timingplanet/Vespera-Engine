#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace vespera {

class Application;

enum class Key : std::uint8_t {
    W,
    A,
    S,
    D,
    LeftShift,
    Escape,
    Space,
    Enter,
    Tab,
    Up,
    Down,
    Left,
    Right,
    L,
    Backspace,
    Count,
};


enum class MouseButton : std::uint8_t {
    Left,
    Right,
    Middle,
    X1,
    X2,
    Count,
};

enum class GamepadButton : std::uint8_t {
    South,
    East,
    West,
    North,
    Back,
    Guide,
    Start,
    LeftStick,
    RightStick,
    LeftShoulder,
    RightShoulder,
    DpadUp,
    DpadDown,
    DpadLeft,
    DpadRight,
    Count,
};

enum class GamepadAxis : std::uint8_t {
    LeftX,
    LeftY,
    RightX,
    RightY,
    LeftTrigger,
    RightTrigger,
    Count,
};

enum class InputBindingType : std::uint8_t {
    Key,
    GamepadButton,
    GamepadAxis,
};

struct InputBinding {
    InputBindingType type = InputBindingType::Key;
    std::uint16_t code = 0;
    float scale = 1.0f;
    float deadzone = 0.0f;
};

class InputMap {
public:
    void clear();
    void clear_action(std::string_view action);

    void bind_key(std::string action, Key key, float scale = 1.0f);
    void bind_gamepad_button(std::string action, GamepadButton button, float scale = 1.0f);
    void bind_gamepad_axis(
        std::string action,
        GamepadAxis axis,
        float scale = 1.0f,
        float deadzone = 0.18f
    );

    [[nodiscard]] bool has_action(std::string_view action) const;
    [[nodiscard]] std::size_t action_count() const;
    [[nodiscard]] std::size_t binding_count() const;

private:
    friend class InputSystem;
    using BindingList = std::vector<InputBinding>;
    std::unordered_map<std::string, BindingList> bindings_;
};

class InputSystem {
public:
    [[nodiscard]] InputMap& map() { return map_; }
    [[nodiscard]] const InputMap& map() const { return map_; }

    [[nodiscard]] bool down(Key key) const;
    [[nodiscard]] bool pressed(Key key) const;
    [[nodiscard]] bool released(Key key) const;
    [[nodiscard]] std::uint16_t repeat_count(Key key) const;

    [[nodiscard]] bool gamepad_down(GamepadButton button) const;
    [[nodiscard]] bool gamepad_pressed(GamepadButton button) const;
    [[nodiscard]] bool gamepad_released(GamepadButton button) const;
    [[nodiscard]] float gamepad_axis(GamepadAxis axis) const;

    [[nodiscard]] bool gamepad_connected() const { return gamepad_connected_; }
    [[nodiscard]] std::string_view gamepad_name() const { return gamepad_name_; }

    [[nodiscard]] float mouse_delta_x() const { return mouse_delta_x_; }
    [[nodiscard]] float mouse_delta_y() const { return mouse_delta_y_; }
    [[nodiscard]] float mouse_x() const { return mouse_x_; }
    [[nodiscard]] float mouse_y() const { return mouse_y_; }
    [[nodiscard]] bool mouse_down(MouseButton button) const;
    [[nodiscard]] bool mouse_pressed(MouseButton button) const;
    [[nodiscard]] bool mouse_released(MouseButton button) const;
    [[nodiscard]] std::string_view text_input() const { return text_input_; }

    [[nodiscard]] float action_value(std::string_view action) const;
    [[nodiscard]] bool action_down(std::string_view action, float threshold = 0.5f) const;
    [[nodiscard]] bool action_pressed(std::string_view action, float threshold = 0.5f) const;
    [[nodiscard]] bool action_released(std::string_view action, float threshold = 0.5f) const;

    // Host-facing feed used by Application and the in-editor Play runtime.
    // Keeping this semantic (keys/mouse/gamepad) avoids exposing internal arrays
    // and lets alternate hosts drive the same InputMap/managed Input API.
    void host_begin_frame();
    void host_set_key(Key key, bool value);
    void host_add_key_repeat(Key key);
    void host_add_mouse_delta(float x, float y);
    void host_set_mouse_position(float x, float y);
    void host_set_mouse_button(MouseButton button, bool value);
    void host_add_text_input(std::string_view text);
    void host_set_gamepad_connected(bool connected, std::string name = {});
    void host_set_gamepad_button(GamepadButton button, bool value);
    void host_set_gamepad_axis(GamepadAxis axis, float value);
    // QA/automation-only semantic injection. When an override exists it replaces
    // physical bindings for that named action until cleared. This lets editor and
    // standalone runtime tests exercise the public Input action API without
    // synthesizing OS keyboard/gamepad events.
    void host_set_action_override(std::string action, float value);
    void host_clear_action_override(std::string_view action);
    void host_clear_action_overrides();

private:
    friend class Application;

    void begin_frame();
    void set_key(Key key, bool value);
    void add_key_repeat(Key key);
    void add_mouse_delta(float x, float y);
    void set_mouse_position(float x, float y);
    void set_mouse_button(MouseButton button, bool value);
    void add_text_input(std::string_view text);
    void set_gamepad_connected(bool connected, std::string name = {});
    void set_gamepad_button(GamepadButton button, bool value);
    void set_gamepad_axis(GamepadAxis axis, float value);

    [[nodiscard]] float evaluate_action(std::string_view action, bool previous) const;

    InputMap map_;
    std::array<bool, static_cast<std::size_t>(Key::Count)> keys_{};
    std::array<bool, static_cast<std::size_t>(Key::Count)> previous_keys_{};
    std::array<std::uint16_t, static_cast<std::size_t>(Key::Count)> key_repeat_counts_{};
    std::array<bool, static_cast<std::size_t>(MouseButton::Count)> mouse_buttons_{};
    std::array<bool, static_cast<std::size_t>(MouseButton::Count)> previous_mouse_buttons_{};
    std::array<bool, static_cast<std::size_t>(GamepadButton::Count)> gamepad_buttons_{};
    std::array<bool, static_cast<std::size_t>(GamepadButton::Count)> previous_gamepad_buttons_{};
    std::array<float, static_cast<std::size_t>(GamepadAxis::Count)> gamepad_axes_{};
    std::array<float, static_cast<std::size_t>(GamepadAxis::Count)> previous_gamepad_axes_{};
    std::unordered_map<std::string, float> action_overrides_;
    std::unordered_map<std::string, float> previous_action_overrides_;

    float mouse_delta_x_ = 0.0f;
    float mouse_delta_y_ = 0.0f;
    float mouse_x_ = 0.0f;
    float mouse_y_ = 0.0f;
    std::string text_input_;
    bool gamepad_connected_ = false;
    std::string gamepad_name_;
};

} // namespace vespera
