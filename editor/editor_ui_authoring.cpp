#include "editor_ui_authoring.hpp"

#include "editor_asset_interactions.hpp"
#include "editor_assets.hpp"
#include "editor_console.hpp"
#include "editor_state.hpp"
#include "rml_source_editor.hpp"

#include <vespera/ui/ui_io.hpp>
#include <vespera/ui/ui_render.hpp>

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <string>

namespace vespera::editor {

const vespera::AssetRecord* ui_authoring_record(const EditorState& state) {
    if (state.ui_authoring.asset_id.empty()) return nullptr;
    return state.asset_catalog.find_by_id(state.ui_authoring.asset_id);
}

bool save_ui_authoring(EditorState& state) {
    auto& ui = state.ui_authoring;
    if (!ui.open || ui.path.empty()) return false;
    const auto saved = vespera::save_ui_document(ui.document, ui.path);
    ui.message = saved.message;
    if (!saved) {
        push_console(state, ConsoleEntry::Level::Error, "UI authoring save failed: " + saved.message);
        return false;
    }
    ui.dirty = false;
    refresh_asset_catalog(state, false, true);
    push_console(state, ConsoleEntry::Level::Info, saved.message);
    return true;
}

bool open_ui_authoring(EditorState& state, const vespera::AssetRecord& record) {
    if (record.kind != vespera::AssetKind::UiDocument) return false;
    auto& ui = state.ui_authoring;
    if (ui.dirty && !ui.path.empty()) {
        if (ui.path == record.absolute_path) {
            ui.open = true;
            return true;
        }
        if (!save_ui_authoring(state)) return false;
    }
    vespera::UiDocument document;
    const auto loaded = vespera::load_ui_document(document, record.absolute_path);
    if (!loaded) {
        ui.message = loaded.message;
        push_console(state, ConsoleEntry::Level::Error, "UI authoring open failed: " + loaded.message);
        return false;
    }
    ui.open = true;
    ui.asset_id = record.asset_id;
    ui.path = record.absolute_path;
    ui.document = std::move(document);
    ui.selected_node = vespera::kInvalidUiNodeId;
    for (const auto& node : ui.document.nodes()) {
        if (node.type == vespera::UiNodeType::Canvas) { ui.selected_node = node.id; break; }
    }
    if (ui.selected_node == vespera::kInvalidUiNodeId && !ui.document.nodes().empty())
        ui.selected_node = ui.document.nodes().front().id;
    ui.preview_width = 1280;
    ui.preview_height = 720;
    if (const auto* canvas = ui.document.find(ui.selected_node); canvas && canvas->type == vespera::UiNodeType::Canvas) {
        ui.preview_width = std::max(320, static_cast<int>(std::lround(canvas->canvas.reference_resolution.x)));
        ui.preview_height = std::max(200, static_cast<int>(std::lround(canvas->canvas.reference_resolution.y)));
    }
    ui.dirty = false;
    ui.preview_drag_mode = 0;
    ui.message = loaded.message;
    return true;
}

void mark_ui_dirty(EditorState& state) {
    state.ui_authoring.dirty = true;
}

bool ui_node_has_children(const vespera::UiDocument& document, vespera::UiNodeId id) {
    return std::any_of(document.nodes().begin(), document.nodes().end(),
        [id](const vespera::UiNode& node) { return node.parent_id == id; });
}

void draw_ui_authoring_tree_node(UiAuthoringState& ui, vespera::UiNodeId id) {
    const auto* node = ui.document.find(id);
    if (!node) return;
    const bool children = ui_node_has_children(ui.document, id);
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (!children) flags |= ImGuiTreeNodeFlags_Leaf;
    if (ui.selected_node == id) flags |= ImGuiTreeNodeFlags_Selected;
    ImGui::PushID(static_cast<int>(id));
    const std::string label = std::format("{}  [{}]", node->name, vespera::ui_node_type_name(node->type));
    const bool open = ImGui::TreeNodeEx("##ui_node", flags, "%s", label.c_str());
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) ui.selected_node = id;
    if (open) {
        for (const auto& child : ui.document.nodes()) {
            if (child.parent_id == id) draw_ui_authoring_tree_node(ui, child.id);
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void add_ui_authoring_node(EditorState& state, vespera::UiNodeType type) {
    auto& ui = state.ui_authoring;
    auto& node = ui.document.create_node(type);
    if (type != vespera::UiNodeType::Canvas) {
        vespera::UiNodeId parent = ui.selected_node;
        if (parent == vespera::kInvalidUiNodeId || !ui.document.find(parent)) {
            for (const auto& candidate : ui.document.nodes()) {
                if (candidate.type == vespera::UiNodeType::Canvas) { parent = candidate.id; break; }
            }
        }
        std::string error;
        if (parent != vespera::kInvalidUiNodeId && !ui.document.reparent(node.id, parent, &error)) {
            ui.message = error;
        }
    }
    ui.selected_node = node.id;
    ui.dirty = true;
}

void draw_ui_node_properties(EditorState& state) {
    auto& ui = state.ui_authoring;
    auto* node = ui.document.find(ui.selected_node);
    if (!node) {
        ImGui::TextDisabled("Select a UI node.");
        return;
    }

    ImGui::SeparatorText("Node");
    if (ImGui::InputText("Name", &node->name)) mark_ui_dirty(state);
    if (ImGui::Checkbox("Enabled", &node->enabled)) mark_ui_dirty(state);
    if (ImGui::DragInt("Z Order", &node->z_order, 1.0f)) mark_ui_dirty(state);

    std::string parent_label = "<root>";
    if (const auto* parent = ui.document.find(node->parent_id)) parent_label = parent->name;
    if (node->type != vespera::UiNodeType::Canvas && ImGui::BeginCombo("Parent", parent_label.c_str())) {
        const bool root_selected = node->parent_id == vespera::kInvalidUiNodeId;
        if (ImGui::Selectable("<root>", root_selected)) {
            std::string error;
            if (ui.document.reparent(node->id, vespera::kInvalidUiNodeId, &error)) mark_ui_dirty(state);
            else ui.message = error;
        }
        for (const auto& candidate : ui.document.nodes()) {
            if (candidate.id == node->id || ui.document.is_descendant(candidate.id, node->id)) continue;
            const bool selected = node->parent_id == candidate.id;
            if (ImGui::Selectable(candidate.name.c_str(), selected)) {
                std::string error;
                if (ui.document.reparent(node->id, candidate.id, &error)) mark_ui_dirty(state);
                else ui.message = error;
            }
        }
        ImGui::EndCombo();
    }

    ImGui::SeparatorText("Rect Transform");
    if (ImGui::Button("Top Left")) {
        node->rect.anchor_min = node->rect.anchor_max = {0.0f, 0.0f}; mark_ui_dirty(state);
    }
    ImGui::SameLine();
    if (ImGui::Button("Center")) {
        node->rect.anchor_min = node->rect.anchor_max = {0.5f, 0.5f}; mark_ui_dirty(state);
    }
    ImGui::SameLine();
    if (ImGui::Button("Stretch")) {
        node->rect.anchor_min = {0.0f, 0.0f}; node->rect.anchor_max = {1.0f, 1.0f}; mark_ui_dirty(state);
    }
    float anchor_min[2]{node->rect.anchor_min.x, node->rect.anchor_min.y};
    float anchor_max[2]{node->rect.anchor_max.x, node->rect.anchor_max.y};
    float offset_min[2]{node->rect.offset_min.x, node->rect.offset_min.y};
    float offset_max[2]{node->rect.offset_max.x, node->rect.offset_max.y};
    if (ImGui::DragFloat2("Anchor Min", anchor_min, 0.01f, 0.0f, 1.0f, "%.2f")) {
        node->rect.anchor_min = {anchor_min[0], anchor_min[1]}; mark_ui_dirty(state);
    }
    if (ImGui::DragFloat2("Anchor Max", anchor_max, 0.01f, 0.0f, 1.0f, "%.2f")) {
        node->rect.anchor_max = {anchor_max[0], anchor_max[1]}; mark_ui_dirty(state);
    }
    if (ImGui::DragFloat2("Offset Min", offset_min, 1.0f)) {
        node->rect.offset_min = {offset_min[0], offset_min[1]}; mark_ui_dirty(state);
    }
    if (ImGui::DragFloat2("Offset Max", offset_max, 1.0f)) {
        node->rect.offset_max = {offset_max[0], offset_max[1]}; mark_ui_dirty(state);
    }
    float margin[4]{node->margin.left,node->margin.top,node->margin.right,node->margin.bottom};
    float padding[4]{node->padding.left,node->padding.top,node->padding.right,node->padding.bottom};
    if (ImGui::DragFloat4("Margin LTRB", margin, 0.5f)) {
        node->margin={margin[0],margin[1],margin[2],margin[3]}; mark_ui_dirty(state);
    }
    if (ImGui::DragFloat4("Padding LTRB", padding, 0.5f)) {
        node->padding={padding[0],padding[1],padding[2],padding[3]}; mark_ui_dirty(state);
    }

    if (node->type == vespera::UiNodeType::Canvas) {
        ImGui::SeparatorText("Canvas");
        float reference[2]{node->canvas.reference_resolution.x,node->canvas.reference_resolution.y};
        if (ImGui::DragFloat2("Reference Resolution", reference, 1.0f, 1.0f, 8192.0f, "%.0f")) {
            node->canvas.reference_resolution={reference[0],reference[1]}; mark_ui_dirty(state);
        }
        if (ImGui::SliderFloat("Match Width / Height", &node->canvas.match_width_or_height, 0.0f, 1.0f)) mark_ui_dirty(state);
    }

    if (node->type != vespera::UiNodeType::Canvas && node->type != vespera::UiNodeType::Text
        && node->type != vespera::UiNodeType::Button && node->type != vespera::UiNodeType::ProgressBar) {
        ImGui::SeparatorText("Visual");
        if (ImGui::ColorEdit4("Color", node->visual.color.data())) mark_ui_dirty(state);
    }
    if (node->type != vespera::UiNodeType::Canvas) {
        ImGui::SeparatorText("Surface Style");
        if (ImGui::SliderFloat("Opacity", &node->surface.opacity, 0.0f, 1.0f)) mark_ui_dirty(state);
        if (ImGui::DragFloat("Corner Radius", &node->surface.corner_radius, 0.5f, 0.0f, 256.0f)) mark_ui_dirty(state);
        if (ImGui::DragFloat("Border Width", &node->surface.border_width, 0.25f, 0.0f, 64.0f)) mark_ui_dirty(state);
        if (ImGui::ColorEdit4("Border Color", node->surface.border_color.data())) mark_ui_dirty(state);
        float shadow_offset[2]{node->surface.shadow_offset.x,node->surface.shadow_offset.y};
        if (ImGui::DragFloat2("Shadow Offset", shadow_offset, 0.5f, -128.0f, 128.0f)) {
            node->surface.shadow_offset={shadow_offset[0],shadow_offset[1]}; mark_ui_dirty(state);
        }
        if (ImGui::DragFloat("Shadow Softness", &node->surface.shadow_softness, 0.5f, 0.0f, 32.0f)) mark_ui_dirty(state);
        if (ImGui::ColorEdit4("Shadow Color", node->surface.shadow_color.data())) mark_ui_dirty(state);
        int image_fit=static_cast<int>(node->surface.image_fit);
        const char* image_fit_names[]={"Stretch","Contain","Cover"};
        if (ImGui::Combo("Image Fit", &image_fit, image_fit_names, 3)) {
            node->surface.image_fit=static_cast<vespera::UiImageFit>(image_fit); mark_ui_dirty(state);
        }
        float slice[4]{node->surface.nine_slice.left,node->surface.nine_slice.top,node->surface.nine_slice.right,node->surface.nine_slice.bottom};
        if (ImGui::DragFloat4("9-Slice LTRB", slice, 0.5f, 0.0f, 2048.0f)) {
            node->surface.nine_slice={slice[0],slice[1],slice[2],slice[3]}; mark_ui_dirty(state);
        }
    }

    if (node->type == vespera::UiNodeType::Text || node->type == vespera::UiNodeType::Button
        || node->type == vespera::UiNodeType::ProgressBar || node->type == vespera::UiNodeType::TextInput
        || node->type == vespera::UiNodeType::Modal || node->type == vespera::UiNodeType::Tooltip
        || node->type == vespera::UiNodeType::Tabs) {
        ImGui::SeparatorText("Text");
        if (ImGui::InputTextMultiline("Text", &node->text.text, ImVec2(-1.0f, 64.0f))) mark_ui_dirty(state);
        if (ImGui::InputText("Font Family", &node->text.font_family)) mark_ui_dirty(state);
        if (ImGui::DragFloat("Font Size", &node->text.font_size, 0.5f, 4.0f, 256.0f)) mark_ui_dirty(state);
        int font_weight=static_cast<int>(node->text.font_weight);
        if (ImGui::SliderInt("Font Weight", &font_weight, 100, 900)) {
            node->text.font_weight=static_cast<std::uint16_t>(std::clamp(font_weight,100,900)); mark_ui_dirty(state);
        }
        if (ImGui::Checkbox("Italic", &node->text.italic)) mark_ui_dirty(state);
        ImGui::SameLine();
        if (ImGui::Checkbox("Word Wrap", &node->text.wrap)) mark_ui_dirty(state);
        if (ImGui::DragFloat("Line Spacing", &node->text.line_spacing, 0.02f, 0.5f, 4.0f)) mark_ui_dirty(state);
        if (ImGui::ColorEdit4("Text Color", node->text.color.data())) mark_ui_dirty(state);
        int h_align=static_cast<int>(node->text.horizontal_alignment);
        const char* h_names[]={"Left","Center","Right"};
        if (ImGui::Combo("Horizontal Align", &h_align, h_names, 3)) {
            node->text.horizontal_alignment=static_cast<vespera::UiHorizontalAlignment>(h_align); mark_ui_dirty(state);
        }
        int v_align=static_cast<int>(node->text.vertical_alignment);
        const char* v_names[]={"Top","Middle","Bottom"};
        if (ImGui::Combo("Vertical Align", &v_align, v_names, 3)) {
            node->text.vertical_alignment=static_cast<vespera::UiVerticalAlignment>(v_align); mark_ui_dirty(state);
        }
        float text_shadow[2]{node->text.shadow_offset.x,node->text.shadow_offset.y};
        if (ImGui::DragFloat2("Text Shadow Offset", text_shadow, 0.5f, -64.0f, 64.0f)) {
            node->text.shadow_offset={text_shadow[0],text_shadow[1]}; mark_ui_dirty(state);
        }
        if (ImGui::ColorEdit4("Text Shadow Color", node->text.shadow_color.data())) mark_ui_dirty(state);
    }

    const bool text_capable = node->type == vespera::UiNodeType::Text || node->type == vespera::UiNodeType::Button
        || node->type == vespera::UiNodeType::ProgressBar || node->type == vespera::UiNodeType::TextInput
        || node->type == vespera::UiNodeType::Modal || node->type == vespera::UiNodeType::Tooltip
        || node->type == vespera::UiNodeType::Tabs;
    const bool image_capable = node->type != vespera::UiNodeType::Canvas && node->type != vespera::UiNodeType::Text
        && node->type != vespera::UiNodeType::ProgressBar;
    if (image_capable || text_capable) {
        ImGui::SeparatorText("Project Assets");
        if (image_capable) {
            const auto resolved = state.asset_catalog.resolve_reference(node->visual.image);
            const std::string label = resolved ? resolved.record->display_name : (node->visual.image.empty() ? "None" : node->visual.image.path.generic_string());
            ImGui::TextUnformatted("Image"); ImGui::SameLine(72.0f);
            ImGui::Button((label + "##UiImageAsset").c_str(), ImVec2(-28.0f, 0.0f));
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("VESPERA_PROJECT_ASSET")) {
                    if (const auto* record = asset_record_from_payload(state, payload); record && record->kind == vespera::AssetKind::Texture) {
                        node->visual.image = {record->asset_id, record->relative_path}; mark_ui_dirty(state);
                    }
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("X##ClearUiImage")) { node->visual.image = {}; mark_ui_dirty(state); }
        }
        if (text_capable) {
            const auto resolved = state.asset_catalog.resolve_reference(node->text.font);
            const std::string label = resolved ? resolved.record->display_name : (node->text.font.empty() ? "Fallback" : node->text.font.path.generic_string());
            ImGui::TextUnformatted("Font"); ImGui::SameLine(72.0f);
            ImGui::Button((label + "##UiFontAsset").c_str(), ImVec2(-28.0f, 0.0f));
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("VESPERA_PROJECT_ASSET")) {
                    if (const auto* record = asset_record_from_payload(state, payload); record && record->kind == vespera::AssetKind::Font) {
                        node->text.font = {record->asset_id, record->relative_path}; mark_ui_dirty(state);
                    }
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("X##ClearUiFont")) { node->text.font = {}; mark_ui_dirty(state); }
        }
    }

    if (node->type == vespera::UiNodeType::Button) {
        ImGui::SeparatorText("Button");
        if (ImGui::Checkbox("Interactable", &node->button.interactable)) mark_ui_dirty(state);
        if (ImGui::ColorEdit4("Normal", node->button.normal_color.data())) mark_ui_dirty(state);
        if (ImGui::ColorEdit4("Hovered", node->button.hovered_color.data())) mark_ui_dirty(state);
        if (ImGui::ColorEdit4("Pressed", node->button.pressed_color.data())) mark_ui_dirty(state);
        if (ImGui::ColorEdit4("Disabled", node->button.disabled_color.data())) mark_ui_dirty(state);
    }

    if (node->type == vespera::UiNodeType::ProgressBar) {
        ImGui::SeparatorText("Progress Bar");
        if (ImGui::DragFloat("Value", &node->progress.value, 0.01f)) mark_ui_dirty(state);
        if (ImGui::DragFloat("Minimum", &node->progress.minimum, 0.01f)) mark_ui_dirty(state);
        if (ImGui::DragFloat("Maximum", &node->progress.maximum, 0.01f)) mark_ui_dirty(state);
        if (ImGui::ColorEdit4("Background", node->progress.background_color.data())) mark_ui_dirty(state);
        if (ImGui::ColorEdit4("Fill", node->progress.fill_color.data())) mark_ui_dirty(state);
    }

    ImGui::SeparatorText("Layout");
    int layout_mode = static_cast<int>(node->layout.mode);
    const char* layout_names[] = {"None","Horizontal","Vertical","Grid"};
    if (ImGui::Combo("Layout Mode", &layout_mode, layout_names, 4)) {
        node->layout.mode = static_cast<vespera::UiLayoutMode>(layout_mode); mark_ui_dirty(state);
    }
    float spacing[2]{node->layout.spacing.x,node->layout.spacing.y};
    float cell[2]{node->layout.cell_size.x,node->layout.cell_size.y};
    if (ImGui::DragFloat2("Spacing", spacing, 0.5f, 0.0f, 256.0f)) {
        node->layout.spacing={spacing[0],spacing[1]}; mark_ui_dirty(state);
    }
    if (ImGui::DragFloat2("Cell Size (0=auto)", cell, 0.5f, 0.0f, 2048.0f)) {
        node->layout.cell_size={cell[0],cell[1]}; mark_ui_dirty(state);
    }
    int columns = static_cast<int>(node->layout.columns);
    if (ImGui::DragInt("Columns", &columns, 1.0f, 1, 64)) {
        node->layout.columns=static_cast<std::uint32_t>(std::max(1,columns)); mark_ui_dirty(state);
    }
    if (ImGui::Checkbox("Clip Children", &node->layout.clip_children)) mark_ui_dirty(state);

    if (node->type == vespera::UiNodeType::ScrollView) {
        ImGui::SeparatorText("Scroll View");
        float offset[2]{node->scroll.offset.x,node->scroll.offset.y};
        if (ImGui::DragFloat2("Scroll Offset", offset, 1.0f, 0.0f, 100000.0f)) {
            node->scroll.offset={offset[0],offset[1]}; mark_ui_dirty(state);
        }
    }
    if (node->type == vespera::UiNodeType::Tabs) {
        ImGui::SeparatorText("Tabs");
        int active=static_cast<int>(node->tabs.active_index);
        if (ImGui::DragInt("Active Child", &active, 1.0f, 0, 128)) {
            node->tabs.active_index=static_cast<std::uint32_t>(std::max(0,active)); mark_ui_dirty(state);
        }
    }
    if (node->type == vespera::UiNodeType::TextInput) {
        ImGui::SeparatorText("Text Input");
        if (ImGui::InputText("Placeholder", &node->input.placeholder)) mark_ui_dirty(state);
        int max_len=static_cast<int>(node->input.max_length);
        if (ImGui::DragInt("Max Length", &max_len, 1.0f, 1, 65536)) {
            node->input.max_length=static_cast<std::uint32_t>(std::max(1,max_len)); mark_ui_dirty(state);
        }
        if (ImGui::Checkbox("Read Only", &node->input.read_only)) mark_ui_dirty(state);
    }
}

ImU32 ui_preview_color(const vespera::UiNode& node) {
    auto c = node.visual.color;
    if (node.type == vespera::UiNodeType::Button) c = node.button.normal_color;
    else if (node.type == vespera::UiNodeType::ProgressBar) c = node.progress.background_color;
    return ImGui::GetColorU32(ImVec4(c[0],c[1],c[2],std::max(0.08f,c[3] * std::clamp(node.surface.opacity,0.0f,1.0f))));
}

void draw_ui_authoring_preview(EditorState& state) {
    auto& ui = state.ui_authoring;
    const auto layout = vespera::resolve_ui_layout(ui.document,
        static_cast<float>(ui.preview_width), static_cast<float>(ui.preview_height));

    ImVec2 avail = ImGui::GetContentRegionAvail();
    avail.x = std::max(avail.x, 120.0f); avail.y = std::max(avail.y, 120.0f);
    const float fit = std::max(0.05f, std::min(avail.x / static_cast<float>(ui.preview_width),
                                               avail.y / static_cast<float>(ui.preview_height)));
    const ImVec2 size{static_cast<float>(ui.preview_width)*fit, static_cast<float>(ui.preview_height)*fit};
    const ImVec2 origin{ImGui::GetCursorScreenPos().x + std::max(0.0f,(avail.x-size.x)*0.5f),
                        ImGui::GetCursorScreenPos().y + std::max(0.0f,(avail.y-size.y)*0.5f)};
    ImGui::SetCursorScreenPos(origin);
    ImGui::InvisibleButton("##ui_authoring_canvas", size);
    const bool hovered=ImGui::IsItemHovered();
    ImDrawList* draw=ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin,{origin.x+size.x,origin.y+size.y},ImGui::GetColorU32(ImVec4(0.025f,0.030f,0.042f,1.0f)));
    draw->AddRect(origin,{origin.x+size.x,origin.y+size.y},ImGui::GetColorU32(ImVec4(0.30f,0.34f,0.44f,1.0f)));

