#include "editor_managed.hpp"
#include "editor_console.hpp"
#include "editor_installation.hpp"
#include <vespera/platform/process.hpp>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>
namespace vespera::editor {

bool load_managed_metadata(EditorState& state, bool report) {
    ManagedMetadataCatalog catalog;
    if (!state.project_loaded || state.project.root_directory.empty()) {
        state.managed_metadata = std::move(catalog);
        if (report) push_console(state, ConsoleEntry::Level::Info, "Open a project to load C# metadata.");
        return false;
    }
    catalog.path = state.project.root_directory / ".vespera" / "managed" / "Vespera.ScriptMetadata.txt";
    std::ifstream input(catalog.path);
    if (!input) {
        state.managed_metadata = std::move(catalog);
        if (report) push_console(state, ConsoleEntry::Level::Warning, "C# metadata not found. Use Build C# to generate it.");
        return false;
    }
    ManagedScriptMetadata* current = nullptr;
    std::string line; int version = 0;
    while (std::getline(input, line)) {
        const auto first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos || line[first] == '#') continue;
        std::istringstream stream(line.substr(first)); std::string cmd; stream >> cmd;
        if (cmd == "vespera_script_metadata") { stream >> version; if (version < 1 || version > 4) break; }
        else if (cmd == "assembly") stream >> std::quoted(catalog.assembly);
        else if (cmd == "script") { ManagedScriptMetadata meta; stream >> std::quoted(meta.class_name); catalog.scripts.push_back(std::move(meta)); current = &catalog.scripts.back(); }
        else if (cmd == "field" && current) {
            ManagedFieldMetadata f;
            stream >> std::quoted(f.name) >> std::quoted(f.type) >> std::quoted(f.display_name) >> std::quoted(f.clr_type);
            if (version >= 2) {
                int has_range = 0;
                stream >> std::quoted(f.tooltip) >> has_range >> f.range_min >> f.range_max;
                f.has_range = has_range != 0;
            }
            current->fields.push_back(std::move(f));
        }
        else if (cmd == "enum_value" && current && !current->fields.empty()) {
            std::string value; stream >> std::quoted(value);
            current->fields.back().enum_values.push_back(std::move(value));
        }
        else if (cmd == "alias" && current && !current->fields.empty()) {
            std::string value; stream >> std::quoted(value);
            if (!value.empty()) current->fields.back().aliases.push_back(std::move(value));
        }
        else if (cmd == "endscript") current = nullptr;
        else if (cmd == "end_metadata") { catalog.loaded = version >= 1 && version <= 4; break; }
    }
    state.managed_metadata = std::move(catalog);
    if (report) push_console(state, state.managed_metadata.loaded ? ConsoleEntry::Level::Info : ConsoleEntry::Level::Warning,
        state.managed_metadata.loaded ? std::format("Loaded C# metadata: {} component type(s).", state.managed_metadata.scripts.size()) : "C# metadata file is invalid.");
    return state.managed_metadata.loaded;
}
std::string default_managed_value(const ManagedFieldMetadata& field) {
    if (field.type == "bool") return "false";
    if (field.type == "int") {
        if (field.has_range && (0.0f < field.range_min || 0.0f > field.range_max))
            return std::to_string(static_cast<int>(field.range_min));
        return "0";
    }
    if (field.type == "float") {
        if (field.has_range && (0.0f < field.range_min || 0.0f > field.range_max))
            return std::format("{}", field.range_min);
        return "0";
    }
    if (field.type == "enum") return field.enum_values.empty() ? std::string{} : field.enum_values.front();
    if (field.type == "vec2") return "0 0";
    if (field.type == "vec3") return "0 0 0";
    if (field.type == "color") return "1 1 1 1";
    return {};
}
std::optional<std::filesystem::path> managed_build_helper_path() {
#if defined(_WIN32)
    char* raw = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&raw, &size, "VESPERA_MANAGED_BUILD_HELPER") != 0 || !raw || !*raw) {
        if (raw) std::free(raw);
        return std::nullopt;
    }
    std::filesystem::path path(raw);
    std::free(raw);
    return path;
#else
    return std::nullopt;
