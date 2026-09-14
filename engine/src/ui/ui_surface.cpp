#include <vespera/ui/ui_surface.hpp>

#include <vespera/ui/ui.hpp>

#include <algorithm>
#include <charconv>
#include <system_error>

namespace vespera {
namespace {

UiNode* find_by_name(UiDocument* document, std::string_view name) {
    if (!document || name.empty()) return nullptr;
    for (auto& node : document->nodes()) {
        if (node.name == name) return &node;
    }
    return nullptr;
}

const UiNode* find_by_name(const UiDocument* document, std::string_view name) {
    if (!document || name.empty()) return nullptr;
    for (const auto& node : document->nodes()) {
        if (node.name == name) return &node;
    }
    return nullptr;
}

std::string name_for_id(const UiDocument* document, const std::optional<UiNodeId>& id) {
    if (!document || !id) return {};
    const auto* node = document->find(*id);
    return node ? node->name : std::string{};
}

bool parse_float(std::string_view text, float& value) {
    const char* first = text.data();
    const char* last = first + text.size();
    const auto result = std::from_chars(first, last, value);
    return result.ec == std::errc{} && result.ptr == last;
}

} // namespace

void UiHandleTable::bind(UiSurface* surface) {
    if (surface_ == surface) return;
    surface_ = surface;
    clear();
}

void UiHandleTable::clear() {
    next_handle_ = 1;
    handle_by_key_.clear();
    key_by_handle_.clear();
}

std::uint64_t UiHandleTable::find(std::string_view element_key) {
    if (!surface_ || element_key.empty() || !surface_->exists(element_key)) return 0;
    const std::string key(element_key);
    if (const auto it = handle_by_key_.find(key); it != handle_by_key_.end()) return it->second;
    const std::uint64_t handle = next_handle_++;
    handle_by_key_.emplace(key, handle);
    key_by_handle_.emplace(handle, key);
    return handle;
}

bool UiHandleTable::exists(std::uint64_t handle) const {
    if (!surface_ || handle == 0) return false;
    const auto it = key_by_handle_.find(handle);
    return it != key_by_handle_.end() && surface_->exists(it->second);
}

std::string_view UiHandleTable::key(std::uint64_t handle) const {
    const auto it = key_by_handle_.find(handle);
    return it == key_by_handle_.end() ? std::string_view{} : std::string_view(it->second);
}

LegacyUiSurface::LegacyUiSurface(UiDocument& document, UiRuntimeState* runtime_state)
    : document_(&document), runtime_state_(runtime_state) {}

void LegacyUiSurface::bind_runtime_state(UiRuntimeState* runtime_state) {
    runtime_state_ = runtime_state;
}

std::string_view LegacyUiSurface::backend_name() const { return "legacy-slui"; }

bool LegacyUiSurface::exists(std::string_view element_key) const {
    return find_by_name(static_cast<const UiDocument*>(document_), element_key) != nullptr;
}

bool LegacyUiSurface::set_text(std::string_view element_key, std::string_view text_value) {
    auto* node = find_by_name(document_, element_key);
    if (!node) return false;
    node->text.text.assign(text_value);
    return true;
}

std::string LegacyUiSurface::text(std::string_view element_key) const {
    const auto* node = find_by_name(static_cast<const UiDocument*>(document_), element_key);
    return node ? node->text.text : std::string{};
}

bool LegacyUiSurface::set_value(std::string_view element_key, std::string_view value_text) {
    auto* node = find_by_name(document_, element_key);
    if (!node) return false;
    if (node->type == UiNodeType::TextInput) {
        node->text.text.assign(value_text);
        if (node->text.text.size() > node->input.max_length) {
            node->text.text.resize(node->input.max_length);
        }
        return true;
    }
    if (node->type == UiNodeType::ProgressBar) {
        float parsed = 0.0f;
        if (!parse_float(value_text, parsed)) return false;
        node->progress.value = std::clamp(parsed, node->progress.minimum, node->progress.maximum);
        return true;
    }
    return false;
}

std::string LegacyUiSurface::value(std::string_view element_key) const {
    const auto* node = find_by_name(static_cast<const UiDocument*>(document_), element_key);
    if (!node) return {};
    if (node->type == UiNodeType::TextInput) return node->text.text;
    if (node->type == UiNodeType::ProgressBar) return std::to_string(node->progress.value);
    return {};
}

bool LegacyUiSurface::set_property(std::string_view, std::string_view, std::string_view) {
    return false;
}

bool LegacyUiSurface::set_class(std::string_view, std::string_view, bool) {
    return false;
}

bool LegacyUiSurface::set_disabled(std::string_view element_key, bool disabled) {
    auto* node = find_by_name(document_, element_key);
    if (!node) return false;
    if (node->type == UiNodeType::Button) {
        node->button.interactable = !disabled;
        return true;
    }
    if (node->type == UiNodeType::TextInput) {
        node->input.read_only = disabled;
        return true;
    }
    return false;
}

bool LegacyUiSurface::disabled(std::string_view element_key, bool& disabled_value) const {
    const auto* node = find_by_name(static_cast<const UiDocument*>(document_), element_key);
    if (!node) return false;
    if (node->type == UiNodeType::Button) {
        disabled_value = !node->button.interactable;
        return true;
    }
    if (node->type == UiNodeType::TextInput) {
        disabled_value = node->input.read_only;
        return true;
    }
    return false;
}

bool LegacyUiSurface::set_visible(std::string_view element_key, bool visible) {
    auto* node = find_by_name(document_, element_key);
    if (!node) return false;
    node->enabled = visible;
    if (!visible && runtime_state_) {
        if (runtime_state_->hovered && *runtime_state_->hovered == node->id) runtime_state_->hovered.reset();
        if (runtime_state_->pressed && *runtime_state_->pressed == node->id) runtime_state_->pressed.reset();
        if (runtime_state_->focused && *runtime_state_->focused == node->id) runtime_state_->focused.reset();
        if (runtime_state_->pointer_capture == node->id) runtime_state_->pointer_capture = kInvalidUiNodeId;
        std::erase(runtime_state_->pending_clicks, node->id);
    }
    return true;
}

bool LegacyUiSurface::visible(std::string_view element_key, bool& visible_value) const {
    const auto* node = find_by_name(static_cast<const UiDocument*>(document_), element_key);
    if (!node) return false;
    visible_value = node->enabled;
    return true;
}

bool LegacyUiSurface::set_interactable(std::string_view element_key, bool interactable_value) {
    auto* node = find_by_name(document_, element_key);
    if (!node || node->type != UiNodeType::Button) return false;
    node->button.interactable = interactable_value;
    return true;
}

bool LegacyUiSurface::interactable(std::string_view element_key, bool& interactable_value) const {
    const auto* node = find_by_name(static_cast<const UiDocument*>(document_), element_key);
    if (!node || node->type != UiNodeType::Button) return false;
    interactable_value = node->button.interactable;
    return true;
}

bool LegacyUiSurface::set_read_only(std::string_view element_key, bool read_only_value) {
    auto* node = find_by_name(document_, element_key);
    if (!node || node->type != UiNodeType::TextInput) return false;
    node->input.read_only = read_only_value;
    return true;
}

bool LegacyUiSurface::read_only(std::string_view element_key, bool& read_only_value) const {
    const auto* node = find_by_name(static_cast<const UiDocument*>(document_), element_key);
    if (!node || node->type != UiNodeType::TextInput) return false;
    read_only_value = node->input.read_only;
    return true;
}

bool LegacyUiSurface::set_numeric_value(std::string_view element_key, float numeric_value) {
    auto* node = find_by_name(document_, element_key);
    if (!node || node->type != UiNodeType::ProgressBar) return false;
    node->progress.value = std::clamp(numeric_value, node->progress.minimum, node->progress.maximum);
    return true;
}

bool LegacyUiSurface::numeric_value(std::string_view element_key, float& numeric_value) const {
    const auto* node = find_by_name(static_cast<const UiDocument*>(document_), element_key);
    if (!node || node->type != UiNodeType::ProgressBar) return false;
    numeric_value = node->progress.value;
    return true;
}

bool LegacyUiSurface::set_color(std::string_view element_key, UiColorSlot slot, const std::array<float, 4>& color) {
    auto* node = find_by_name(document_, element_key);
    if (!node) return false;
    switch (slot) {
        case UiColorSlot::Visual: node->visual.color = color; return true;
        case UiColorSlot::Text: node->text.color = color; return true;
        case UiColorSlot::ProgressFill: node->progress.fill_color = color; return true;
        case UiColorSlot::ProgressBackground: node->progress.background_color = color; return true;
    }
    return false;
}

bool LegacyUiSurface::set_asset(std::string_view element_key, UiAssetSlot slot, const AssetReference& reference, std::string_view) {
    auto* node = find_by_name(document_, element_key);
    if (!node) return false;
    switch (slot) {
        case UiAssetSlot::Image: node->visual.image = reference; return true;
        case UiAssetSlot::Font: node->text.font = reference; return true;
    }
    return false;
}

bool LegacyUiSurface::focus(std::string_view element_key, bool) {
    if (!runtime_state_) return false;
    auto* node = find_by_name(document_, element_key);
    if (!node || !node->enabled) return false;
    if (node->type == UiNodeType::Button && !node->button.interactable) return false;
    if (node->type == UiNodeType::TextInput && node->input.read_only) return false;
    if (node->type != UiNodeType::Button && node->type != UiNodeType::TextInput) return false;
    runtime_state_->focused = node->id;
    return true;
}

bool LegacyUiSurface::blur(std::string_view element_key) {
    if (!runtime_state_) return false;
    const auto* node = find_by_name(static_cast<const UiDocument*>(document_), element_key);
    if (!node) return false;
    if (runtime_state_->focused && *runtime_state_->focused == node->id) runtime_state_->focused.reset();
    return true;
}

bool LegacyUiSurface::click(std::string_view element_key) {
    if (!runtime_state_) return false;
    auto* node = find_by_name(document_, element_key);
    if (!node || !node->enabled) return false;
    if (node->type == UiNodeType::Button && !node->button.interactable) return false;
    if (node->type == UiNodeType::TextInput && node->input.read_only) return false;
    if (node->type != UiNodeType::Button && node->type != UiNodeType::TextInput) return false;
    runtime_state_->pending_clicks.push_back(node->id);
    return true;
}

int LegacyUiSurface::consume_clicks(std::string_view element_key) {
    if (!runtime_state_) return 0;
    const auto* node = find_by_name(static_cast<const UiDocument*>(document_), element_key);
    return node && runtime_state_->consume_click(node->id) ? 1 : 0;
}

std::string LegacyUiSurface::hovered_id() const {
    return name_for_id(document_, runtime_state_ ? runtime_state_->hovered : std::optional<UiNodeId>{});
}

std::string LegacyUiSurface::focused_id() const {
    return name_for_id(document_, runtime_state_ ? runtime_state_->focused : std::optional<UiNodeId>{});
}

} // namespace vespera
