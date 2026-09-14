#include <vespera/input/input.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace vespera {
namespace {

std::size_t index_of(Key key) {
    return static_cast<std::size_t>(key);
}

std::size_t index_of(MouseButton button) {
    return static_cast<std::size_t>(button);
}

std::size_t index_of(GamepadButton button) {
    return static_cast<std::size_t>(button);
}

std::size_t index_of(GamepadAxis axis) {
    return static_cast<std::size_t>(axis);
}

float apply_deadzone(float value, float deadzone) {
    const float safe_deadzone = std::clamp(deadzone, 0.0f, 0.95f);
    const float magnitude = std::abs(value);
    if (magnitude <= safe_deadzone) {
        return 0.0f;
    }

    const float remapped = (magnitude - safe_deadzone) / (1.0f - safe_deadzone);
    return std::copysign(std::clamp(remapped, 0.0f, 1.0f), value);
}

bool active(float value, float threshold) {
    return std::abs(value) >= std::clamp(std::abs(threshold), 0.0f, 1.0f);
}

} // namespace

void InputMap::clear() {
    bindings_.clear();
}

void InputMap::clear_action(std::string_view action) {
    bindings_.erase(std::string(action));
}

void InputMap::bind_key(std::string action, Key key, float scale) {
    bindings_[std::move(action)].push_back(InputBinding{
        .type = InputBindingType::Key,
        .code = static_cast<std::uint16_t>(key),
        .scale = scale,
        .deadzone = 0.0f,
    });
}

void InputMap::bind_gamepad_button(std::string action, GamepadButton button, float scale) {
    bindings_[std::move(action)].push_back(InputBinding{
        .type = InputBindingType::GamepadButton,
        .code = static_cast<std::uint16_t>(button),
        .scale = scale,
        .deadzone = 0.0f,
    });
}

void InputMap::bind_gamepad_axis(
    std::string action,
    GamepadAxis axis,
    float scale,
    float deadzone
) {
    bindings_[std::move(action)].push_back(InputBinding{
        .type = InputBindingType::GamepadAxis,
        .code = static_cast<std::uint16_t>(axis),
        .scale = scale,
        .deadzone = deadzone,
    });
}

bool InputMap::has_action(std::string_view action) const {
    return bindings_.contains(std::string(action));
}

std::size_t InputMap::action_count() const {
    return bindings_.size();
}

std::size_t InputMap::binding_count() const {
    std::size_t total = 0;
    for (const auto& [_, bindings] : bindings_) {
        total += bindings.size();
    }
    return total;
}

bool InputSystem::down(Key key) const {
    return keys_[index_of(key)];
}

bool InputSystem::pressed(Key key) const {
    const auto index = index_of(key);
    return keys_[index] && !previous_keys_[index];
}

bool InputSystem::released(Key key) const {
    const auto index = index_of(key);
    return !keys_[index] && previous_keys_[index];
}

std::uint16_t InputSystem::repeat_count(Key key) const {
    return key_repeat_counts_[index_of(key)];
}

bool InputSystem::gamepad_down(GamepadButton button) const {
    return gamepad_buttons_[index_of(button)];
}

bool InputSystem::gamepad_pressed(GamepadButton button) const {
    const auto index = index_of(button);
    return gamepad_buttons_[index] && !previous_gamepad_buttons_[index];
}

bool InputSystem::gamepad_released(GamepadButton button) const {
    const auto index = index_of(button);
    return !gamepad_buttons_[index] && previous_gamepad_buttons_[index];
}

float InputSystem::gamepad_axis(GamepadAxis axis) const {
    return gamepad_axes_[index_of(axis)];
}

bool InputSystem::mouse_down(MouseButton button) const {
    return mouse_buttons_[index_of(button)];
}

bool InputSystem::mouse_pressed(MouseButton button) const {
    const auto index = index_of(button);
    return mouse_buttons_[index] && !previous_mouse_buttons_[index];
}

bool InputSystem::mouse_released(MouseButton button) const {
    const auto index = index_of(button);
    return !mouse_buttons_[index] && previous_mouse_buttons_[index];
}

float InputSystem::action_value(std::string_view action) const {
    return evaluate_action(action, false);
}

bool InputSystem::action_down(std::string_view action, float threshold) const {
    return active(evaluate_action(action, false), threshold);
}

bool InputSystem::action_pressed(std::string_view action, float threshold) const {
    return active(evaluate_action(action, false), threshold)
        && !active(evaluate_action(action, true), threshold);
}

bool InputSystem::action_released(std::string_view action, float threshold) const {
    return !active(evaluate_action(action, false), threshold)
        && active(evaluate_action(action, true), threshold);
}

