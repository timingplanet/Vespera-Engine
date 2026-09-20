#include "editor_windows_d3d12.hpp"

#if defined(_WIN32)

#include "editor_frame_ui.hpp"
#include "editor_installation.hpp"
#include "editor_play_controls.hpp"
#include "editor_render_viewports.hpp"
#include "editor_state.hpp"
#include "editor_startup_splash.hpp"
#include "editor_window_identity.hpp"

#include <vespera/ui/ui_render.hpp>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_dx12.h>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <filesystem>
#include <format>
#include <thread>
#include <system_error>

namespace vespera::editor {

void editor_startup_trace(std::string_view message, bool reset) {
    const auto settings = editor_user_settings_directory();
    std::error_code ec;
    std::filesystem::create_directories(settings, ec);
    std::ofstream stream(
        settings / "vespera_editor_startup.log",
        reset ? std::ios::trunc : std::ios::app
    );
    if (stream) {
        stream << message << '\n';
        stream.flush();
    }
}

bool EditorD3D12DescriptorAllocator::reserve(
    D3D12_CPU_DESCRIPTOR_HANDLE& out_cpu,
    D3D12_GPU_DESCRIPTOR_HANDLE& out_gpu
) {
    out_cpu = {};
    out_gpu = {};
    if (!heap || descriptor_size == 0) return false;
    for (std::size_t i = 0; i < used.size(); ++i) {
        if (used[i]) continue;
        used[i] = true;
        out_cpu = heap->GetCPUDescriptorHandleForHeapStart();
        out_gpu = heap->GetGPUDescriptorHandleForHeapStart();
        out_cpu.ptr += static_cast<SIZE_T>(i) * descriptor_size;
        out_gpu.ptr += static_cast<UINT64>(i) * descriptor_size;
        return true;
    }
    return false;
}

void EditorD3D12DescriptorAllocator::allocate(
    ImGui_ImplDX12_InitInfo* info,
    D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu,
    D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu
) {
    auto* allocator = static_cast<EditorD3D12DescriptorAllocator*>(info->UserData);
    if (!allocator || !allocator->heap || allocator->descriptor_size == 0) {
        *out_cpu = {};
        *out_gpu = {};
        return;
    }

    if (allocator->reserve(*out_cpu, *out_gpu)) return;

    // Dear ImGui treats a zero handle as an allocation failure. The editor
    // reserves enough descriptors for its own UI plus future thumbnails;
    // reaching this path means the tooling heap should be grown.
    *out_cpu = {};
    *out_gpu = {};
}

void EditorD3D12DescriptorAllocator::free(
    ImGui_ImplDX12_InitInfo* info,
    D3D12_CPU_DESCRIPTOR_HANDLE cpu,
    D3D12_GPU_DESCRIPTOR_HANDLE
) {
    auto* allocator = static_cast<EditorD3D12DescriptorAllocator*>(info->UserData);
    if (!allocator || !allocator->heap || allocator->descriptor_size == 0 || cpu.ptr == 0) return;
    const SIZE_T base = allocator->heap->GetCPUDescriptorHandleForHeapStart().ptr;
    if (cpu.ptr < base) return;
    const SIZE_T offset = cpu.ptr - base;
    if ((offset % allocator->descriptor_size) != 0) return;
    const std::size_t index = static_cast<std::size_t>(offset / allocator->descriptor_size);
    if (index < allocator->used.size()) allocator->used[index] = false;
}

bool EditorScenePreviewTarget::reserve_descriptor(EditorD3D12DescriptorAllocator& allocator) {
    if (descriptor_reserved) return true;
    descriptor_reserved = allocator.reserve(cpu_descriptor, gpu_descriptor);
    return descriptor_reserved;
}

bool EditorScenePreviewTarget::ensure(
    vespera::D3D12NativeAccess& native,
    EditorD3D12DescriptorAllocator& allocator,
    UINT requested_width,
    UINT requested_height
) {
    if (requested_width == 0 || requested_height == 0) return false;
    if (!reserve_descriptor(allocator)) return false;
    if (texture && width == requested_width && height == requested_height) return true;

    native.d3d12_wait_for_gpu();
    texture.Reset();
    width = 0;
    height = 0;

    D3D12_HEAP_PROPERTIES heap_properties{};
    heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap_properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heap_properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heap_properties.CreationNodeMask = 1;
    heap_properties.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = requested_width;
    desc.Height = requested_height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = native.d3d12_backbuffer_format();
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_NONE;

    const HRESULT hr = native.d3d12_device()->CreateCommittedResource(
        &heap_properties,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        nullptr,
        IID_PPV_ARGS(&texture)
    );
    if (FAILED(hr)) return false;

    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Format = desc.Format;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Texture2D.MostDetailedMip = 0;
    srv.Texture2D.MipLevels = 1;
    srv.Texture2D.PlaneSlice = 0;
    srv.Texture2D.ResourceMinLODClamp = 0.0f;
    native.d3d12_device()->CreateShaderResourceView(texture.Get(), &srv, cpu_descriptor);

    width = requested_width;
    height = requested_height;
    return true;
}

ImTextureID EditorScenePreviewTarget::texture_id() const {
    return texture ? static_cast<ImTextureID>(gpu_descriptor.ptr) : ImTextureID_Invalid;
}

bool render_editor_windows_frame(EditorWindowsFrameContext& context, bool interactive) {
    if (!context.window || !context.render_backend || !context.d3d12
        || !context.imgui_descriptors || !context.scene_preview_target
        || !context.state || !context.running || !context.first_present_traced) {
        return false;
    }

    EditorState& state = *context.state;
    bool& running = *context.running;
    const auto frame_begin = std::chrono::steady_clock::now();
    if (context.frame_in_progress) {
        return running;
    }
    context.frame_in_progress = true;

    ImGui_ImplDX12_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    draw_editor_ui_frame(state, running, interactive);

    ImGui::Render();
    SDL_SetWindowTitle(context.window, window_title(state).c_str());

    const auto now = std::chrono::steady_clock::now();
    const double total_seconds = std::chrono::duration<double>(now - context.start_time).count();
    const vespera::RenderFrameConfig frame_config{{0.018f, 0.024f, 0.035f, 1.0f}};
    const auto render_begin = std::chrono::steady_clock::now();
    if (!context.render_backend->begin_frame(frame_config)) {
        context.frame_in_progress = false;
        return running;
    }

    // During a Windows live resize, the OS temporarily blocks the normal application
    // loop. Redraw the editor chrome from SDL's exposed-event watcher, but avoid
    // rebuilding the full-size 3D preview texture for every intermediate mouse pixel.
    // The normal frame immediately refreshes the preview after the resize finishes.
    if (!context.live_redraw_in_progress) {
        int preview_pixel_width = 0;
        int preview_pixel_height = 0;
        const bool have_window_pixels = SDL_GetWindowSizeInPixels(
            context.window, &preview_pixel_width, &preview_pixel_height);
        if (have_window_pixels && preview_pixel_width > 0 && preview_pixel_height > 0
            && context.scene_preview_target->ensure(
                *context.d3d12,
                *context.imgui_descriptors,
                static_cast<UINT>(preview_pixel_width),
                static_cast<UINT>(preview_pixel_height))) {
            state.scene_view_3d.preview_texture = context.scene_preview_target->texture_id();
            state.game_view.preview_texture = context.scene_preview_target->texture_id();
        } else {
            state.scene_view_3d.preview_texture = ImTextureID_Invalid;
            state.game_view.preview_texture = ImTextureID_Invalid;
        }

        const auto play_update_begin = std::chrono::steady_clock::now();
        update_play_clock(state, total_seconds);
        const auto play_update_end = std::chrono::steady_clock::now();
        state.play_update_ms = std::chrono::duration<double, std::milli>(play_update_end - play_update_begin).count();
        bool rendered_preview = false;
        if (const auto viewport = scene_view_3d_pixel_viewport(state, context.window)) {
            context.render_backend->render_scene(
                state.scene,
                total_seconds,
                &state.scene_view_3d.camera,
                &*viewport
            );
            rendered_preview = true;
            if (preview_pixel_width > 0 && preview_pixel_height > 0) {
                state.scene_view_3d.preview_uv0 = {
                    static_cast<float>(viewport->x) / static_cast<float>(preview_pixel_width),
                    static_cast<float>(viewport->y) / static_cast<float>(preview_pixel_height)
                };
                state.scene_view_3d.preview_uv1 = {
                    static_cast<float>(viewport->x + viewport->width) / static_cast<float>(preview_pixel_width),
                    static_cast<float>(viewport->y + viewport->height) / static_cast<float>(preview_pixel_height)
                };
            }
        }

        if (const auto viewport = game_view_pixel_viewport(state, context.window)) {
            const bool playing = editor_is_playing(state);
            const vespera::Scene& game_scene = playing ? state.play_scene : state.scene;
            const double game_time = playing ? state.play_time_seconds : total_seconds;
            context.render_backend->render_scene(
                game_scene,
                game_time,
                &game_scene.camera,
                &*viewport
            );
            if (playing && state.play_rml_ui_loaded && state.play_rml_ui) {
                state.play_rml_view_width = std::max(1, viewport->width);
                state.play_rml_view_height = std::max(1, viewport->height);
                state.play_rml_ui->resize(state.play_rml_view_width, state.play_rml_view_height);
                const auto ui_packet = state.play_rml_ui->build_packet();
                context.render_backend->render_ui(ui_packet, &*viewport);
            } else if (playing && state.play_ui_loaded) {
                vespera::UiPointerState ui_pointer{};
                const ImGuiIO& ui_io = ImGui::GetIO();
                const float view_w = std::max(1.0f, state.game_view.content_max.x - state.game_view.content_min.x);
                const float view_h = std::max(1.0f, state.game_view.content_max.y - state.game_view.content_min.y);
                if (state.game_view.hovered && !state.game_view.input_captured) {
                    ui_pointer.available = true;
                    ui_pointer.position = {
                        std::clamp((ui_io.MousePos.x - state.game_view.content_min.x) / view_w, 0.0f, 1.0f) * static_cast<float>(viewport->width),
                        std::clamp((ui_io.MousePos.y - state.game_view.content_min.y) / view_h, 0.0f, 1.0f) * static_cast<float>(viewport->height)
                    };
                    ui_pointer.primary_down = ui_io.MouseDown[ImGuiMouseButton_Left];
                    ui_pointer.primary_pressed = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
                    ui_pointer.primary_released = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
                }
                vespera::UiNavigationState ui_navigation{};
                if (state.game_view.focused && !state.game_view.input_captured) {
                    const bool tab = ImGui::IsKeyPressed(ImGuiKey_Tab, false);
                    ui_navigation.focus_next = tab && !ui_io.KeyShift;
                    ui_navigation.focus_previous = tab && ui_io.KeyShift;
                    ui_navigation.activate_pressed = ImGui::IsKeyPressed(ImGuiKey_Enter, false);
                }
                vespera::UiTextInputState ui_text_input{};
                if (state.game_view.focused && !state.game_view.input_captured) {
                    ui_text_input.text = state.game_view_text_input;
                    ui_text_input.backspace = state.game_view_backspace_pending;
                }
                auto* ui_runtime = state.play_runtime ? &state.play_runtime->ui_runtime_state() : nullptr;
                const auto ui_packet = state.play_ui_cache.build_packet(
                    state.play_ui_document, static_cast<float>(viewport->width), static_cast<float>(viewport->height),
                    ui_pointer, ui_runtime, ui_navigation, ui_text_input);
                state.game_view_text_input.clear();
                state.game_view_backspace_pending = false;
                context.render_backend->render_ui(ui_packet, &*viewport);
            }
            rendered_preview = true;
            if (preview_pixel_width > 0 && preview_pixel_height > 0) {
                state.game_view.preview_uv0 = {
                    static_cast<float>(viewport->x) / static_cast<float>(preview_pixel_width),
                    static_cast<float>(viewport->y) / static_cast<float>(preview_pixel_height)
                };
                state.game_view.preview_uv1 = {
                    static_cast<float>(viewport->x + viewport->width) / static_cast<float>(preview_pixel_width),
                    static_cast<float>(viewport->y + viewport->height) / static_cast<float>(preview_pixel_height)
                };
            }
        }

        if (rendered_preview && context.scene_preview_target->texture) {
            if (!context.d3d12->d3d12_copy_backbuffer_to_texture(context.scene_preview_target->texture.Get())) {
                editor_startup_trace("WARNING: failed to copy editor Scene/Game preview texture");
            }
        }
    }

    if (!context.d3d12->d3d12_prepare_overlay(context.imgui_descriptors->heap.Get())) {
        editor_startup_trace("ERROR: failed to prepare D3D12 overlay state");
        running = false;
    } else {
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), context.d3d12->d3d12_command_list());
        if (!context.render_backend->end_frame()) {
            editor_startup_trace("ERROR: D3D12 frame presentation failed");
            running = false;
        } else if (!*context.first_present_traced) {
            editor_startup_trace("First editor frame presented successfully");
            *context.first_present_traced = true;
        }
    }

    const auto frame_end = std::chrono::steady_clock::now();
    const auto to_ms = [](auto duration) { return std::chrono::duration<double, std::milli>(duration).count(); };
    state.performance.render = context.render_backend->frame_stats();
    state.performance.record_frame(
        to_ms(frame_end - frame_begin),
        state.play_update_ms,
        to_ms(frame_end - render_begin));
    context.frame_in_progress = false;
    return running;
}

