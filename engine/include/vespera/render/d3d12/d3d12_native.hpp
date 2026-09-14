#pragma once

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d12.h>
#include <dxgiformat.h>
#endif

namespace vespera {

class RenderBackend;
struct RenderViewport;

#if defined(_WIN32)
// Narrow, explicitly backend-specific bridge for editor/tool integrations that
// need to record Direct3D 12 commands after Vespera renders the scene. Core
// scene/game APIs never depend on this surface. A Vulkan equivalent can live
// beside it later without leaking D3D12 objects into renderer-independent code.
class D3D12NativeAccess {
public:
    virtual ~D3D12NativeAccess() = default;
    [[nodiscard]] virtual ID3D12Device* d3d12_device() const = 0;
    [[nodiscard]] virtual ID3D12CommandQueue* d3d12_command_queue() const = 0;
    [[nodiscard]] virtual ID3D12GraphicsCommandList* d3d12_command_list() const = 0;
    [[nodiscard]] virtual DXGI_FORMAT d3d12_backbuffer_format() const = 0;

    // Tooling helpers used by the editor's texture-backed scene preview.
    // The destination texture must match the swap-chain dimensions/format and
    // be created in PIXEL_SHADER_RESOURCE state. Vespera handles the copy
    // state transitions while the current frame is still open.
    [[nodiscard]] virtual bool d3d12_copy_backbuffer_to_texture(ID3D12Resource* destination) = 0;
    virtual void d3d12_wait_for_gpu() = 0;

    // Prepare the active Vespera frame for a backend-native UI/overlay pass.
    // D3D12 only permits one CBV/SRV/UAV shader-visible heap of a given type
    // to be bound at a time, so tools such as Dear ImGui must explicitly
    // replace the scene renderer's texture heap before issuing their draws.
    // This also rebinds the current back buffer without the scene depth target.
    [[nodiscard]] virtual bool d3d12_prepare_overlay(ID3D12DescriptorHeap* shader_visible_srv_heap) = 0;
};

[[nodiscard]] D3D12NativeAccess* d3d12_native_access(RenderBackend* backend);
[[nodiscard]] const D3D12NativeAccess* d3d12_native_access(const RenderBackend* backend);
#endif

} // namespace vespera