void InputSystem::begin_frame() {
    previous_keys_ = keys_;
    previous_mouse_buttons_ = mouse_buttons_;
    previous_gamepad_buttons_ = gamepad_buttons_;
    previous_gamepad_axes_ = gamepad_axes_;
    previous_action_overrides_ = action_overrides_;
    key_repeat_counts_.fill(0);
    mouse_delta_x_ = 0.0f;
    mouse_delta_y_ = 0.0f;
    text_input_.clear();
}

void InputSystem::set_key(Key key, bool value) {
    keys_[index_of(key)] = value;
}

void InputSystem::add_key_repeat(Key key) {
    auto& count = key_repeat_counts_[index_of(key)];
    if (count != (std::numeric_limits<std::uint16_t>::max)()) ++count;
}

void InputSystem::add_mouse_delta(float x, float y) {
    mouse_delta_x_ += x;
    mouse_delta_y_ += y;
}

void InputSystem::set_mouse_position(float x, float y) {
    mouse_x_ = x;
    mouse_y_ = y;
}

void InputSystem::set_mouse_button(MouseButton button, bool value) {
    mouse_buttons_[index_of(button)] = value;
}

void InputSystem::add_text_input(std::string_view text) {
    text_input_.append(text);
}

void InputSystem::set_gamepad_connected(bool connected, std::string name) {
    gamepad_connected_ = connected;
    gamepad_name_ = connected ? std::move(name) : std::string{};
    if (!connected) {
        gamepad_buttons_.fill(false);
        previous_gamepad_buttons_.fill(false);
        gamepad_axes_.fill(0.0f);
        previous_gamepad_axes_.fill(0.0f);
    }
}

void InputSystem::set_gamepad_button(GamepadButton button, bool value) {
    gamepad_buttons_[index_of(button)] = value;
}

void InputSystem::set_gamepad_axis(GamepadAxis axis, float value) {
    gamepad_axes_[index_of(axis)] = std::clamp(value, -1.0f, 1.0f);
}


void InputSystem::host_begin_frame() {
    begin_frame();
}

void InputSystem::host_set_key(Key key, bool value) {
    set_key(key, value);
}

void InputSystem::host_add_key_repeat(Key key) {
    add_key_repeat(key);
}

void InputSystem::host_add_mouse_delta(float x, float y) {
    add_mouse_delta(x, y);
}

void InputSystem::host_set_mouse_position(float x, float y) {
    set_mouse_position(x, y);
}

void InputSystem::host_set_mouse_button(MouseButton button, bool value) {
    set_mouse_button(button, value);
}

void InputSystem::host_add_text_input(std::string_view text) {
    add_text_input(text);
}

void InputSystem::host_set_gamepad_connected(bool connected, std::string name) {
    set_gamepad_connected(connected, std::move(name));
}

void InputSystem::host_set_gamepad_button(GamepadButton button, bool value) {
    set_gamepad_button(button, value);
}

void InputSystem::host_set_gamepad_axis(GamepadAxis axis, float value) {
    set_gamepad_axis(axis, value);
}

void InputSystem::host_set_action_override(std::string action, float value) {
    if (action.empty()) return;
    action_overrides_[std::move(action)] = std::clamp(value, -1.0f, 1.0f);
}

void InputSystem::host_clear_action_override(std::string_view action) {
    action_overrides_.erase(std::string(action));
}

void InputSystem::host_clear_action_overrides() {
    action_overrides_.clear();
}

float InputSystem::evaluate_action(std::string_view action, bool previous) const {
    const auto& overrides = previous ? previous_action_overrides_ : action_overrides_;
    if (const auto injected = overrides.find(std::string(action)); injected != overrides.end()) {
        return std::clamp(injected->second, -1.0f, 1.0f);
    }
    const auto it = map_.bindings_.find(std::string(action));
    if (it == map_.bindings_.end()) {
        return 0.0f;
    }

    float value = 0.0f;
    for (const InputBinding& binding : it->second) {
        switch (binding.type) {
            case InputBindingType::Key: {
                const auto index = static_cast<std::size_t>(binding.code);
                if (index < keys_.size()) {
                    const bool is_down = previous ? previous_keys_[index] : keys_[index];
                    value += is_down ? binding.scale : 0.0f;
                }
                break;
            }
            case InputBindingType::GamepadButton: {
                const auto index = static_cast<std::size_t>(binding.code);
                if (index < gamepad_buttons_.size()) {
                    const bool is_down = previous
                        ? previous_gamepad_buttons_[index]
                        : gamepad_buttons_[index];
                    value += is_down ? binding.scale : 0.0f;
                }
                break;
            }
            case InputBindingType::GamepadAxis: {
                const auto index = static_cast<std::size_t>(binding.code);
                if (index < gamepad_axes_.size()) {
                    const float raw = previous ? previous_gamepad_axes_[index] : gamepad_axes_[index];
                    value += apply_deadzone(raw, binding.deadzone) * binding.scale;
                }
                break;
            }
        }
    }

    return std::clamp(value, -1.0f, 1.0f);
}

} // namespace vespera
