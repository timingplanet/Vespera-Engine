#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace vespera::platform {

struct ProcessOptions {
    std::filesystem::path executable;
    std::vector<std::string> arguments;
    std::filesystem::path working_directory;
    // Optional hard timeout for synchronous child processes. Zero keeps the
    // historical unbounded wait. Timed-out children are terminated and the
    // caller receives a negative Vespera-owned error code.
    unsigned int timeout_seconds = 0;
};

// Runs a child process synchronously with stdout/stderr redirected to log_path.
// Returns the process exit code, or a negative Vespera-owned error code when the
// child could not be created/waited. error receives a human-readable reason.
int run_process_to_file(
    const ProcessOptions& options,
    const std::filesystem::path& log_path,
    std::string* error = nullptr
);

// Launches a detached child and returns once ownership has been handed to the OS.
bool launch_process(const ProcessOptions& options, std::string* error = nullptr);

// Absolute path to the currently running executable when the platform exposes it.
[[nodiscard]] std::filesystem::path current_executable_path();

// Opens a directory in the platform file manager (Explorer / xdg-open).
bool reveal_directory(const std::filesystem::path& directory, std::string* error = nullptr);

} // namespace vespera::platform
