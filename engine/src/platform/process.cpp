#include <vespera/platform/process.hpp>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <system_error>
#include <thread>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace vespera::platform {
namespace {

void set_error(std::string* error, std::string message) {
    if (error) *error = std::move(message);
}

#if defined(_WIN32)
std::wstring utf8_to_wide(const std::string& value) {
    if (value.empty()) return {};
    const int required = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (required <= 0) {
        return std::wstring(value.begin(), value.end());
    }
    std::wstring wide(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
        wide.data(), required);
    return wide;
}

std::wstring quote_windows_argument(std::wstring_view argument) {
    if (argument.empty()) return L"\"\"";
    if (argument.find_first_of(L" \t\n\v\"") == std::wstring_view::npos) {
        return std::wstring(argument);
    }
    std::wstring result;
    result.push_back(L'\"');
    std::size_t backslashes = 0;
    for (wchar_t ch : argument) {
        if (ch == L'\\') {
            ++backslashes;
            continue;
        }
        if (ch == L'\"') {
            result.append(backslashes * 2 + 1, L'\\');
            result.push_back(L'\"');
            backslashes = 0;
            continue;
        }
        result.append(backslashes, L'\\');
        backslashes = 0;
        result.push_back(ch);
    }
    result.append(backslashes * 2, L'\\');
    result.push_back(L'\"');
    return result;
}

std::wstring build_windows_command_line(const ProcessOptions& options) {
    std::wstring command = quote_windows_argument(options.executable.wstring());
    for (const auto& argument : options.arguments) {
        command.push_back(L' ');
        command += quote_windows_argument(utf8_to_wide(argument));
    }
    return command;
}

std::string windows_error_message(DWORD code) {
    wchar_t* raw = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, reinterpret_cast<wchar_t*>(&raw), 0, nullptr);
    if (!raw || length == 0) return "Windows error " + std::to_string(code);
    std::wstring message(raw, raw + length);
    LocalFree(raw);
    while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n' || message.back() == L' ')) {
        message.pop_back();
    }
    std::string utf8;
    if (!message.empty()) {
        const int bytes = WideCharToMultiByte(CP_UTF8, 0, message.data(), static_cast<int>(message.size()),
            nullptr, 0, nullptr, nullptr);
        if (bytes > 0) {
            utf8.resize(static_cast<std::size_t>(bytes));
            WideCharToMultiByte(CP_UTF8, 0, message.data(), static_cast<int>(message.size()),
                utf8.data(), bytes, nullptr, nullptr);
        }
    }
    return utf8.empty() ? ("Windows error " + std::to_string(code)) : utf8;
}
#else
std::vector<std::string> make_posix_argv(const ProcessOptions& options) {
    std::vector<std::string> values;
    values.reserve(options.arguments.size() + 1);
    values.push_back(options.executable.string());
    values.insert(values.end(), options.arguments.begin(), options.arguments.end());
    return values;
}

[[noreturn]] void exec_posix(const ProcessOptions& options) {
    if (!options.working_directory.empty()) {
        if (::chdir(options.working_directory.c_str()) != 0) {
            _exit(126);
        }
    }
    auto values = make_posix_argv(options);
    std::vector<char*> argv;
    argv.reserve(values.size() + 1);
    for (auto& value : values) argv.push_back(value.data());
    argv.push_back(nullptr);
    const std::string executable = options.executable.string();
    if (executable.find('/') == std::string::npos) {
        ::execvp(executable.c_str(), argv.data());
    } else {
        ::execv(executable.c_str(), argv.data());
    }
    _exit(127);
}
#endif

} // namespace

