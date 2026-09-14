#include <vespera/core/log.hpp>

#include <SDL3/SDL_log.h>

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>

namespace vespera::log {
namespace {
std::mutex g_file_mutex;
std::filesystem::path g_file_path;

std::string owned(std::string_view message) {
    return std::string(message);
}

std::string timestamp_text() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t value = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &value);
#else
    localtime_r(&value, &local);
#endif
    std::ostringstream out;
    out << std::put_time(&local, "%Y-%m-%d %H:%M:%S");
    return out.str();
}

void append_file(std::string_view level, std::string_view message) {
    std::scoped_lock lock(g_file_mutex);
    if (g_file_path.empty()) return;
    std::ofstream out(g_file_path, std::ios::binary | std::ios::app);
    if (!out) return;
    out << '[' << timestamp_text() << "] [" << level << "] " << message << '\n';
}
} // namespace

bool set_file_sink(const std::filesystem::path& path) {
    if (path.empty()) return false;
    std::error_code ec;
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) return false;

    {
        std::ofstream probe(path, std::ios::binary | std::ios::app);
        if (!probe) return false;
        probe << '[' << timestamp_text() << "] [SESSION] Vespera runtime log opened\n";
        if (!probe) return false;
    }
    std::scoped_lock lock(g_file_mutex);
    g_file_path = std::filesystem::absolute(path, ec).lexically_normal();
    if (ec) g_file_path = path.lexically_normal();
    return true;
}

void clear_file_sink() {
    std::scoped_lock lock(g_file_mutex);
    g_file_path.clear();
}

std::filesystem::path file_sink_path() {
    std::scoped_lock lock(g_file_mutex);
    return g_file_path;
}

void info(std::string_view message) {
    const auto text = owned(message);
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "%s", text.c_str());
    append_file("INFO", text);
}

void warn(std::string_view message) {
    const auto text = owned(message);
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s", text.c_str());
    append_file("WARN", text);
}

void error(std::string_view message) {
    const auto text = owned(message);
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", text.c_str());
    append_file("ERROR", text);
}

} // namespace vespera::log