    for (const auto& resolved : layout.nodes) {
        if (!resolved.enabled || resolved.type == vespera::UiNodeType::Canvas) continue;
        const auto* node=ui.document.find(resolved.id); if(!node) continue;
        const ImVec2 a{origin.x+resolved.rect.x*fit,origin.y+resolved.rect.y*fit};
        const ImVec2 b{a.x+resolved.rect.width*fit,a.y+resolved.rect.height*fit};
        const float rounding=std::max(0.0f,node->surface.corner_radius*fit);
        if(node->surface.shadow_color[3]>0.001f){
            auto sc=node->surface.shadow_color;sc[3]*=std::clamp(node->surface.opacity,0.0f,1.0f);
            const ImVec2 sa{a.x+node->surface.shadow_offset.x*fit,a.y+node->surface.shadow_offset.y*fit};
            const ImVec2 sb{b.x+node->surface.shadow_offset.x*fit,b.y+node->surface.shadow_offset.y*fit};
            draw->AddRectFilled(sa,sb,ImGui::GetColorU32(ImVec4(sc[0],sc[1],sc[2],sc[3])),rounding);
        }
        draw->AddRectFilled(a,b,ui_preview_color(*node),rounding);
        if(node->surface.border_width>0.01f&&node->surface.border_color[3]>0.001f){
            auto bc=node->surface.border_color;bc[3]*=std::clamp(node->surface.opacity,0.0f,1.0f);
            draw->AddRect(a,b,ImGui::GetColorU32(ImVec4(bc[0],bc[1],bc[2],bc[3])),rounding,0,std::max(1.0f,node->surface.border_width*fit));
        }
        if (node->type == vespera::UiNodeType::ProgressBar) {
            const float range=std::max(0.0001f,node->progress.maximum-node->progress.minimum);
            const float t=std::clamp((node->progress.value-node->progress.minimum)/range,0.0f,1.0f);
            auto fc=node->progress.fill_color;
            draw->AddRectFilled(a,{a.x+(b.x-a.x)*t,b.y},ImGui::GetColorU32(ImVec4(fc[0],fc[1],fc[2],fc[3])),rounding);
        }
        if (node->type == vespera::UiNodeType::Image) {
            draw->AddText({a.x+4.0f,a.y+3.0f},ImGui::GetColorU32(ImGuiCol_TextDisabled),"IMG");
        } else {
            const std::string text = node->text.text.empty() ? node->name : node->text.text;
            if (!text.empty() && (b.x-a.x)>18.0f && (b.y-a.y)>12.0f) {
                auto tc=node->text.color;tc[3]*=std::clamp(node->surface.opacity,0.0f,1.0f);
                const ImU32 text_color=ImGui::GetColorU32(ImVec4(tc[0],tc[1],tc[2],tc[3]));
                if(node->text.shadow_color[3]>0.001f){auto sc=node->text.shadow_color;draw->AddText({a.x+4.0f+node->text.shadow_offset.x*fit,a.y+3.0f+node->text.shadow_offset.y*fit},ImGui::GetColorU32(ImVec4(sc[0],sc[1],sc[2],sc[3])),text.c_str());}
                draw->AddText({a.x+4.0f,a.y+3.0f},text_color,text.c_str());
            }
        }
        if (ui.selected_node == node->id) {
            draw->AddRect(a,b,ImGui::GetColorU32(ImVec4(1.0f,0.78f,0.30f,1.0f)),2.0f,0,2.0f);
            draw->AddRectFilled({b.x-6.0f,b.y-6.0f},{b.x+2.0f,b.y+2.0f},ImGui::GetColorU32(ImVec4(1.0f,0.78f,0.30f,1.0f)));
        }
    }

