#pragma once

#include <string>
#include <vector>

namespace vespera {

class Scene;

enum class SceneValidationSeverity {
    Warning,
    Error,
};

struct SceneValidationIssue {
    SceneValidationSeverity severity = SceneValidationSeverity::Error;
    std::string message;
};

[[nodiscard]] std::vector<SceneValidationIssue> validate_scene(const Scene& scene);
[[nodiscard]] bool scene_validation_has_errors(const std::vector<SceneValidationIssue>& issues);

} // namespace vespera
