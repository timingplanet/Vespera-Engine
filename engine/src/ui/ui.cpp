#include <vespera/ui/ui.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <unordered_map>
#include <unordered_set>

namespace vespera {
namespace {

float clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

bool finite_rect(const UiRect& rect) {
    return std::isfinite(rect.x) && std::isfinite(rect.y)
        && std::isfinite(rect.width) && std::isfinite(rect.height);
}

UiRect inset_rect(UiRect rect, const UiInsets& insets, float scale) {
    rect.x += insets.left * scale;
    rect.y += insets.top * scale;
    rect.width -= (insets.left + insets.right) * scale;
    rect.height -= (insets.top + insets.bottom) * scale;
    rect.width = std::max(rect.width, 0.0f);
    rect.height = std::max(rect.height, 0.0f);
    return rect;
}

UiRect intersect_rect(const UiRect& a, const UiRect& b) {
    const float x0 = std::max(a.x, b.x);
    const float y0 = std::max(a.y, b.y);
    const float x1 = std::min(a.x + a.width, b.x + b.width);
    const float y1 = std::min(a.y + a.height, b.y + b.height);
    return {x0, y0, std::max(0.0f, x1 - x0), std::max(0.0f, y1 - y0)};
}

float compute_canvas_scale(const UiCanvasProperties& canvas, float width, float height) {
    if (canvas.scale_mode == UiScaleMode::ConstantPixelSize) return 1.0f;
    const float ref_w = std::max(canvas.reference_resolution.x, 1.0f);
    const float ref_h = std::max(canvas.reference_resolution.y, 1.0f);
    const float width_scale = std::max(width, 1.0f) / ref_w;
    const float height_scale = std::max(height, 1.0f) / ref_h;
    const float t = clamp01(canvas.match_width_or_height);
    return std::exp(std::log(width_scale) * (1.0f - t) + std::log(height_scale) * t);
}

UiRect resolve_rect(const UiRectTransform& transform, const UiRect& parent, float scale) {
    const float min_x = parent.x + parent.width * clamp01(transform.anchor_min.x) + transform.offset_min.x * scale;
    const float min_y = parent.y + parent.height * clamp01(transform.anchor_min.y) + transform.offset_min.y * scale;
    const float max_x = parent.x + parent.width * clamp01(transform.anchor_max.x) + transform.offset_max.x * scale;
    const float max_y = parent.y + parent.height * clamp01(transform.anchor_max.y) + transform.offset_max.y * scale;
    UiRect result{min_x, min_y, max_x - min_x, max_y - min_y};
    if (result.width < 0.0f) {
        result.x += result.width;
        result.width = -result.width;
    }
    if (result.height < 0.0f) {
        result.y += result.height;
        result.height = -result.height;
    }
    return result;
}

std::size_t sibling_index(const UiDocument& document, const UiNode& node) {
    std::size_t index = 0;
    for (const auto& candidate : document.nodes()) {
        if (candidate.id == node.id) break;
        if (candidate.parent_id == node.parent_id) ++index;
    }
    return index;
}

std::size_t sibling_count(const UiDocument& document, UiNodeId parent_id) {
    return static_cast<std::size_t>(std::count_if(document.nodes().begin(), document.nodes().end(),
        [parent_id](const UiNode& node) { return node.parent_id == parent_id; }));
}

UiLayoutMode effective_layout_mode(const UiNode& node) {
    if (node.layout.mode != UiLayoutMode::None) return node.layout.mode;
    if (node.type == UiNodeType::List) return UiLayoutMode::Vertical;
    if (node.type == UiNodeType::Grid) return UiLayoutMode::Grid;
    return UiLayoutMode::None;
}

UiRect resolve_layout_child_rect(
    const UiDocument& document,
    const UiNode& node,
    const UiNode& parent,
    const UiRect& parent_rect,
    float scale
) {
    const UiLayoutMode mode = effective_layout_mode(parent);
    if (mode == UiLayoutMode::None) {
        UiRect rect = resolve_rect(node.rect, parent_rect, scale);
        if (parent.type == UiNodeType::ScrollView) {
            rect.x -= parent.scroll.offset.x * scale;
            rect.y -= parent.scroll.offset.y * scale;
        }
        return rect;
    }

    const std::size_t index = sibling_index(document, node);
    const std::size_t count = std::max<std::size_t>(1, sibling_count(document, parent.id));
    const float authored_w = std::max(1.0f, std::abs(node.rect.offset_max.x - node.rect.offset_min.x) * scale);
    const float authored_h = std::max(1.0f, std::abs(node.rect.offset_max.y - node.rect.offset_min.y) * scale);
    const float cell_w = parent.layout.cell_size.x > 0.0f ? parent.layout.cell_size.x * scale : authored_w;
    const float cell_h = parent.layout.cell_size.y > 0.0f ? parent.layout.cell_size.y * scale : authored_h;
    const float gap_x = std::max(0.0f, parent.layout.spacing.x * scale);
    const float gap_y = std::max(0.0f, parent.layout.spacing.y * scale);

    UiRect rect{};
    if (mode == UiLayoutMode::Horizontal) {
        const float automatic_w = std::max(1.0f, (parent_rect.width - gap_x * static_cast<float>(count - 1)) / static_cast<float>(count));
        rect = {parent_rect.x + static_cast<float>(index) * (automatic_w + gap_x), parent_rect.y,
                parent.layout.cell_size.x > 0.0f ? cell_w : automatic_w,
                parent.layout.cell_size.y > 0.0f ? cell_h : parent_rect.height};
    } else if (mode == UiLayoutMode::Vertical) {
        rect = {parent_rect.x,
                parent_rect.y + static_cast<float>(index) * (cell_h + gap_y),
                parent.layout.cell_size.x > 0.0f ? cell_w : parent_rect.width,
                cell_h};
    } else {
        const std::uint32_t columns = std::max<std::uint32_t>(1, parent.layout.columns);
        const std::uint32_t col = static_cast<std::uint32_t>(index % columns);
        const std::uint32_t row = static_cast<std::uint32_t>(index / columns);
        const float automatic_w = std::max(1.0f,
            (parent_rect.width - gap_x * static_cast<float>(columns - 1)) / static_cast<float>(columns));
        rect = {parent_rect.x + static_cast<float>(col) * (automatic_w + gap_x),
                parent_rect.y + static_cast<float>(row) * (cell_h + gap_y),
                parent.layout.cell_size.x > 0.0f ? cell_w : automatic_w,
                cell_h};
    }
    if (parent.type == UiNodeType::ScrollView) {
        rect.x -= parent.scroll.offset.x * scale;
        rect.y -= parent.scroll.offset.y * scale;
    }
    return rect;
}

bool node_focusable(const UiNode& node) {
    if (!node.enabled) return false;
    if (node.type == UiNodeType::Button) return node.button.interactable;
    if (node.type == UiNodeType::TextInput) return !node.input.read_only;
    return false;
}

void apply_type_defaults(UiNode& node) {
    if (node.type == UiNodeType::Canvas) {
        node.rect.anchor_max = {1.0f, 1.0f};
        node.rect.offset_min = {0.0f, 0.0f};
        node.rect.offset_max = {0.0f, 0.0f};
    } else if (node.type == UiNodeType::Panel) {
        node.rect.offset_max = {320.0f, 180.0f};
        node.visual.color = {0.075f, 0.085f, 0.12f, 0.96f};
        node.surface.corner_radius = 12.0f;
        node.surface.border_width = 1.0f;
        node.surface.border_color = {0.24f, 0.31f, 0.46f, 0.72f};
        node.surface.shadow_offset = {0.0f, 5.0f};
        node.surface.shadow_softness = 8.0f;
        node.surface.shadow_color = {0.0f, 0.0f, 0.0f, 0.38f};
    } else if (node.type == UiNodeType::Text) {
        node.rect.offset_max = {240.0f, 40.0f};
    } else if (node.type == UiNodeType::Image) {
        node.rect.offset_max = {128.0f, 128.0f};
    } else if (node.type == UiNodeType::Button) {
        node.rect.offset_max = {160.0f, 44.0f};
        node.text.text = "Button";
        node.text.font_weight = 600;
        node.text.horizontal_alignment = UiHorizontalAlignment::Center;
        node.text.vertical_alignment = UiVerticalAlignment::Middle;
        node.button.normal_color = {0.15f, 0.25f, 0.42f, 1.0f};
        node.button.hovered_color = {0.20f, 0.36f, 0.62f, 1.0f};
        node.button.pressed_color = {0.10f, 0.19f, 0.34f, 1.0f};
        node.surface.corner_radius = 8.0f;
        node.surface.border_width = 1.0f;
        node.surface.border_color = {0.40f, 0.58f, 0.90f, 0.82f};
        node.surface.shadow_offset = {0.0f, 3.0f};
        node.surface.shadow_softness = 6.0f;
        node.surface.shadow_color = {0.0f, 0.0f, 0.0f, 0.34f};
    } else if (node.type == UiNodeType::ProgressBar) {
        node.rect.offset_max = {240.0f, 24.0f};
        node.surface.corner_radius = 6.0f;
        node.surface.border_width = 1.0f;
        node.surface.border_color = {0.18f, 0.26f, 0.42f, 0.82f};
    } else if (node.type == UiNodeType::ScrollView) {
        node.rect.offset_max = {360.0f, 240.0f};
        node.visual.color = {0.06f, 0.07f, 0.10f, 0.94f};
        node.surface.corner_radius = 10.0f;
        node.surface.border_width = 1.0f;
        node.surface.border_color = {0.20f, 0.27f, 0.40f, 0.65f};
        node.layout.clip_children = true;
        node.padding = {8.0f, 8.0f, 8.0f, 8.0f};
    } else if (node.type == UiNodeType::List) {
        node.rect.offset_max = {320.0f, 240.0f};
        node.layout.mode = UiLayoutMode::Vertical;
        node.layout.spacing = {0.0f, 8.0f};
        node.padding = {4.0f, 4.0f, 4.0f, 4.0f};
        node.visual.color = {0.07f, 0.08f, 0.10f, 0.40f};
    } else if (node.type == UiNodeType::Grid) {
        node.rect.offset_max = {420.0f, 260.0f};
        node.layout.mode = UiLayoutMode::Grid;
        node.layout.columns = 3;
        node.layout.spacing = {8.0f, 8.0f};
        node.layout.cell_size = {0.0f, 72.0f};
        node.padding = {4.0f, 4.0f, 4.0f, 4.0f};
        node.visual.color = {0.07f, 0.08f, 0.10f, 0.40f};
    } else if (node.type == UiNodeType::Tabs) {
        node.rect.offset_max = {420.0f, 260.0f};
        node.padding = {8.0f, 36.0f, 8.0f, 8.0f};
        node.visual.color = {0.07f, 0.08f, 0.12f, 0.96f};
        node.surface.corner_radius = 10.0f;
        node.surface.border_width = 1.0f;
        node.surface.border_color = {0.22f, 0.30f, 0.46f, 0.70f};
    } else if (node.type == UiNodeType::Modal) {
        node.rect.anchor_min = {0.5f, 0.5f};
        node.rect.anchor_max = {0.5f, 0.5f};
        node.rect.offset_min = {-240.0f, -160.0f};
        node.rect.offset_max = {240.0f, 160.0f};
        node.visual.color = {0.055f, 0.065f, 0.095f, 0.985f};
        node.surface.corner_radius = 14.0f;
        node.surface.border_width = 1.0f;
        node.surface.border_color = {0.30f, 0.42f, 0.66f, 0.82f};
        node.surface.shadow_offset = {0.0f, 8.0f};
        node.surface.shadow_softness = 14.0f;
        node.surface.shadow_color = {0.0f, 0.0f, 0.0f, 0.55f};
        node.z_order = 100;
        node.padding = {16.0f, 16.0f, 16.0f, 16.0f};
    } else if (node.type == UiNodeType::Tooltip) {
        node.rect.offset_max = {220.0f, 72.0f};
        node.visual.color = {0.035f, 0.045f, 0.065f, 0.97f};
        node.surface.corner_radius = 8.0f;
        node.surface.border_width = 1.0f;
        node.surface.border_color = {0.26f, 0.34f, 0.50f, 0.75f};
        node.surface.shadow_offset = {0.0f, 4.0f};
        node.surface.shadow_softness = 8.0f;
        node.surface.shadow_color = {0.0f, 0.0f, 0.0f, 0.45f};
        node.text.text = "Tooltip";
        node.z_order = 200;
        node.enabled = false;
        node.padding = {8.0f, 8.0f, 8.0f, 8.0f};
    } else if (node.type == UiNodeType::TextInput) {
        node.rect.offset_max = {260.0f, 40.0f};
        node.text.text.clear();
        node.text.vertical_alignment = UiVerticalAlignment::Middle;
        node.padding = {8.0f, 4.0f, 8.0f, 4.0f};
        node.visual.color = {0.065f, 0.085f, 0.13f, 1.0f};
        node.surface.corner_radius = 8.0f;
        node.surface.border_width = 1.0f;
        node.surface.border_color = {0.28f, 0.40f, 0.64f, 0.78f};
    }
}

} // namespace

const UiResolvedNode* UiLayoutResult::find(UiNodeId id) const {
    const auto it = std::find_if(nodes.begin(), nodes.end(), [id](const UiResolvedNode& node) { return node.id == id; });
    return it == nodes.end() ? nullptr : &*it;
}

void UiRuntimeState::clear_transient() {
    hovered.reset();
    pressed.reset();
}

bool UiRuntimeState::was_clicked(UiNodeId id) const {
    return std::find(pending_clicks.begin(), pending_clicks.end(), id) != pending_clicks.end();
}

bool UiRuntimeState::consume_click(UiNodeId id) {
    const auto it = std::find(pending_clicks.begin(), pending_clicks.end(), id);
    if (it == pending_clicks.end()) return false;
    pending_clicks.erase(it);
    return true;
}

UiNode& UiDocument::create_node(UiNodeType type, std::string name) {
    UiNode node;
    node.id = next_id_++;
    node.type = type;
    node.name = name.empty() ? std::string(ui_node_type_name(type)) : std::move(name);
    apply_type_defaults(node);
    nodes_.push_back(std::move(node));
    return nodes_.back();
}

UiNode* UiDocument::create_node_with_id(UiNodeId id, UiNodeType type, std::string name, std::string* error) {
    if (id == kInvalidUiNodeId) {
        if (error) *error = "UI node ID 0 is reserved";
        return nullptr;
    }
    if (find(id)) {
        if (error) *error = "duplicate UI node ID";
        return nullptr;
    }
    UiNode node;
    node.id = id;
    node.type = type;
    node.name = name.empty() ? std::string(ui_node_type_name(type)) : std::move(name);
    apply_type_defaults(node);
    nodes_.push_back(std::move(node));
    next_id_ = (std::max)(next_id_, id + 1);
    return &nodes_.back();
}

void UiDocument::clear() {
    nodes_.clear();
    next_id_ = 1;
}

UiNode* UiDocument::find(UiNodeId id) {
    const auto it = std::find_if(nodes_.begin(), nodes_.end(), [id](const UiNode& node) { return node.id == id; });
    return it == nodes_.end() ? nullptr : &*it;
}

const UiNode* UiDocument::find(UiNodeId id) const {
    const auto it = std::find_if(nodes_.begin(), nodes_.end(), [id](const UiNode& node) { return node.id == id; });
    return it == nodes_.end() ? nullptr : &*it;
}

bool UiDocument::is_descendant(UiNodeId possible_descendant, UiNodeId ancestor) const {
    if (possible_descendant == kInvalidUiNodeId || ancestor == kInvalidUiNodeId) return false;
    UiNodeId cursor = possible_descendant;
    std::unordered_set<UiNodeId> visited;
    while (cursor != kInvalidUiNodeId && visited.insert(cursor).second) {
        if (cursor == ancestor) return true;
        const auto* node = find(cursor);
        if (!node) break;
        cursor = node->parent_id;
    }
    return false;
}

bool UiDocument::reparent(UiNodeId child_id, UiNodeId parent_id, std::string* error) {
    auto* child = find(child_id);
    if (!child) {
        if (error) *error = "UI child does not exist";
        return false;
    }
    if (parent_id != kInvalidUiNodeId && !find(parent_id)) {
        if (error) *error = "UI parent does not exist";
        return false;
    }
    if (parent_id == child_id || is_descendant(parent_id, child_id)) {
        if (error) *error = "UI reparent would create a cycle";
        return false;
    }
    if (child->type == UiNodeType::Canvas && parent_id != kInvalidUiNodeId) {
        if (error) *error = "Canvas nodes must remain root UI nodes";
        return false;
    }
    child->parent_id = parent_id;
    return true;
}

bool UiDocument::destroy_node(UiNodeId id) {
    if (!find(id)) return false;
    std::vector<UiNodeId> children;
    for (const auto& node : nodes_) if (node.parent_id == id) children.push_back(node.id);
    for (const auto child : children) destroy_node(child);
    const auto it = std::find_if(nodes_.begin(), nodes_.end(), [id](const UiNode& node) { return node.id == id; });
    if (it != nodes_.end()) nodes_.erase(it);
    return true;
}

UiLayoutResult resolve_ui_layout(const UiDocument& document, float viewport_width, float viewport_height) {
    UiLayoutResult result;
    result.viewport = {std::max(viewport_width, 0.0f), std::max(viewport_height, 0.0f)};
    const UiRect viewport{0.0f, 0.0f, result.viewport.x, result.viewport.y};

    const UiNode* canvas_node = nullptr;
    for (const auto& node : document.nodes()) {
        if (node.type == UiNodeType::Canvas && node.parent_id == kInvalidUiNodeId) {
            canvas_node = &node;
            break;
        }
    }
    result.canvas_scale = canvas_node ? compute_canvas_scale(canvas_node->canvas, viewport.width, viewport.height) : 1.0f;

    std::unordered_map<UiNodeId, UiRect> resolved_rects;
    std::unordered_map<UiNodeId, UiRect> resolved_clips;
    std::unordered_map<UiNodeId, bool> resolved_clip_enabled;
    std::unordered_map<UiNodeId, bool> effective_enabled;
    std::unordered_set<UiNodeId> pending;
    for (const auto& node : document.nodes()) pending.insert(node.id);

    std::size_t passes = 0;
    while (!pending.empty() && passes++ <= document.nodes().size()) {
        bool progressed = false;
        for (const auto& node : document.nodes()) {
            if (!pending.contains(node.id)) continue;
            UiRect parent_rect = viewport;
            UiRect inherited_clip = viewport;
            bool inherited_clip_enabled = false;
            bool parent_enabled = true;
            const UiNode* parent_node = nullptr;
            if (node.parent_id != kInvalidUiNodeId) {
                const auto parent = resolved_rects.find(node.parent_id);
                if (parent == resolved_rects.end()) continue;
                parent_node = document.find(node.parent_id);
                parent_rect = parent_node ? inset_rect(parent->second, parent_node->padding, result.canvas_scale) : parent->second;
                inherited_clip = resolved_clips[node.parent_id];
                inherited_clip_enabled = resolved_clip_enabled[node.parent_id];
                parent_enabled = effective_enabled[node.parent_id];
            }

            UiRect rect{};
            if (node.type == UiNodeType::Canvas && node.parent_id == kInvalidUiNodeId) {
                rect = viewport;
            } else if (parent_node) {
                rect = resolve_layout_child_rect(document, node, *parent_node, parent_rect, result.canvas_scale);
            } else {
                rect = resolve_rect(node.rect, parent_rect, result.canvas_scale);
            }
            rect = inset_rect(rect, node.margin, result.canvas_scale);
            if (!finite_rect(rect)) {
                result.warnings.push_back(std::format("UI node {} ('{}') resolved to non-finite geometry", node.id, node.name));
                rect = {};
            }

            bool enabled = node.enabled && parent_enabled;
            if (parent_node && parent_node->type == UiNodeType::Tabs) {
                enabled = enabled && sibling_index(document, node) == parent_node->tabs.active_index;
            }

            UiRect clip = inherited_clip_enabled ? inherited_clip : viewport;
            bool clip_enabled = inherited_clip_enabled;
            if (parent_node && (parent_node->layout.clip_children || parent_node->type == UiNodeType::ScrollView)) {
                clip = intersect_rect(clip, parent_rect);
                clip_enabled = true;
            }

            resolved_rects[node.id] = rect;
            resolved_clips[node.id] = clip;
            resolved_clip_enabled[node.id] = clip_enabled;
            effective_enabled[node.id] = enabled;
            result.nodes.push_back({node.id, node.parent_id, node.type, rect, clip, clip_enabled,
                                    result.canvas_scale, node.z_order, enabled});
            pending.erase(node.id);
            progressed = true;
        }
        if (!progressed) break;
    }

    for (const auto unresolved : pending) {
        const auto* node = document.find(unresolved);
        result.warnings.push_back(std::format("UI node {} ('{}') has a missing/cyclic parent", unresolved,
            node ? node->name : std::string("?")));
    }
    std::stable_sort(result.nodes.begin(), result.nodes.end(), [](const UiResolvedNode& a, const UiResolvedNode& b) {
        return a.z_order < b.z_order;
    });
    return result;
}

std::optional<UiNodeId> ui_hit_test(const UiDocument& document, const UiLayoutResult& layout, UiVec2 point, bool interactables_only) {
    for (auto it = layout.nodes.rbegin(); it != layout.nodes.rend(); ++it) {
        if (!it->enabled) continue;
        const auto* node = document.find(it->id);
        if (!node) continue;
        if (interactables_only && !node_focusable(*node)) continue;
        if (it->clip_enabled) {
            const auto& clip = it->clip_rect;
            if (point.x < clip.x || point.y < clip.y || point.x > clip.x + clip.width || point.y > clip.y + clip.height) continue;
        }
        const auto& rect = it->rect;
        if (point.x >= rect.x && point.y >= rect.y && point.x <= rect.x + rect.width && point.y <= rect.y + rect.height) {
            return it->id;
        }
    }
    return std::nullopt;
}

} // namespace vespera
