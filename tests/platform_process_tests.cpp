#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <vespera/platform/process.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <chrono>

namespace fs = std::filesystem;

namespace {

fs::path scratch_file(std::string_view name) {
    return fs::temp_directory_path() / ("vespera-process-test-" + std::string(name) + ".log");
}

std::string read_file(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::ostringstream out;
    out << input.rdbuf();
    return out.str();
}

vespera::platform::ProcessOptions shell_process(std::string command) {
    vespera::platform::ProcessOptions options;
#if defined(_WIN32)
    if (const char* comspec = std::getenv("COMSPEC"); comspec && *comspec) options.executable = comspec;
    else options.executable = "C:\\Windows\\System32\\cmd.exe";
    options.arguments = {"/D", "/S", "/C", std::move(command)};
#else
    options.executable = "/bin/sh";
    options.arguments = {"-c", std::move(command)};
#endif
    return options;
}

} // namespace

TEST_CASE("synchronous process output is captured") {
    const fs::path log = scratch_file("capture");
    std::error_code ec;
    fs::remove(log, ec);

#if defined(_WIN32)
    auto process = shell_process("echo vespera-process-ok");
#else
    auto process = shell_process("printf vespera-process-ok");
#endif
    std::string error;
    CHECK(vespera::platform::run_process_to_file(process, log, &error) == 0);
    CHECK(error.empty());
    CHECK(read_file(log).find("vespera-process-ok") != std::string::npos);
    fs::remove(log, ec);
}

TEST_CASE("synchronous process timeout terminates a stalled child") {
    const fs::path log = scratch_file("timeout");
    std::error_code ec;
    fs::remove(log, ec);

#if defined(_WIN32)
    auto process = shell_process("ping 127.0.0.1 -n 4 >NUL");
#else
    auto process = shell_process("sleep 3");
#endif
    process.timeout_seconds = 1;
    std::string error;
    CHECK(vespera::platform::run_process_to_file(process, log, &error) == -1006);
    CHECK(error.find("timed out") != std::string::npos);
    fs::remove(log, ec);
}


#if !defined(_WIN32)
TEST_CASE("synchronous process timeout terminates descendants") {
    const fs::path log = scratch_file("timeout-tree");
    const fs::path marker = fs::temp_directory_path() / "vespera-process-test-descendant-survived.txt";
    std::error_code ec;
    fs::remove(log, ec);
    fs::remove(marker, ec);

    const std::string command = "(sleep 2; printf survived > '" + marker.string() + "') & sleep 5";
    auto process = shell_process(command);
    process.timeout_seconds = 1;
    std::string error;
    CHECK(vespera::platform::run_process_to_file(process, log, &error) == -1006);
    CHECK(error.find("process tree terminated") != std::string::npos);

    // If only the shell were terminated, its background child would create the
    // marker about one second after the timeout returned.
    std::this_thread::sleep_for(std::chrono::milliseconds(1400));
    CHECK_FALSE(fs::exists(marker));
    fs::remove(marker, ec);
    fs::remove(log, ec);
}
#endif
