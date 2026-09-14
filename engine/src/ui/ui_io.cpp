#include <vespera/ui/ui_io.hpp>

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_set>

namespace vespera {
namespace {

constexpr int kUiFormatVersion = 3;

UiIoResult fail(std::size_t line, std::string message) {
    if (line != 0) message = "UI line " + std::to_string(line) + ": " + std::move(message);
    return {false, std::move(message)};
}

std::string_view node_type_token(UiNodeType type) {
    switch (type) {
        case UiNodeType::Canvas: return "canvas";
        case UiNodeType::Panel: return "panel";
        case UiNodeType::Text: return "text";
        case UiNodeType::Image: return "image";
        case UiNodeType::Button: return "button";
        case UiNodeType::ProgressBar: return "progress";
        case UiNodeType::ScrollView: return "scroll";
        case UiNodeType::List: return "list";
        case UiNodeType::Grid: return "grid";
        case UiNodeType::Tabs: return "tabs";
        case UiNodeType::Modal: return "modal";
        case UiNodeType::Tooltip: return "tooltip";
        case UiNodeType::TextInput: return "text_input";
    }
    return "panel";
}

std::optional<UiNodeType> node_type_from_token(std::string_view value) {
    if (value == "canvas") return UiNodeType::Canvas;
    if (value == "panel") return UiNodeType::Panel;
    if (value == "text") return UiNodeType::Text;
    if (value == "image") return UiNodeType::Image;
    if (value == "button") return UiNodeType::Button;
    if (value == "progress") return UiNodeType::ProgressBar;
    if (value == "scroll") return UiNodeType::ScrollView;
    if (value == "list") return UiNodeType::List;
    if (value == "grid") return UiNodeType::Grid;
    if (value == "tabs") return UiNodeType::Tabs;
    if (value == "modal") return UiNodeType::Modal;
    if (value == "tooltip") return UiNodeType::Tooltip;
    if (value == "text_input") return UiNodeType::TextInput;
    return std::nullopt;
}

std::string_view scale_token(UiScaleMode value) {
    return value == UiScaleMode::ConstantPixelSize ? "constant" : "screen";
}
std::optional<UiScaleMode> scale_from_token(std::string_view value) {
    if (value == "constant") return UiScaleMode::ConstantPixelSize;
    if (value == "screen") return UiScaleMode::ScaleWithScreenSize;
    return std::nullopt;
}
std::string_view h_align_token(UiHorizontalAlignment value) {
    switch (value) { case UiHorizontalAlignment::Left: return "left"; case UiHorizontalAlignment::Center: return "center"; case UiHorizontalAlignment::Right: return "right"; }
    return "left";
}
std::optional<UiHorizontalAlignment> h_align_from_token(std::string_view value) {
    if (value == "left") return UiHorizontalAlignment::Left;
    if (value == "center") return UiHorizontalAlignment::Center;
    if (value == "right") return UiHorizontalAlignment::Right;
    return std::nullopt;
}
std::string_view v_align_token(UiVerticalAlignment value) {
    switch (value) { case UiVerticalAlignment::Top: return "top"; case UiVerticalAlignment::Middle: return "middle"; case UiVerticalAlignment::Bottom: return "bottom"; }
    return "top";
}
std::optional<UiVerticalAlignment> v_align_from_token(std::string_view value) {
    if (value == "top") return UiVerticalAlignment::Top;
    if (value == "middle") return UiVerticalAlignment::Middle;
    if (value == "bottom") return UiVerticalAlignment::Bottom;
    return std::nullopt;
}
std::string_view layout_token(UiLayoutMode value) {
    switch (value) {
        case UiLayoutMode::None: return "none";
        case UiLayoutMode::Horizontal: return "horizontal";
        case UiLayoutMode::Vertical: return "vertical";
        case UiLayoutMode::Grid: return "grid";
    }
    return "none";
}
std::optional<UiLayoutMode> layout_from_token(std::string_view value) {
    if (value == "none") return UiLayoutMode::None;
    if (value == "horizontal") return UiLayoutMode::Horizontal;
    if (value == "vertical") return UiLayoutMode::Vertical;
    if (value == "grid") return UiLayoutMode::Grid;
    return std::nullopt;
}

std::string_view image_fit_token(UiImageFit value) {
    switch (value) {
        case UiImageFit::Stretch: return "stretch";
        case UiImageFit::Contain: return "contain";
        case UiImageFit::Cover: return "cover";
    }
    return "stretch";
}
std::optional<UiImageFit> image_fit_from_token(std::string_view value) {
    if (value == "stretch") return UiImageFit::Stretch;
    if (value == "contain") return UiImageFit::Contain;
    if (value == "cover") return UiImageFit::Cover;
    return std::nullopt;
}

std::string asset_token(const std::string& value) { return value.empty() ? "-" : value; }
std::string path_token(const std::filesystem::path& value) { return value.empty() ? "-" : value.generic_string(); }

} // namespace

UiIoResult save_ui_document(const UiDocument& document, const std::filesystem::path& path) {
    std::error_code ec;
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) return {false, "could not create UI document directory: " + ec.message()};

