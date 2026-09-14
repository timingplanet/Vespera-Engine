#include <vespera/scene/scene_triggers.hpp>

#include <vespera/scene/scene_collision.hpp>

#include <algorithm>
#include <utility>

namespace vespera {

std::vector<TriggerEvent> TriggerTracker::update_circle(
    const Scene& scene,
    Vec3 position,
    float radius,
    SceneObjectId ignore_entity
) {
    std::vector<SceneObjectId> current = scene_circle_overlapping_triggers(
        scene,
        position,
        radius,
        ignore_entity
    );

    std::vector<TriggerEvent> events;
    for (const SceneObjectId id : current) {
        if (!std::binary_search(active_trigger_ids_.begin(), active_trigger_ids_.end(), id)) {
            events.push_back({TriggerEventType::Enter, id});
        }
    }
    for (const SceneObjectId id : active_trigger_ids_) {
        if (!std::binary_search(current.begin(), current.end(), id)) {
            events.push_back({TriggerEventType::Exit, id});
        }
    }

    active_trigger_ids_ = std::move(current);
    return events;
}

void TriggerTracker::reset() {
    active_trigger_ids_.clear();
}

} // namespace vespera