    const ImGuiIO& io=ImGui::GetIO();
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const vespera::UiVec2 point{(io.MousePos.x-origin.x)/fit,(io.MousePos.y-origin.y)/fit};
        const auto* selected_resolved=layout.find(ui.selected_node);
        if (selected_resolved) {
            const ImVec2 corner{origin.x+(selected_resolved->rect.x+selected_resolved->rect.width)*fit,
                                origin.y+(selected_resolved->rect.y+selected_resolved->rect.height)*fit};
            const float dx=io.MousePos.x-corner.x,dy=io.MousePos.y-corner.y;
            if (dx*dx+dy*dy <= 12.0f*12.0f) ui.preview_drag_mode=2;
        }
        if (ui.preview_drag_mode==0) {
            ui.selected_node=vespera::kInvalidUiNodeId;
            for (auto it=layout.nodes.rbegin();it!=layout.nodes.rend();++it) {
                if (!it->enabled || it->type==vespera::UiNodeType::Canvas) continue;
                const auto& r=it->rect;
                if(point.x>=r.x&&point.y>=r.y&&point.x<=r.x+r.width&&point.y<=r.y+r.height){ui.selected_node=it->id;break;}
            }
            if (ui.selected_node!=vespera::kInvalidUiNodeId) ui.preview_drag_mode=1;
        }
    }
    if (ui.preview_drag_mode!=0 && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (auto* node=ui.document.find(ui.selected_node)) {
            bool managed_by_parent=false;
            if (const auto* parent=ui.document.find(node->parent_id)) {
                const auto mode=parent->layout.mode;
                managed_by_parent=mode!=vespera::UiLayoutMode::None || parent->type==vespera::UiNodeType::List || parent->type==vespera::UiNodeType::Grid;
            }
            if (!managed_by_parent) {
                const float dx=io.MouseDelta.x/fit,dy=io.MouseDelta.y/fit;
                if (std::abs(dx)>0.0f||std::abs(dy)>0.0f) {
                    if(ui.preview_drag_mode==1){
                        node->rect.offset_min.x+=dx;node->rect.offset_min.y+=dy;
                        node->rect.offset_max.x+=dx;node->rect.offset_max.y+=dy;
                    } else {
                        node->rect.offset_max.x+=dx;node->rect.offset_max.y+=dy;
                    }
                    mark_ui_dirty(state);
                }
            }
        }
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) ui.preview_drag_mode=0;
}

