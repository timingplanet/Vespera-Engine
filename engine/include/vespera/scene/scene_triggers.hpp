#pragma once

#include <vespera/math/types.hpp>
#include <vespera/scene/scene.hpp>

#include <vector>

namespace vespera {

enum class TriggerEventType {
    Enter,
    Exit,
};

struct TriggerEvent {
    TriggerEventType type = TriggerEventType::Enter;
    SceneObjectId trigger_entity = kInvalidSceneObjectId;
};

// Small stateful helper that converts overlap snapshots into deterministic
// enter/exit events. It owns ids rather than pointers, so scene vector moves do
// not invalidate the tracker. Scripting layers can wrap the same API later.
class TriggerTracker {
public:
    [[nodiscard]] std::vector<TriggerEvent> update_circle(
        const Scene& scene,
        Vec3 position,
        float radius,
        SceneObjectId ignore_entity = kInvalidSceneObjectId
    );

    void reset();

    [[nodiscard]] const std::vector<SceneObjectId>& active_trigger_ids() const {
        return active_trigger_ids_;
    }

private:
    std::vector<SceneObjectId> active_trigger_ids_;
};

} // namespace vespera
