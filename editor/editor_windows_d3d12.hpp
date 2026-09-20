#pragma once

#if defined(_WIN32)

#include <vespera/render/d3d12/d3d12_native.hpp>
#include <vespera/render/render_backend.hpp>

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_dx12.h>
#include <wrl/client.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace vespera::editor {

struct EditorState;

void editor_startup_trace(std::string_view message, bool reset = false);

struct EditorD3D12DescriptorAllocator {
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap;
    UINT descriptor_size = 0;
    std::vector<bool> used;

    bool reserve(D3D12_CPU_DESCRIPTOR_HANDLE& out_cpu, D3D12_GPU_DESCRIPTOR_HANDLE& out_gpu);

    static void allocate(
        ImGui_ImplDX12_InitInfo* info,
        D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu,
        D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu
    );

    static void free(
        ImGui_ImplDX12_InitInfo* info,
        D3D12_CPU_DESCRIPTOR_HANDLE cpu,
        D3D12_GPU_DESCRIPTOR_HANDLE gpu
    );
};

struct EditorScenePreviewTarget {
    Microsoft::WRL::ComPtr<ID3D12Resource> texture;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu_descriptor{};
    D3D12_GPU_DESCRIPTOR_HANDLE gpu_descriptor{};
    UINT width = 0;
    UINT height = 0;
    bool descriptor_reserved = false;

    bool reserve_descriptor(EditorD3D12DescriptorAllocator& allocator);
    bool ensure(
        vespera::D3D12NativeAccess& native,
        EditorD3D12DescriptorAllocator& allocator,
        UINT requested_width,
        UINT requested_height
    );
    [[nodiscard]] ImTextureID texture_id() const;
};

struct EditorWindowsFrameContext {
    SDL_Window* window = nullptr;
    vespera::RenderBackend* render_backend = nullptr;
    vespera::D3D12NativeAccess* d3d12 = nullptr;
    EditorD3D12DescriptorAllocator* imgui_descriptors = nullptr;
    EditorScenePreviewTarget* scene_preview_target = nullptr;
    EditorState* state = nullptr;
    std::chrono::steady_clock::time_point start_time{};
    bool* running = nullptr;
    bool* first_present_traced = nullptr;
    bool frame_in_progress = false;
    bool live_redraw_in_progress = false;
};

bool render_editor_windows_frame(EditorWindowsFrameContext& context, bool interactive);
bool SDLCALL editor_live_resize_event_watch(void* userdata, SDL_Event* event);

class EditorWindowsD3D12Host {
public:
    bool initialize_renderer(SDL_Window* window);
    bool initialize_imgui(SDL_Window* window);
    void wait_for_startup_splash(double minimum_seconds);

    [[nodiscard]] std::string renderer_name() const;
    void resize(int pixel_width, int pixel_height);

    bool attach_frame(EditorState& state, bool& running, std::chrono::steady_clock::time_point start_time);
    void detach_frame();
    bool render_frame(bool interactive);

    void shutdown_imgui_backends();
    void shutdown_renderer();

private:
    SDL_Window* window_ = nullptr;
    std::unique_ptr<vespera::RenderBackend> render_backend_;
    vespera::D3D12NativeAccess* d3d12_ = nullptr;
    EditorD3D12DescriptorAllocator imgui_descriptors_;
    EditorScenePreviewTarget scene_preview_target_;
    EditorWindowsFrameContext frame_context_{};
    std::chrono::steady_clock::time_point splash_presented_at_{};
    bool splash_presented_ = false;
    bool first_present_traced_ = false;
    bool live_resize_watch_installed_ = false;
    bool imgui_sdl_initialized_ = false;
    bool imgui_dx12_initialized_ = false;
};

} // namespace vespera::editor

#endif