void draw_rml_source_authoring(EditorState& state) {
    if (!vespera::editor::draw_rml_source_editor(state.rml_source_editor)) return;
    push_console(state, ConsoleEntry::Level::Info,
        "RmlUi source saved: " + state.rml_source_editor.path.generic_string());
    refresh_asset_catalog(state, false, true);

    if (state.play_rml_ui_loaded && state.play_rml_ui && state.play_rml_ui->initialized()) {
        std::error_code ec;
        const auto active = std::filesystem::absolute(state.play_rml_ui->document_path(), ec).lexically_normal();
        ec.clear();
        const auto edited = std::filesystem::absolute(state.rml_source_editor.path, ec).lexically_normal();
        bool reload_active = !ec && active == edited;
        if (!reload_active && !state.rml_source_editor.asset_id.empty()) {
            std::error_code relative_ec;
            const auto active_relative = std::filesystem::relative(active, state.asset_catalog.root(), relative_ec);
            if (!relative_ec) {
                if (const auto* active_record = state.asset_catalog.find(active_relative.generic_string())) {
                    for (const auto* dependency : state.asset_catalog.dependencies_of(active_record->asset_id)) {
                        if (dependency && dependency->target_asset_id == state.rml_source_editor.asset_id) {
                            reload_active = true;
                            break;
                        }
                    }
                }
            }
        }
        if (reload_active) {
            if (state.play_rml_ui->reload())
                push_console(state, ConsoleEntry::Level::Info, "Play Mode RML reloaded after source save.");
            else
                push_console(state, ConsoleEntry::Level::Warning, "Play Mode RML reload failed after source save.");
        }
    }
}