bool SDLCALL editor_live_resize_event_watch(void* userdata, SDL_Event* event) {
    auto* context = static_cast<EditorWindowsFrameContext*>(userdata);
    if (!context || !event || !context->window || !context->render_backend) {
        return true;
    }

    // SDL guarantees WINDOW_EXPOSED is delivered on the main thread and explicitly
    // supports redrawing from an event watcher for Windows live-resize operations.
    if (event->type != SDL_EVENT_WINDOW_EXPOSED
        || event->window.data1 != 1
        || event->window.windowID != SDL_GetWindowID(context->window)
        || context->frame_in_progress
        || context->live_redraw_in_progress) {
        return true;
    }

    int pixel_width = 0;
    int pixel_height = 0;
    if (!SDL_GetWindowSizeInPixels(context->window, &pixel_width, &pixel_height)
        || pixel_width <= 0 || pixel_height <= 0) {
        return true;
    }

    context->live_redraw_in_progress = true;
    if (context->state) ++context->state->performance.live_resize_redraws;
    context->render_backend->resize(pixel_width, pixel_height);
    render_editor_windows_frame(*context, false);
    context->live_redraw_in_progress = false;
    return true;
}


bool EditorWindowsD3D12Host::initialize_renderer(SDL_Window* window) {
    window_ = window;
    editor_startup_trace("SDL window created");
    render_backend_ = vespera::create_default_render_backend();
    if (!render_backend_ || !render_backend_->initialize(window_)) {
        std::fprintf(stderr, "Vespera editor renderer initialization failed.\n");
        shutdown_renderer();
        return false;
    }
    editor_startup_trace("Vespera D3D12 renderer initialized");

    int initial_pixel_width = 0;
    int initial_pixel_height = 0;
    if (SDL_GetWindowSizeInPixels(window_, &initial_pixel_width, &initial_pixel_height)) {
        render_backend_->resize(initial_pixel_width, initial_pixel_height);
    }

    present_editor_startup_splash(*render_backend_);
    splash_presented_at_ = std::chrono::steady_clock::now();
    splash_presented_ = true;
    editor_startup_trace("Vespera startup splash presented");

    d3d12_ = vespera::d3d12_native_access(render_backend_.get());
    if (!d3d12_ || !d3d12_->d3d12_device() || !d3d12_->d3d12_command_queue() || !d3d12_->d3d12_command_list()) {
        std::fprintf(stderr, "Vespera editor requires Direct3D 12 native renderer access on Windows.\n");
        shutdown_renderer();
        return false;
    }

    constexpr UINT kImGuiDescriptorCount = 64;
    D3D12_DESCRIPTOR_HEAP_DESC imgui_heap_desc{};
    imgui_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    imgui_heap_desc.NumDescriptors = kImGuiDescriptorCount;
    imgui_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(d3d12_->d3d12_device()->CreateDescriptorHeap(
            &imgui_heap_desc,
            IID_PPV_ARGS(&imgui_descriptors_.heap)))) {
        std::fprintf(stderr, "Failed to create the editor ImGui D3D12 descriptor heap.\n");
        shutdown_renderer();
        return false;
    }
    imgui_descriptors_.descriptor_size = d3d12_->d3d12_device()->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    imgui_descriptors_.used.assign(kImGuiDescriptorCount, false);
    return true;
}

