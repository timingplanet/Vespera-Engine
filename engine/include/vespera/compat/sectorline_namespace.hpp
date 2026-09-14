#pragma once

// Pre-1.0 compatibility alias for source that still uses the Sectorline codename.
// New code should include <vespera/...> and use vespera::. Serialized asset/component
// identifiers intentionally remain sectorline.* where format compatibility requires it.
namespace vespera {}
namespace sectorline = vespera;