void draw_ui_authoring(EditorState& state) {
    auto& ui=state.ui_authoring;
    if(!ui.open) return;
    bool open=ui.open;
    std::string title=std::format("UI Authoring{}###VesperaUiAuthoring",ui.dirty?" *":"");
    if(!ImGui::Begin(title.c_str(),&open)){ImGui::End();ui.open=open;return;}
    ui.open=open;
    if(ImGui::Button("Save")) save_ui_authoring(state);
    ImGui::SameLine();
    ImGui::BeginDisabled(ui.dirty);
    if(ImGui::Button("Reload")) {
        if(const auto* record=ui_authoring_record(state)) open_ui_authoring(state,*record);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if(ImGui::Button("Add")) ImGui::OpenPopup("##ui_add_node");
    if(ImGui::BeginPopup("##ui_add_node")) {
        const std::array<vespera::UiNodeType,12> types{
            vespera::UiNodeType::Panel,vespera::UiNodeType::Text,vespera::UiNodeType::Image,
            vespera::UiNodeType::Button,vespera::UiNodeType::ProgressBar,vespera::UiNodeType::ScrollView,
            vespera::UiNodeType::List,vespera::UiNodeType::Grid,vespera::UiNodeType::Tabs,
            vespera::UiNodeType::Modal,vespera::UiNodeType::Tooltip,vespera::UiNodeType::TextInput};
        for(const auto type:types) if(ImGui::MenuItem(std::string(vespera::ui_node_type_name(type)).c_str())) add_ui_authoring_node(state,type);
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    bool can_delete=false;
    if(const auto* n=ui.document.find(ui.selected_node)) can_delete=n->type!=vespera::UiNodeType::Canvas;
    if(!can_delete) ImGui::BeginDisabled();
    if(ImGui::Button("Delete")) {ui.document.destroy_node(ui.selected_node);ui.selected_node=vespera::kInvalidUiNodeId;ui.dirty=true;}
    if(!can_delete) ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    const char* preset = (ui.preview_width==1920&&ui.preview_height==1080)?"1920 x 1080":"1280 x 720";
    if(ImGui::BeginCombo("Preview",preset)) {
        if(ImGui::Selectable("1280 x 720",ui.preview_width==1280&&ui.preview_height==720)){ui.preview_width=1280;ui.preview_height=720;}
        if(ImGui::Selectable("1920 x 1080",ui.preview_width==1920&&ui.preview_height==1080)){ui.preview_width=1920;ui.preview_height=1080;}
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s",ui.path.filename().string().c_str());
    if(!ui.message.empty()) ImGui::TextDisabled("%s",ui.message.c_str());

    const float left=220.0f,right=330.0f;
    ImGui::BeginChild("##ui_hierarchy",ImVec2(left,0.0f),true);
    ImGui::SeparatorText("UI Hierarchy");
    for(const auto& node:ui.document.nodes()) if(node.parent_id==vespera::kInvalidUiNodeId) draw_ui_authoring_tree_node(ui,node.id);
    ImGui::EndChild();
    ImGui::SameLine();
    const float center=std::max(160.0f,ImGui::GetContentRegionAvail().x-right-8.0f);
    ImGui::BeginChild("##ui_preview",ImVec2(center,0.0f),true);
    ImGui::TextDisabled("Click to select | drag to move | drag lower-right handle to resize");
    draw_ui_authoring_preview(state);
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##ui_properties",ImVec2(0.0f,0.0f),true);
    draw_ui_node_properties(state);
    ImGui::EndChild();
    ImGui::End();
}


} // namespace vespera::editor