bool EditorWindowsD3D12Host::initialize_imgui(SDL_Window* window) {
    if (!render_backend_ || !d3d12_ || !imgui_descriptors_.heap) return false;
    if (!ImGui_ImplSDL3_InitForD3D(window)) {
        std::fprintf(stderr, "ImGui SDL3/D3D backend initialization failed.\n");
        return false;
    }
    imgui_sdl_initialized_ = true;

    ImGui_ImplDX12_InitInfo dx12_init{};
    dx12_init.Device = d3d12_->d3d12_device();
    dx12_init.CommandQueue = d3d12_->d3d12_command_queue();
    dx12_init.NumFramesInFlight = 2;
    dx12_init.RTVFormat = d3d12_->d3d12_backbuffer_format();
    dx12_init.DSVFormat = DXGI_FORMAT_UNKNOWN;
    dx12_init.SrvDescriptorHeap = imgui_descriptors_.heap.Get();
    dx12_init.SrvDescriptorAllocFn = &EditorD3D12DescriptorAllocator::allocate;
    dx12_init.SrvDescriptorFreeFn = &EditorD3D12DescriptorAllocator::free;
    dx12_init.UserData = &imgui_descriptors_;
    if (!ImGui_ImplDX12_Init(&dx12_init)) {
        std::fprintf(stderr, "ImGui Direct3D 12 backend initialization failed.\n");
        shutdown_imgui_backends();
        return false;
    }
    imgui_dx12_initialized_ = true;
    editor_startup_trace("Dear ImGui D3D12 backend initialized");

    if (!scene_preview_target_.reserve_descriptor(imgui_descriptors_)) {
        std::fprintf(stderr, "Failed to reserve a D3D12 descriptor for the editor 3D preview.\n");
        shutdown_imgui_backends();
        return false;
    }
    return true;
}