    const auto temporary = std::filesystem::path(path.string() + ".tmp");
    const auto backup = std::filesystem::path(path.string() + ".vespera_bak");
    std::filesystem::remove(temporary, ec);
    ec.clear();

    std::ofstream out(temporary, std::ios::trunc);
    if (!out) return {false, "could not write UI document: " + path.string()};
    out << "vespera_ui " << kUiFormatVersion << "\n";
    for (const auto& node : document.nodes()) {
        out << "node " << node.id << " " << node.parent_id << " " << std::quoted(std::string(node_type_token(node.type)))
            << " " << std::quoted(node.name) << " " << (node.enabled ? 1 : 0) << " " << node.z_order << "\n";
        out << "rect " << node.rect.anchor_min.x << " " << node.rect.anchor_min.y << " "
            << node.rect.anchor_max.x << " " << node.rect.anchor_max.y << " "
            << node.rect.offset_min.x << " " << node.rect.offset_min.y << " "
            << node.rect.offset_max.x << " " << node.rect.offset_max.y << " "
            << node.rect.pivot.x << " " << node.rect.pivot.y << "\n";
        out << "margin " << node.margin.left << " " << node.margin.top << " " << node.margin.right << " " << node.margin.bottom << "\n";
        out << "padding " << node.padding.left << " " << node.padding.top << " " << node.padding.right << " " << node.padding.bottom << "\n";
        out << "canvas " << node.canvas.reference_resolution.x << " " << node.canvas.reference_resolution.y << " "
            << std::quoted(std::string(scale_token(node.canvas.scale_mode))) << " " << node.canvas.match_width_or_height << "\n";
        out << "visual " << node.visual.color[0] << " " << node.visual.color[1] << " " << node.visual.color[2] << " " << node.visual.color[3] << "\n";
        out << "surface " << node.surface.opacity << " " << node.surface.corner_radius << " " << node.surface.border_width;
        for (float v : node.surface.border_color) out << " " << v;
        out << " " << node.surface.shadow_offset.x << " " << node.surface.shadow_offset.y << " " << node.surface.shadow_softness;
        for (float v : node.surface.shadow_color) out << " " << v;
        out << " " << std::quoted(std::string(image_fit_token(node.surface.image_fit)))
            << " " << node.surface.nine_slice.left << " " << node.surface.nine_slice.top
            << " " << node.surface.nine_slice.right << " " << node.surface.nine_slice.bottom << "\n";
        out << "image_asset " << std::quoted(asset_token(node.visual.image.asset_id)) << " " << std::quoted(path_token(node.visual.image.path)) << "\n";
        out << "text " << std::quoted(node.text.text) << " " << node.text.font_size << " "
            << node.text.color[0] << " " << node.text.color[1] << " " << node.text.color[2] << " " << node.text.color[3] << " "
            << std::quoted(std::string(h_align_token(node.text.horizontal_alignment))) << " "
            << std::quoted(std::string(v_align_token(node.text.vertical_alignment))) << " " << (node.text.rich_text ? 1 : 0) << "\n";
        out << "font_asset " << std::quoted(asset_token(node.text.font.asset_id)) << " " << std::quoted(path_token(node.text.font.path)) << "\n";
        out << "text_style " << std::quoted(node.text.font_family) << " " << node.text.font_weight << " "
            << (node.text.italic ? 1 : 0) << " " << (node.text.wrap ? 1 : 0) << " " << node.text.line_spacing
            << " " << node.text.shadow_offset.x << " " << node.text.shadow_offset.y;
        for (float v : node.text.shadow_color) out << " " << v;
        out << "\n";
        out << "button " << (node.button.interactable ? 1 : 0);
        for (float v : node.button.normal_color) out << " " << v;
        for (float v : node.button.hovered_color) out << " " << v;
        for (float v : node.button.pressed_color) out << " " << v;
        for (float v : node.button.disabled_color) out << " " << v;
        out << "\n";
        out << "layout " << std::quoted(std::string(layout_token(node.layout.mode))) << " "
            << node.layout.spacing.x << " " << node.layout.spacing.y << " "
            << node.layout.cell_size.x << " " << node.layout.cell_size.y << " "
            << std::max<std::uint32_t>(1, node.layout.columns) << " " << (node.layout.clip_children ? 1 : 0) << "\n";
        out << "progress " << node.progress.value << " " << node.progress.minimum << " " << node.progress.maximum;
        for (float v : node.progress.background_color) out << " " << v;
        for (float v : node.progress.fill_color) out << " " << v;
        out << "\n";
        out << "scroll " << node.scroll.offset.x << " " << node.scroll.offset.y << "\n";
        out << "tabs " << node.tabs.active_index << "\n";
        out << "input " << std::quoted(node.input.placeholder) << " " << node.input.max_length << " " << (node.input.read_only ? 1 : 0) << "\n";
        out << "end_node\n";
    }
    out << "end_ui\n";
    out.close();
    if (!out) {
        std::filesystem::remove(temporary, ec);
        return {false, "failed while writing UI document: " + path.string()};
    }

