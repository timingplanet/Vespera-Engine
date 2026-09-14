#pragma once

#include "automation_protocol.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace vespera::editor {

struct AutomationIncomingRequest {
    std::uint64_t client_id = 0;
    AutomationRequest request;
};

class AutomationServer {
public:
    AutomationServer();
    ~AutomationServer();

    AutomationServer(const AutomationServer&) = delete;
    AutomationServer& operator=(const AutomationServer&) = delete;

    bool start(std::uint16_t port, std::string* error = nullptr);
    void stop();
    [[nodiscard]] bool running() const;
    [[nodiscard]] std::uint16_t port() const;

    std::vector<AutomationIncomingRequest> poll();
    bool respond(std::uint64_t client_id, const AutomationResponse& response);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace vespera::editor