#endif
}
void load_managed_build_diagnostics(EditorState& state, const std::filesystem::path& path, int process_result) {
    std::ifstream input(path);
    if (!input) {
        push_console(state, ConsoleEntry::Level::Error,
            std::format("C# build process returned {}, but no diagnostics file was produced.", process_result));
        return;
    }

    std::string status;
    std::string tfm;
    std::vector<std::string> output_tail;
    bool saw_diagnostic = false;
    std::string line;
    while (std::getline(input, line)) {
        const auto first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) continue;
        std::istringstream stream(line.substr(first));
        std::string command;
        stream >> command;
        if (command == "status") {
            stream >> std::quoted(status);
        } else if (command == "tfm") {
            stream >> std::quoted(tfm);
        } else if (command == "diagnostic") {
            std::string severity, code, file, message;
            int source_line = 0, column = 0;
            stream >> std::quoted(severity) >> std::quoted(code) >> std::quoted(file)
                >> source_line >> column >> std::quoted(message);
            const auto level = severity == "error" ? ConsoleEntry::Level::Error : ConsoleEntry::Level::Warning;
            const auto location = file.empty() ? std::string{} : std::format("{}({},{})", file, source_line, column);
            push_console(state, level, std::format("{}{}{}: {}",
                code, location.empty() ? "" : " ", location, message));
            saw_diagnostic = true;
        } else if (command == "output") {
            std::string text;
            stream >> std::quoted(text);
            if (!text.empty()) output_tail.push_back(std::move(text));
        }
    }

    if (status == "success" && process_result == 0) {
        push_console(state, ConsoleEntry::Level::Info,
            tfm.empty() ? "C# build succeeded. Last-good managed output updated; a running reference game will auto-reload the committed game assembly."
                        : std::format("C# build succeeded ({}). Last-good managed output updated; a running reference game will auto-reload the committed game assembly.", tfm));
        load_managed_metadata(state, true);
    } else {
        push_console(state, ConsoleEntry::Level::Error,
            "C# build failed. The previous good managed assembly and metadata were preserved.");
        if (!saw_diagnostic) {
            for (const auto& text : output_tail) push_console(state, ConsoleEntry::Level::Error, text);
        }
    }
}
void build_managed_scripts(EditorState& state) {
    if (!state.project_loaded) {
        push_console(state, ConsoleEntry::Level::Warning, "Open a Vespera project before building C# scripts.");
        return;
    }
    if (state.project.managed_assembly.empty() || state.project.managed_project.empty()) {
        push_console(state, ConsoleEntry::Level::Warning, "This project does not declare a C# script assembly.");
        return;
    }

    const auto builder = editor_find_builder_executable("Debug");
    const auto dotnet = editor_find_dotnet_executable();
    const auto sdk_project = editor_find_sdk_project();
    const auto script_tool_project = editor_find_script_tool_project();
    if (builder.empty()) {
        push_console(state, ConsoleEntry::Level::Error,
            "Build C# cannot locate VesperaBuilder. Rebuild or reinstall Vespera Engine.");
        return;
    }
    if (dotnet.empty()) {
        push_console(state, ConsoleEntry::Level::Error,
            "Build C# cannot locate a .NET 8+ SDK. Official Vespera distributions include one; source builds may use a system SDK.");
        return;
    }
    if (sdk_project.empty() || script_tool_project.empty()) {
        push_console(state, ConsoleEntry::Level::Error,
            "Build C# cannot locate the Vespera C# SDK/tooling files. Rebuild or reinstall Vespera Engine.");
        return;
    }

    const auto managed_dir = (state.project.root_directory / ".vespera" / "managed").lexically_normal();
    const auto log_dir = state.project.root_directory / ".vespera" / "logs";
    const auto log_path = log_dir / "csharp-build.log";

    vespera::platform::ProcessOptions process;
    process.executable = builder;
    process.working_directory = editor_installation_root();
    process.arguments = {
        "--project", state.project_path.string(),
        "--managed-only",
        "--managed-output", managed_dir.string(),
        "--configuration", "Debug",
        "--dotnet", dotnet.string(),
        "--sdk-project", sdk_project.string(),
        "--script-tool-project", script_tool_project.string(),
    };

    push_console(state, ConsoleEntry::Level::Info, "Building C# scripts...");
    std::string process_error;
    process.timeout_seconds = 900;
    const int result = vespera::platform::run_process_to_file(process, log_path, &process_error);
    std::ifstream input(log_path);
    std::vector<std::string> output;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) output.push_back(line);
    }

    if (result == 0) {
        push_console(state, ConsoleEntry::Level::Info,
            "C# build succeeded. Play Mode will use the updated game assembly and metadata.");
        load_managed_metadata(state, true);
        for (const auto& text : output) {
            if (text.starts_with("WARNING:")) push_console(state, ConsoleEntry::Level::Warning, text.substr(9));
        }
        return;
    }

    push_console(state, ConsoleEntry::Level::Error,
        "C# build failed. The previous good managed assembly and metadata were preserved.");
    if (!process_error.empty()) push_console(state, ConsoleEntry::Level::Error, process_error);
    const std::size_t begin = output.size() > 16 ? output.size() - 16 : 0;
    for (std::size_t i = begin; i < output.size(); ++i) {
        push_console(state, ConsoleEntry::Level::Error, output[i]);
    }
}

} // namespace vespera::editor