    const bool had_original = std::filesystem::exists(path, ec);
    if (ec) {
        std::filesystem::remove(temporary);
        return {false, "could not inspect existing UI document: " + ec.message()};
    }
    if (had_original) {
        std::filesystem::remove(backup, ec);
        if (ec) {
            std::filesystem::remove(temporary);
            return {false, "could not clear UI document backup path: " + ec.message()};
        }
        std::filesystem::rename(path, backup, ec);
        if (ec) {
            std::filesystem::remove(temporary);
            return {false, "could not stage existing UI document for replacement: " + ec.message()};
        }
    }

    ec.clear();
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        const auto replace_error = ec.message();
        std::error_code restore_ec;
        if (had_original) std::filesystem::rename(backup, path, restore_ec);
        std::filesystem::remove(temporary);
        if (had_original && restore_ec) {
            return {false, "could not replace UI document (" + replace_error
                + ") and backup restore also failed: " + restore_ec.message()};
        }
        return {false, "could not replace UI document: " + replace_error};
    }

    std::string cleanup_warning;
    if (had_original) {
        std::filesystem::remove(backup, ec);
        if (ec) cleanup_warning = " (warning: old backup could not be removed: " + ec.message() + ")";
    }
    return {true, "saved UI document v3: " + path.string() + cleanup_warning};
}