int run_process_to_file(
    const ProcessOptions& options,
    const std::filesystem::path& log_path,
    std::string* error
) {
    if (options.executable.empty()) {
        set_error(error, "process executable is empty");
        return -1000;
    }
    std::error_code ec;
    if (!log_path.parent_path().empty()) std::filesystem::create_directories(log_path.parent_path(), ec);
    if (ec) {
        set_error(error, "could not create process log directory: " + ec.message());
        return -1001;
    }

#if defined(_WIN32)
    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    HANDLE log = CreateFileW(log_path.wstring().c_str(), GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, &security, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log == INVALID_HANDLE_VALUE) {
        set_error(error, "could not open process log: " + windows_error_message(GetLastError()));
        return -1002;
    }
    HANDLE null_input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = null_input == INVALID_HANDLE_VALUE ? nullptr : null_input;
    startup.hStdOutput = log;
    startup.hStdError = log;
    PROCESS_INFORMATION process{};
    std::wstring command = build_windows_command_line(options);
    std::vector<wchar_t> mutable_command(command.begin(), command.end());
    mutable_command.push_back(L'\0');
    const std::wstring working = options.working_directory.wstring();
    // Start suspended so a Job Object can own the process tree before any child
    // compiler/build processes are spawned. KILL_ON_JOB_CLOSE makes timeout and
    // error cleanup deterministic for tools such as dotnet/MSBuild.
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    bool job_owns_process = false;
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(
                job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
            CloseHandle(job);
            job = nullptr;
        }
    }

    const BOOL created = CreateProcessW(
        options.executable.wstring().c_str(), mutable_command.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr,
        working.empty() ? nullptr : working.c_str(), &startup, &process);
    CloseHandle(log);
    if (null_input != INVALID_HANDLE_VALUE) CloseHandle(null_input);
    if (!created) {
        const DWORD code = GetLastError();
        if (job) CloseHandle(job);
        set_error(error, "could not start process: " + windows_error_message(code));
        return -1003;
    }

    if (job && AssignProcessToJobObject(job, process.hProcess)) {
        job_owns_process = true;
    } else if (job) {
        // Some hosts place children in a non-breakaway job. Continue without
        // tree ownership rather than making all synchronous process launches fail.
        CloseHandle(job);
        job = nullptr;
    }
    if (ResumeThread(process.hThread) == static_cast<DWORD>(-1)) {
        const DWORD code = GetLastError();
        if (job_owns_process) (void)TerminateJobObject(job, 125);
        else (void)TerminateProcess(process.hProcess, 125);
        (void)WaitForSingleObject(process.hProcess, 5000);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        if (job) CloseHandle(job);
        set_error(error, "could not resume process: " + windows_error_message(code));
        return -1003;
    }

    const DWORD timeout_ms = options.timeout_seconds == 0
        ? INFINITE
        : static_cast<DWORD>((std::min<unsigned long long>)(
            static_cast<unsigned long long>(options.timeout_seconds) * 1000ull,
            static_cast<unsigned long long>(INFINITE - 1)));
    const DWORD wait_result = WaitForSingleObject(process.hProcess, timeout_ms);
    if (wait_result == WAIT_TIMEOUT) {
        if (job_owns_process) (void)TerminateJobObject(job, 124);
        else (void)TerminateProcess(process.hProcess, 124);
        (void)WaitForSingleObject(process.hProcess, 5000);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        if (job) CloseHandle(job);
        set_error(error, "process timed out after " + std::to_string(options.timeout_seconds)
            + " second(s); process tree terminated");
        return -1006;
    }
    DWORD exit_code = 1;
    const bool got_exit = wait_result == WAIT_OBJECT_0 && GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (job) CloseHandle(job);
    if (!got_exit) {
        set_error(error, "could not wait for process completion");
        return -1004;
    }
    return static_cast<int>(exit_code);
