#include <vespera/render/render_backend.hpp>

#include <algorithm>
#include <cctype>
#include <string>

namespace vespera {

std::unique_ptr<RenderBackend> create_null_render_backend();
#if defined(VESPERA_HAS_D3D12)
std::unique_ptr<RenderBackend> create_d3d12_render_backend();
#endif
#if defined(VESPERA_HAS_VULKAN)
std::unique_ptr<RenderBackend> create_vulkan_render_backend();
#endif

std::string_view render_backend_type_name(RenderBackendType type) {
    switch (type) {
        case RenderBackendType::Automatic: return "Automatic";
        case RenderBackendType::Direct3D12: return "Direct3D 12";
        case RenderBackendType::Vulkan: return "Vulkan";
        case RenderBackendType::Null: return "Null";
    }
    return "Unknown";
}

std::optional<RenderBackendType> parse_render_backend_type(std::string_view value) {
    std::string normalized(value);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (normalized == "auto" || normalized == "automatic" || normalized == "default") {
        return RenderBackendType::Automatic;
    }
    if (normalized == "d3d12" || normalized == "direct3d12" || normalized == "direct3d-12" || normalized == "dx12") {
        return RenderBackendType::Direct3D12;
    }
    if (normalized == "vulkan" || normalized == "vk") {
        return RenderBackendType::Vulkan;
    }
    if (normalized == "null" || normalized == "headless") {
        return RenderBackendType::Null;
    }
    return std::nullopt;
}

bool render_backend_compiled(RenderBackendType type) {
    switch (type) {
        case RenderBackendType::Automatic:
        case RenderBackendType::Null:
            return true;
        case RenderBackendType::Direct3D12:
#if defined(VESPERA_HAS_D3D12)
            return true;
#else
            return false;
#endif
        case RenderBackendType::Vulkan:
#if defined(VESPERA_HAS_VULKAN)
            return true;
#else
            return false;
#endif
    }
    return false;
}

RenderBackendType resolve_render_backend_type(RenderBackendType requested) {
    if (requested != RenderBackendType::Automatic) return requested;
#if defined(_WIN32) && defined(VESPERA_HAS_D3D12)
    return RenderBackendType::Direct3D12;
#elif defined(VESPERA_HAS_VULKAN)
    return RenderBackendType::Vulkan;
#else
    return RenderBackendType::Null;
#endif
}

std::unique_ptr<RenderBackend> create_render_backend(RenderBackendType type) {
    const RenderBackendType resolved = resolve_render_backend_type(type);
    switch (resolved) {
        case RenderBackendType::Automatic:
            break;
        case RenderBackendType::Direct3D12:
#if defined(VESPERA_HAS_D3D12)
            return create_d3d12_render_backend();
#else
            return nullptr;
#endif
        case RenderBackendType::Vulkan:
#if defined(VESPERA_HAS_VULKAN)
            return create_vulkan_render_backend();
#else
            return nullptr;
#endif
        case RenderBackendType::Null:
            return create_null_render_backend();
    }
    return nullptr;
}

std::unique_ptr<RenderBackend> create_default_render_backend() {
    return create_render_backend(RenderBackendType::Automatic);
}

} // namespace vespera
