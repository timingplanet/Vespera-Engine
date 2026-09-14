#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vespera::editor {

// 0.8.10 establishes the host-side contract only. Dynamic DLL loading is
// intentionally deferred until the command surface has been dogfooded in 0.9.x.
// Extensions must register semantic commands; they do not receive raw Scene,
// renderer, undo-stack, CLR, or native pointer access through this API.
inline constexpr std::uint32_t kEditorExtensionApiVersion = 1;

struct EditorExtensionDescriptor {
    std::string id;
    std::string display_name;
    std::string version;
    std::uint32_t required_api_version = kEditorExtensionApiVersion;
};

struct EditorExtensionCommandDescriptor {
    std::string extension_id;
    std::string name;
    std::string description;
    bool mutating = false;
    bool undoable = false;
    bool allowed_during_play = false;
};

class EditorExtensionRegistry {
public:
    bool register_extension(EditorExtensionDescriptor descriptor, std::string* error = nullptr) {
        if (descriptor.id.empty() || descriptor.display_name.empty() || descriptor.version.empty()) {
            if (error) *error = "extension id, display name and version are required";
            return false;
        }
        if (descriptor.required_api_version != kEditorExtensionApiVersion) {
            if (error) *error = "extension API version mismatch";
            return false;
        }
        if (find_extension(descriptor.id)) {
            if (error) *error = "extension id is already registered";
            return false;
        }
        extensions_.push_back(std::move(descriptor));
        return true;
    }

    bool register_command(EditorExtensionCommandDescriptor descriptor, std::string* error = nullptr) {
        if (!find_extension(descriptor.extension_id)) {
            if (error) *error = "command extension is not registered";
            return false;
        }
        if (descriptor.name.empty() || descriptor.description.empty()) {
            if (error) *error = "command name and description are required";
            return false;
        }
        if (find_command(descriptor.name)) {
            if (error) *error = "command name is already registered";
            return false;
        }
        commands_.push_back(std::move(descriptor));
        return true;
    }

    [[nodiscard]] const EditorExtensionDescriptor* find_extension(std::string_view id) const {
        const auto it = std::find_if(extensions_.begin(), extensions_.end(), [id](const auto& item) { return item.id == id; });
        return it == extensions_.end() ? nullptr : &*it;
    }

    [[nodiscard]] const EditorExtensionCommandDescriptor* find_command(std::string_view name) const {
        const auto it = std::find_if(commands_.begin(), commands_.end(), [name](const auto& item) { return item.name == name; });
        return it == commands_.end() ? nullptr : &*it;
    }

    [[nodiscard]] const std::vector<EditorExtensionDescriptor>& extensions() const { return extensions_; }
    [[nodiscard]] const std::vector<EditorExtensionCommandDescriptor>& commands() const { return commands_; }

private:
    std::vector<EditorExtensionDescriptor> extensions_;
    std::vector<EditorExtensionCommandDescriptor> commands_;
};

} // namespace vespera::editor