UiIoResult load_ui_document(UiDocument& document, const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) return {false, "could not open UI document: " + path.string()};
    UiDocument candidate;
    UiNode* current = nullptr;
    std::unordered_set<UiNodeId> ids;
    std::string line;
    std::size_t line_number = 0;
    bool header = false;
    int version = 0;
    while (std::getline(in, line)) {
        ++line_number;
        const auto first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos || line[first] == '#') continue;
        std::istringstream stream(line.substr(first));
        std::string command; stream >> command;
        if (!header) {
            if (command != "vespera_ui" || !(stream >> version) || version < 1 || version > kUiFormatVersion)
                return fail(line_number, "expected 'vespera_ui 1', 'vespera_ui 2' or 'vespera_ui 3'");
            header = true;
            continue;
        }
        if (command == "node") {
            if (current) return fail(line_number, "nested node record");
            UiNodeId id=0,parent=0; std::string type_text,name; int enabled=0,z=0;
            if (!(stream >> id >> parent >> std::quoted(type_text) >> std::quoted(name) >> enabled >> z)) return fail(line_number, "invalid node record");
            const auto type = node_type_from_token(type_text);
            if (!type) return fail(line_number, "unknown UI node type");
            if (version == 1 && static_cast<int>(*type) > static_cast<int>(UiNodeType::Button)) return fail(line_number, "UI v1 cannot contain v2 node type");
            std::string create_error;
            current = candidate.create_node_with_id(id, *type, name, &create_error);
            if (!current) return fail(line_number, create_error);
            current->parent_id = parent; current->enabled = enabled != 0; current->z_order = z; ids.insert(id);
        } else if (command == "end_node") {
            if (!current) return fail(line_number, "end_node without node");
            current = nullptr;
        } else if (command == "end_ui") {
            break;
        } else {
            if (!current) return fail(line_number, "property outside node");
            if (command == "rect") {
                if (!(stream >> current->rect.anchor_min.x >> current->rect.anchor_min.y >> current->rect.anchor_max.x >> current->rect.anchor_max.y
                    >> current->rect.offset_min.x >> current->rect.offset_min.y >> current->rect.offset_max.x >> current->rect.offset_max.y
                    >> current->rect.pivot.x >> current->rect.pivot.y)) return fail(line_number, "invalid rect");
            } else if (command == "margin" || command == "padding") {
                UiInsets* target = command == "margin" ? &current->margin : &current->padding;
                if (!(stream >> target->left >> target->top >> target->right >> target->bottom)) return fail(line_number, "invalid insets");
            } else if (command == "canvas") {
                std::string mode; if (!(stream >> current->canvas.reference_resolution.x >> current->canvas.reference_resolution.y >> std::quoted(mode) >> current->canvas.match_width_or_height)) return fail(line_number, "invalid canvas");
                const auto parsed = scale_from_token(mode); if (!parsed) return fail(line_number, "invalid canvas scale mode"); current->canvas.scale_mode = *parsed;
            } else if (command == "visual") {
                if (!(stream >> current->visual.color[0] >> current->visual.color[1] >> current->visual.color[2] >> current->visual.color[3])) return fail(line_number, "invalid visual");
            } else if (command == "surface") {
                if (version < 3) return fail(line_number, "surface requires UI v3");
                std::string fit;
                if (!(stream >> current->surface.opacity >> current->surface.corner_radius >> current->surface.border_width)) return fail(line_number, "invalid surface");
                for (float& x : current->surface.border_color) if (!(stream >> x)) return fail(line_number, "invalid surface border color");
                if (!(stream >> current->surface.shadow_offset.x >> current->surface.shadow_offset.y >> current->surface.shadow_softness)) return fail(line_number, "invalid surface shadow");
                for (float& x : current->surface.shadow_color) if (!(stream >> x)) return fail(line_number, "invalid surface shadow color");
                if (!(stream >> std::quoted(fit) >> current->surface.nine_slice.left >> current->surface.nine_slice.top
                    >> current->surface.nine_slice.right >> current->surface.nine_slice.bottom)) return fail(line_number, "invalid surface image settings");
                const auto parsed = image_fit_from_token(fit); if (!parsed) return fail(line_number, "invalid surface image fit");
                current->surface.image_fit = *parsed;
                current->surface.opacity = std::clamp(current->surface.opacity, 0.0f, 1.0f);
                current->surface.corner_radius = std::max(0.0f, current->surface.corner_radius);
                current->surface.border_width = std::max(0.0f, current->surface.border_width);
                current->surface.shadow_softness = std::max(0.0f, current->surface.shadow_softness);
            } else if (command == "image_asset" || command == "font_asset") {
                std::string id,p; if (!(stream >> std::quoted(id) >> std::quoted(p))) return fail(line_number, "invalid asset reference");
                AssetReference ref; if (id != "-") ref.asset_id = id; if (p != "-") ref.path = p;
                if (command == "image_asset") current->visual.image = std::move(ref); else current->text.font = std::move(ref);
            } else if (command == "text") {
                std::string h,v; int rich=0;
                if (!(stream >> std::quoted(current->text.text) >> current->text.font_size >> current->text.color[0] >> current->text.color[1] >> current->text.color[2] >> current->text.color[3]
                    >> std::quoted(h) >> std::quoted(v) >> rich)) return fail(line_number, "invalid text");
                const auto hp = h_align_from_token(h);
                const auto vp = v_align_from_token(v);
                if (!hp || !vp) return fail(line_number, "invalid text alignment");
                current->text.horizontal_alignment=*hp; current->text.vertical_alignment=*vp; current->text.rich_text=rich!=0;
            } else if (command == "text_style") {
                if (version < 3) return fail(line_number, "text_style requires UI v3");
                int italic=0, wrap=0;
                if (!(stream >> std::quoted(current->text.font_family) >> current->text.font_weight >> italic >> wrap
                    >> current->text.line_spacing >> current->text.shadow_offset.x >> current->text.shadow_offset.y)) return fail(line_number, "invalid text_style");
                for (float& x : current->text.shadow_color) if (!(stream >> x)) return fail(line_number, "invalid text shadow color");
                current->text.font_weight = std::clamp<std::uint16_t>(current->text.font_weight, 100, 900);
                current->text.italic = italic != 0;
                current->text.wrap = wrap != 0;
                current->text.line_spacing = std::clamp(current->text.line_spacing, 0.5f, 4.0f);
                if (current->text.font_family.empty()) current->text.font_family = "Segoe UI";
            } else if (command == "button") {
                int enabled=0; if (!(stream >> enabled)) return fail(line_number, "invalid button"); current->button.interactable=enabled!=0;
                for (float& x : current->button.normal_color) if (!(stream >> x)) return fail(line_number, "invalid button normal color");
                for (float& x : current->button.hovered_color) if (!(stream >> x)) return fail(line_number, "invalid button hover color");
                for (float& x : current->button.pressed_color) if (!(stream >> x)) return fail(line_number, "invalid button pressed color");
                if (version >= 2) for (float& x : current->button.disabled_color) if (!(stream >> x)) return fail(line_number, "invalid button disabled color");
            } else if (command == "layout") {
                if (version < 2) return fail(line_number, "layout requires UI v2");
                std::string mode; int clip=0;
                if (!(stream >> std::quoted(mode) >> current->layout.spacing.x >> current->layout.spacing.y
                    >> current->layout.cell_size.x >> current->layout.cell_size.y >> current->layout.columns >> clip)) return fail(line_number, "invalid layout");
                const auto parsed = layout_from_token(mode); if (!parsed) return fail(line_number, "invalid layout mode");
                current->layout.mode=*parsed; current->layout.columns=std::max<std::uint32_t>(1,current->layout.columns); current->layout.clip_children=clip!=0;
            } else if (command == "progress") {
                if (version < 2) return fail(line_number, "progress requires UI v2");
                if (!(stream >> current->progress.value >> current->progress.minimum >> current->progress.maximum)) return fail(line_number, "invalid progress");
                for (float& x : current->progress.background_color) if (!(stream >> x)) return fail(line_number, "invalid progress background color");
                for (float& x : current->progress.fill_color) if (!(stream >> x)) return fail(line_number, "invalid progress fill color");
            } else if (command == "scroll") {
                if (version < 2 || !(stream >> current->scroll.offset.x >> current->scroll.offset.y)) return fail(line_number, "invalid scroll");
            } else if (command == "tabs") {
                if (version < 2 || !(stream >> current->tabs.active_index)) return fail(line_number, "invalid tabs");
            } else if (command == "input") {
                if (version < 2) return fail(line_number, "input requires UI v2");
                int read_only=0; if (!(stream >> std::quoted(current->input.placeholder) >> current->input.max_length >> read_only)) return fail(line_number, "invalid input");
                current->input.read_only=read_only!=0;
            } else return fail(line_number, "unknown UI command '" + command + "'");
        }
    }
    if (!header) return {false, "UI document is empty or missing header"};
    if (current) return fail(line_number, "unterminated node record");
    for (const auto& node : candidate.nodes()) {
        if (node.parent_id != kInvalidUiNodeId && !ids.contains(node.parent_id)) return {false, "UI node " + std::to_string(node.id) + " references a missing parent"};
        if (node.parent_id != kInvalidUiNodeId && candidate.is_descendant(node.parent_id, node.id)) return {false, "UI document contains a parenting cycle"};
        if (node.type == UiNodeType::Canvas && node.parent_id != kInvalidUiNodeId) return {false, "Canvas nodes must be roots"};
    }
    document = std::move(candidate);
    return {true, "loaded UI document v" + std::to_string(version) + ": " + path.string()};
}

} // namespace vespera
