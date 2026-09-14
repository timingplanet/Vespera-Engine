#include "rml_source_editor.hpp"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include <fstream>
#include <system_error>
#include <utility>

namespace vespera::editor {
namespace {

constexpr std::uintmax_t kMaxEditableSourceBytes = 2u * 1024u * 1024u;

bool supported_path(const std::filesystem::path& path) {
    const auto ext = path.extension().string();
    return ext == ".rml" || ext == ".rcss";
}

bool read_source(const std::filesystem::path& path, std::string& text, std::string& error) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) {
        error = "Could not inspect UI source: " + ec.message();
        return false;
    }
    if (size > kMaxEditableSourceBytes) {
        error = "RML/RCSS source is larger than the 2 MiB editor limit.";
        return false;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "Could not open UI source for reading.";
        return false;
    }
    text.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    if (!input.eof() && input.fail()) {
        error = "Could not read the complete UI source.";
        return false;
    }
    return true;
}

bool write_source(const std::filesystem::path& path, std::string_view text, std::string& error) {
    const auto temp = path.string() + ".vespera-tmp";
    {
        std::ofstream output(temp, std::ios::binary | std::ios::trunc);
        if (!output) {
            error = "Could not open temporary UI source for writing.";
            return false;
        }
        output.write(text.data(), static_cast<std::streamsize>(text.size()));
        output.flush();
        if (!output) {
            error = "Could not write the complete temporary UI source.";
            return false;
        }
    }

    std::error_code ec;
    std::filesystem::copy_file(temp, path, std::filesystem::copy_options::overwrite_existing, ec);
    std::error_code cleanup_ec;
    std::filesystem::remove(temp, cleanup_ec);
    if (ec) {
        error = "Could not replace UI source: " + ec.message();
        return false;
    }
    return true;
}

} // namespace

bool open_rml_source_editor(
    RmlSourceEditorState& state,
    const std::filesystem::path& path,
    std::string asset_id,
    std::string* error) {
    if (!supported_path(path)) {
        if (error) *error = "RML source editor accepts only .rml or .rcss assets.";
        return false;
    }

    const auto normalized = std::filesystem::absolute(path).lexically_normal();
    if (!state.path.empty() && state.path != normalized && state.dirty()) {
        state.window_open = true;
        if (error) *error = "Save or Revert the current RML/RCSS edits before opening another asset.";
        return false;
    }
    if (state.path == normalized && state.asset_id == asset_id && state.dirty()) {
        state.window_open = true;
        return true;
    }

    std::string text;
    std::string message;
    if (!read_source(normalized, text, message)) {
        if (error) *error = std::move(message);
        return false;
    }

    state.path = normalized;
    state.asset_id = std::move(asset_id);
    state.text = std::move(text);
    state.saved_text = state.text;
    state.status = "Loaded " + state.path.filename().string();
    state.window_open = true;
    return true;
}

bool draw_rml_source_editor(RmlSourceEditorState& state) {
    if (!state.window_open) return false;

    bool saved = false;
    std::string title = "RML / RCSS Source";
    if (state.dirty()) title += " *";
    title += "###RmlSourceEditor";

    if (!ImGui::Begin(title.c_str(), &state.window_open)) {
        ImGui::End();
        return false;
    }

    ImGui::TextDisabled("%s", state.path.generic_string().c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("| standard RmlUi source");

    if (ImGui::Button("Save")) {
        std::string error;
        if (write_source(state.path, state.text, error)) {
            state.saved_text = state.text;
            state.status = "Saved " + state.path.filename().string();
            saved = true;
        } else {
            state.status = std::move(error);
        }
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!state.dirty());
    if (ImGui::Button("Revert")) {
        std::string text;
        std::string error;
        if (read_source(state.path, text, error)) {
            state.text = std::move(text);
            state.saved_text = state.text;
            state.status = "Reverted to disk";
        } else {
            state.status = std::move(error);
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("%zu bytes", state.text.size());

    if (!state.status.empty()) ImGui::TextWrapped("%s", state.status.c_str());
    ImGui::Separator();

    const ImVec2 available = ImGui::GetContentRegionAvail();
    ImGui::InputTextMultiline(
        "##RmlSourceText",
        &state.text,
        ImVec2(available.x, (available.y > 80.0f ? available.y : 80.0f)),
        ImGuiInputTextFlags_AllowTabInput);

    ImGui::End();
    return saved;
}

} // namespace vespera::editor