#else
    const int log_fd = ::open(log_path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0666);
    if (log_fd < 0) {
        set_error(error, std::string("could not open process log: ") + std::strerror(errno));
        return -1002;
    }
    const pid_t pid = ::fork();
    if (pid < 0) {
        const int saved = errno;
        ::close(log_fd);
        set_error(error, std::string("fork failed: ") + std::strerror(saved));
        return -1003;
    }
    if (pid == 0) {
        // Give the synchronous command its own process group so timeout cleanup
        // can terminate grandchildren spawned by shells, dotnet, compilers, etc.
        (void)::setpgid(0, 0);
        const int null_fd = ::open("/dev/null", O_RDONLY);
        if (null_fd >= 0) {
            (void)::dup2(null_fd, STDIN_FILENO);
            ::close(null_fd);
        }
        (void)::dup2(log_fd, STDOUT_FILENO);
        (void)::dup2(log_fd, STDERR_FILENO);
        ::close(log_fd);
        exec_posix(options);
    }
    // Close the race where the parent reaches timeout handling before the child
    // has executed setpgid(). EACCES/ESRCH are harmless if exec/exit won first.
    (void)::setpgid(pid, pid);
    ::close(log_fd);
    int status = 0;
    if (options.timeout_seconds == 0) {
        if (::waitpid(pid, &status, 0) < 0) {
            set_error(error, std::string("waitpid failed: ") + std::strerror(errno));
            return -1004;
        }
    } else {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(options.timeout_seconds);
        for (;;) {
            const pid_t waited = ::waitpid(pid, &status, WNOHANG);
            if (waited == pid) break;
            if (waited < 0) {
                set_error(error, std::string("waitpid failed: ") + std::strerror(errno));
                return -1004;
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                // Negative PID targets the whole process group created above.
                (void)::kill(-pid, SIGTERM);
                const auto terminate_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
                for (;;) {
                    const pid_t terminated = ::waitpid(pid, &status, WNOHANG);
                    if (terminated == pid) break;
                    if (terminated < 0) break;
                    if (std::chrono::steady_clock::now() >= terminate_deadline) {
                        (void)::kill(-pid, SIGKILL);
                        (void)::waitpid(pid, &status, 0);
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(25));
                }
                set_error(error, "process timed out after " + std::to_string(options.timeout_seconds)
                    + " second(s); process tree terminated");
                return -1006;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return -1005;
#endif
}

bool launch_process(const ProcessOptions& options, std::string* error) {
    if (options.executable.empty()) {
        set_error(error, "process executable is empty");
        return false;
    }
#if defined(_WIN32)
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    std::wstring command = build_windows_command_line(options);
    std::vector<wchar_t> mutable_command(command.begin(), command.end());
    mutable_command.push_back(L'\0');
    const std::wstring working = options.working_directory.wstring();
    const BOOL created = CreateProcessW(
        options.executable.wstring().c_str(), mutable_command.data(), nullptr, nullptr, FALSE,
        0, nullptr, working.empty() ? nullptr : working.c_str(), &startup, &process);
    if (!created) {
        set_error(error, "could not launch process: " + windows_error_message(GetLastError()));
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
#else
    const pid_t first = ::fork();
    if (first < 0) {
        set_error(error, std::string("fork failed: ") + std::strerror(errno));
        return false;
    }
    if (first == 0) {
        const pid_t second = ::fork();
        if (second < 0) _exit(126);
        if (second > 0) _exit(0);
        (void)::setsid();
        const int null_fd = ::open("/dev/null", O_RDWR);
        if (null_fd >= 0) {
            (void)::dup2(null_fd, STDIN_FILENO);
            (void)::dup2(null_fd, STDOUT_FILENO);
            (void)::dup2(null_fd, STDERR_FILENO);
            if (null_fd > STDERR_FILENO) ::close(null_fd);
        }
        exec_posix(options);
    }
    int status = 0;
    if (::waitpid(first, &status, 0) < 0) {
        set_error(error, std::string("waitpid failed: ") + std::strerror(errno));
        return false;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        set_error(error, "could not detach child process");
        return false;
    }
    return true;
#endif
}

std::filesystem::path current_executable_path() {
#if defined(_WIN32)
    std::wstring buffer(512, L'\0');
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) return {};
        if (length < buffer.size() - 1) {
            buffer.resize(length);
            return std::filesystem::path(buffer).lexically_normal();
        }
        buffer.resize(buffer.size() * 2);
    }
#elif defined(__linux__)
    std::string buffer(512, '\0');
    for (;;) {
        const ssize_t length = ::readlink("/proc/self/exe", buffer.data(), buffer.size());
        if (length < 0) return {};
        if (static_cast<std::size_t>(length) < buffer.size()) {
            buffer.resize(static_cast<std::size_t>(length));
            return std::filesystem::path(buffer).lexically_normal();
        }
        buffer.resize(buffer.size() * 2);
    }
#else
    return {};
#endif
}

bool reveal_directory(const std::filesystem::path& directory, std::string* error) {
    std::error_code ec;
    if (!std::filesystem::is_directory(directory, ec) || ec) {
        set_error(error, "directory does not exist: " + directory.string());
        return false;
    }
#if defined(_WIN32)
    ProcessOptions options;
    options.executable = std::filesystem::path(std::getenv("WINDIR") ? std::getenv("WINDIR") : "C:\\Windows")
        / "explorer.exe";
    options.arguments = {directory.string()};
    return launch_process(options, error);
#else
    ProcessOptions options;
    options.executable = "/usr/bin/xdg-open";
    if (!std::filesystem::is_regular_file(options.executable)) options.executable = "xdg-open";
    options.arguments = {directory.string()};
    return launch_process(options, error);
#endif
}

} // namespace vespera::platform
