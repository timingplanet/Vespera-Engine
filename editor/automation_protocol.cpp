#include "automation_protocol.hpp"

#include <charconv>
#include <cctype>
#include <format>

namespace vespera::editor {
namespace {

int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

std::vector<std::string_view> split_tabs(std::string_view line) {
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    while (start <= line.size()) {
        const std::size_t tab = line.find('\t', start);
        if (tab == std::string_view::npos) {
            parts.push_back(line.substr(start));
            break;
        }
        parts.push_back(line.substr(start, tab - start));
        start = tab + 1;
    }
    return parts;
}

} // namespace

std::string automation_percent_encode(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    constexpr char kHex[] = "0123456789ABCDEF";
    for (const unsigned char c : value) {
        const bool safe = std::isalnum(c) != 0 || c == '-' || c == '_' || c == '.' || c == '/' || c == ':';
        if (safe) {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(kHex[(c >> 4) & 0xF]);
            out.push_back(kHex[c & 0xF]);
        }
    }
    return out;
}

std::optional<std::string> automation_percent_decode(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        const char c = value[i];
        if (c != '%') {
            out.push_back(c);
            continue;
        }
        if (i + 2 >= value.size()) return std::nullopt;
        const int hi = hex_value(value[i + 1]);
        const int lo = hex_value(value[i + 2]);
        if (hi < 0 || lo < 0) return std::nullopt;
        out.push_back(static_cast<char>((hi << 4) | lo));
        i += 2;
    }
    return out;
}

std::optional<AutomationRequest> parse_automation_request(std::string_view line, std::string* error) {
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.remove_suffix(1);
    const auto parts = split_tabs(line);
    if (parts.size() < 2) {
        if (error) *error = "automation request requires id and command";
        return std::nullopt;
    }

    AutomationRequest request;
    const auto id_result = std::from_chars(parts[0].data(), parts[0].data() + parts[0].size(), request.id);
    if (id_result.ec != std::errc{} || id_result.ptr != parts[0].data() + parts[0].size()) {
        if (error) *error = "automation request id is not an unsigned integer";
        return std::nullopt;
    }
    const auto decoded_command = automation_percent_decode(parts[1]);
    if (!decoded_command || decoded_command->empty()) {
        if (error) *error = "automation command is empty or invalid";
        return std::nullopt;
    }
    request.command = *decoded_command;

    for (std::size_t i = 2; i < parts.size(); ++i) {
        const auto equals = parts[i].find('=');
        if (equals == std::string_view::npos || equals == 0) {
            if (error) *error = std::format("invalid automation argument token {}", i - 1);
            return std::nullopt;
        }
        const auto key = automation_percent_decode(parts[i].substr(0, equals));
        const auto value = automation_percent_decode(parts[i].substr(equals + 1));
        if (!key || key->empty() || !value) {
            if (error) *error = "invalid percent-encoding in automation argument";
            return std::nullopt;
        }
        request.args[*key] = *value;
    }
    return request;
}

std::string encode_automation_response(const AutomationResponse& response) {
    std::string line = std::to_string(response.id);
    line += response.ok ? "\tok" : "\terror";
    for (const auto& [key, value] : response.fields) {
        line.push_back('\t');
        line += automation_percent_encode(key);
        line.push_back('=');
        line += automation_percent_encode(value);
    }
    line.push_back('\n');
    return line;
}

} // namespace vespera::editor
