#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vespera::editor {

struct AutomationRequest {
    std::uint64_t id = 0;
    std::string command;
    std::map<std::string, std::string> args;
};

struct AutomationResponse {
    std::uint64_t id = 0;
    bool ok = false;
    std::map<std::string, std::string> fields;
};

std::string automation_percent_encode(std::string_view value);
std::optional<std::string> automation_percent_decode(std::string_view value);
std::optional<AutomationRequest> parse_automation_request(std::string_view line, std::string* error = nullptr);
std::string encode_automation_response(const AutomationResponse& response);

} // namespace vespera::editor
