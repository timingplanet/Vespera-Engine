#pragma once

#include <filesystem>
#include <string_view>

namespace vespera::log {

// Optional process-local file sink used by shipping runtimes where a console
// may not exist. SDL logging remains active; this simply mirrors each message
// into a human-readable append-only session log.
[[nodiscard]] bool set_file_sink(const std::filesystem::path& path);
void clear_file_sink();
[[nodiscard]] std::filesystem::path file_sink_path();

void info(std::string_view message);
void warn(std::string_view message);
void error(std::string_view message);

} // namespace vespera::log