void EditorWindowsD3D12Host::wait_for_startup_splash(double minimum_seconds) {
    if (!splash_presented_ || minimum_seconds <= 0.0) return;
    const auto minimum = std::chrono::duration<double>(minimum_seconds);
    const auto visible_for = std::chrono::steady_clock::now() - splash_presented_at_;
    if (visible_for >= minimum) return;

    const auto deadline = std::chrono::steady_clock::now() + (minimum - visible_for);
    while (std::chrono::steady_clock::now() < deadline) {
        SDL_PumpEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

std::string EditorWindowsD3D12Host::renderer_name() const {
    if (!render_backend_) return {};
    return std::string(render_backend_->name());
}

void EditorWindowsD3D12Host::resize(int pixel_width, int pixel_height) {
    if (render_backend_) render_backend_->resize(pixel_width, pixel_height);
}

bool EditorWindowsD3D12Host::attach_frame(
    EditorState& state,
    bool& running,
    std::chrono::steady_clock::time_point start_time
) {
    if (!window_ || !render_backend_ || !d3d12_) return false;
    first_present_traced_ = false;
    frame_context_ = EditorWindowsFrameContext{
        window_,
        render_backend_.get(),
        d3d12_,
        &imgui_descriptors_,
        &scene_preview_target_,
        &state,
        start_time,
        &running,
        &first_present_traced_
    };
    live_resize_watch_installed_ = SDL_AddEventWatch(editor_live_resize_event_watch, &frame_context_);
    return live_resize_watch_installed_;
}

void EditorWindowsD3D12Host::detach_frame() {
    if (live_resize_watch_installed_) {
        SDL_RemoveEventWatch(editor_live_resize_event_watch, &frame_context_);
        live_resize_watch_installed_ = false;
    }
    frame_context_ = {};
}

bool EditorWindowsD3D12Host::render_frame(bool interactive) {
    return render_editor_windows_frame(frame_context_, interactive);
}

void EditorWindowsD3D12Host::shutdown_imgui_backends() {
    if (imgui_dx12_initialized_) {
        ImGui_ImplDX12_Shutdown();
        imgui_dx12_initialized_ = false;
    }
    if (imgui_sdl_initialized_) {
        ImGui_ImplSDL3_Shutdown();
        imgui_sdl_initialized_ = false;
    }
}

void EditorWindowsD3D12Host::shutdown_renderer() {
    detach_frame();
    scene_preview_target_.texture.Reset();
    imgui_descriptors_.heap.Reset();
    d3d12_ = nullptr;
    if (render_backend_) {
        render_backend_->shutdown();
        render_backend_.reset();
    }
    window_ = nullptr;
    splash_presented_ = false;
}

} // namespace vespera::editor

#endif
