#if defined(_WIN32)
#include <vespera/scene/scene_hierarchy.hpp>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include <vespera/render/render_backend.hpp>
#include <vespera/render/d3d12/d3d12_native.hpp>
#include <vespera/render/view_frustum.hpp>
#include <vespera/core/log.hpp>
#include <vespera/scene/scene.hpp>
#include <vespera/scene/sprite_animation.hpp>
#include <vespera/world/sector_world.hpp>
#include <vespera/world/sector_mesh.hpp>
#include <vespera/ui/ui_render.hpp>

#include <SDL3/SDL.h>

#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <DirectXMath.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <format>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vespera {
namespace {

using Microsoft::WRL::ComPtr;

constexpr UINT kFrameCount = 2;
constexpr DXGI_FORMAT kBackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
constexpr DXGI_FORMAT kDepthFormat = DXGI_FORMAT_D32_FLOAT;

struct DrawConstants {
    DirectX::XMFLOAT4X4 view_projection{};
    DirectX::XMFLOAT4X4 model{};
    float tint[4]{};
    // x = forced texture layer, y = 1 when forcing the layer (sprites).
    float draw_params[4]{};
    // rgb = authored emission color, a = emission strength.
    float emission[4]{};
    // x = 1 for Unlit, y reserved, z = alpha cutoff, w reserved.
    float material_params[4]{0.0f, 0.0f, 0.5f, 0.0f};
};

static_assert(sizeof(DrawConstants) == sizeof(float) * 48);

struct PrimitiveInstanceData {
    DirectX::XMFLOAT4X4 model{};
    float tint[4]{};
    float draw_params[4]{};
    float emission[4]{};
    float material_params[4]{0.0f, 0.0f, 0.5f, 0.0f};
};

static_assert(sizeof(PrimitiveInstanceData) == sizeof(float) * 32);

constexpr std::size_t kMaxActivePointLights = 32;
constexpr std::size_t kMaxLightingViewsPerFrame = 8;

struct GpuPointLight {
    float position_radius[4]{};
    float color_intensity[4]{};
};

struct LightingConstants {
    float ambient[4]{0.55f, 0.57f, 0.62f, 1.0f};
    std::uint32_t light_count = 0;
    std::uint32_t reserved0 = 0;
    std::uint32_t reserved1 = 0;
    std::uint32_t reserved2 = 0;
    std::array<GpuPointLight, kMaxActivePointLights> lights{};
};

static_assert(sizeof(LightingConstants) % 16 == 0);
constexpr UINT64 kLightingConstantStride = (sizeof(LightingConstants) + UINT64{255}) & ~UINT64{255};

constexpr std::string_view kShaderSource = R"HLSL(
cbuffer DrawData : register(b0)
{
    row_major float4x4 g_view_projection;
    row_major float4x4 g_model;
    float4 g_tint;
    float4 g_draw_params;
    float4 g_emission;
    float4 g_material_params;
};

#define VESPERA_MAX_POINT_LIGHTS 32

struct PointLightData
{
    float4 position_radius;
    float4 color_intensity;
};

cbuffer LightingData : register(b1)
{
    float4 g_ambient;
    uint g_light_count;
    uint3 g_light_reserved;
    PointLightData g_lights[VESPERA_MAX_POINT_LIGHTS];
};

Texture2DArray g_world_textures : register(t0);
SamplerState g_world_sampler : register(s0);

struct VSInput
{
    float3 position : POSITION;
    float3 color : COLOR;
    float2 uv : TEXCOORD0;
    float texture_layer : TEXCOORD1;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
    float2 uv : TEXCOORD0;
    nointerpolation uint texture_layer : TEXCOORD1;
    float3 world_position : TEXCOORD2;
    nointerpolation float4 emission : TEXCOORD3;
    nointerpolation float4 material_params : TEXCOORD4;
};

VSOutput vs_main(VSInput input)
{
    VSOutput output;
    float4 world_position = mul(float4(input.position, 1.0), g_model);
    output.position = mul(world_position, g_view_projection);
    output.world_position = world_position.xyz;
    output.color = float4(input.color, 1.0) * g_tint;
    output.uv = input.uv;
    output.texture_layer = g_draw_params.y > 0.5
        ? (uint)(g_draw_params.x + 0.5)
        : (uint)(input.texture_layer + 0.5);
    output.emission = g_emission;
    output.material_params = g_material_params;
    return output;
}

struct InstanceInput
{
    float4 model0 : INSTANCE_MODEL0;
    float4 model1 : INSTANCE_MODEL1;
    float4 model2 : INSTANCE_MODEL2;
    float4 model3 : INSTANCE_MODEL3;
    float4 tint : INSTANCE_TINT0;
    float4 draw_params : INSTANCE_DRAW0;
    float4 emission : INSTANCE_EMISSION0;
    float4 material_params : INSTANCE_MATERIAL0;
};

VSOutput vs_instanced(VSInput input, InstanceInput instance)
{
    VSOutput output;
    const float4 local_position = float4(input.position, 1.0);
    const float4 world_position =
        local_position.x * instance.model0 +
        local_position.y * instance.model1 +
        local_position.z * instance.model2 +
        local_position.w * instance.model3;
    output.position = mul(world_position, g_view_projection);
    output.world_position = world_position.xyz;
    output.color = float4(input.color, 1.0) * instance.tint;
    output.uv = input.uv;
    output.texture_layer = instance.draw_params.y > 0.5
        ? (uint)(instance.draw_params.x + 0.5)
        : (uint)(input.texture_layer + 0.5);
    output.emission = instance.emission;
    output.material_params = instance.material_params;
    return output;
}

float3 evaluate_point_light(float3 world_position, float4 position_radius, float4 color_intensity)
{
    float radius = max(position_radius.w, 0.0001);
    float distance_to_light = distance(world_position, position_radius.xyz);
    float attenuation = saturate(1.0 - distance_to_light / radius);
    attenuation *= attenuation;
    return color_intensity.rgb * color_intensity.a * attenuation;
}

float4 ps_main(VSOutput input) : SV_TARGET
{
    float4 texel = g_world_textures.Sample(
        g_world_sampler,
        float3(input.uv, (float)input.texture_layer)
    );
    float alpha = texel.a * input.color.a;
    clip(alpha - saturate(input.material_params.z));

    float3 lighting = 1.0.xxx;
    if (input.material_params.x < 0.5)
    {
        lighting = g_ambient.rgb * g_ambient.a;
        const uint count = min(g_light_count, (uint)VESPERA_MAX_POINT_LIGHTS);
        [loop]
        for (uint light_index = 0; light_index < count; ++light_index)
        {
            PointLightData light = g_lights[light_index];
            if (light.position_radius.w > 0.0 && light.color_intensity.a > 0.0)
                lighting += evaluate_point_light(input.world_position, light.position_radius, light.color_intensity);
        }
    }

    float3 base_color = texel.rgb * input.color.rgb;
    float3 emission = input.emission.rgb * max(input.emission.a, 0.0);
    return float4(saturate(base_color * lighting + emission), alpha);
}
 )HLSL";

constexpr std::string_view kUiShaderSource = R"HLSL(
cbuffer UiData : register(b0)
{
    float2 g_viewport_size;
    float2 g_reserved;
};

Texture2D g_ui_atlas : register(t0);
SamplerState g_ui_sampler : register(s0);

struct UiVsInput
{
    float2 position : POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR;
};

struct UiVsOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR;
};

UiVsOutput ui_vs(UiVsInput input)
{
    UiVsOutput output;
    const float2 safe_viewport = max(g_viewport_size, float2(1.0, 1.0));
    const float2 normalized = input.position / safe_viewport;
    output.position = float4(normalized.x * 2.0 - 1.0, 1.0 - normalized.y * 2.0, 0.0, 1.0);
    output.uv = input.uv;
    output.color = input.color;
    return output;
}

float4 ui_ps(UiVsOutput input) : SV_TARGET
{
    return g_ui_atlas.Sample(g_ui_sampler, input.uv) * input.color;
}
)HLSL";

void fill_lighting_constants(const Scene& scene, const Camera& camera, const ViewFrustum& frustum, LightingConstants& constants, RenderFrameStats& stats) {
    struct Candidate {
        const Entity* entity = nullptr;
        TransformComponent world_transform{};
        float radius = 0.0f;
        float distance_squared = 0.0f;
    };

    std::vector<Candidate> candidates;
    // The scene can contain tens of thousands of non-light entities. Avoid
    // reserving candidate storage proportional to total scene size when the
    // renderer can upload at most kMaxActivePointLights lights for a view.
    candidates.reserve((std::min)(scene.entities.size(), std::size_t{128}));
    for (const Entity& entity : scene.entities) {
        if (!entity.enabled || !entity.point_light) continue;
        ++stats.point_lights_considered;
        const auto world_transform = entity_world_transform(scene, entity);
        const PointLightComponent& light = *entity.point_light;
        const float radius = light.radius * (std::max)({
            std::abs(world_transform.scale.x),
            std::abs(world_transform.scale.y),
            std::abs(world_transform.scale.z)
        });

        // A point light whose entire influence volume misses the current camera
        // frustum cannot affect any visible surface. Dropping it here reduces the
        // per-pixel light loop without changing authored lighting results.
        if (!frustum.intersects_sphere(world_transform.position, radius)) continue;
        ++stats.point_lights_frustum_visible;

        const float dx = world_transform.position.x - camera.position.x;
        const float dy = world_transform.position.y - camera.position.y;
        const float dz = world_transform.position.z - camera.position.z;
        candidates.push_back({&entity, world_transform, radius, dx * dx + dy * dy + dz * dz});
    }
    const auto nearer = [](const Candidate& a, const Candidate& b) {
        return a.distance_squared < b.distance_squared;
    };
    if (candidates.size() > kMaxActivePointLights) {
        std::partial_sort(
            candidates.begin(), candidates.begin() + static_cast<std::ptrdiff_t>(kMaxActivePointLights),
            candidates.end(), nearer);
    } else {
        std::sort(candidates.begin(), candidates.end(), nearer);
    }

    const std::size_t count = (std::min)(candidates.size(), kMaxActivePointLights);
    constants.light_count = static_cast<std::uint32_t>(count);
    stats.point_lights_uploaded += count;
    for (std::size_t index = 0; index < count; ++index) {
        const Candidate& candidate = candidates[index];
        const PointLightComponent& light = *candidate.entity->point_light;
        GpuPointLight& gpu = constants.lights[index];
        gpu.position_radius[0] = candidate.world_transform.position.x;
        gpu.position_radius[1] = candidate.world_transform.position.y;
        gpu.position_radius[2] = candidate.world_transform.position.z;
        gpu.position_radius[3] = candidate.radius;
        gpu.color_intensity[0] = light.color[0];
        gpu.color_intensity[1] = light.color[1];
        gpu.color_intensity[2] = light.color[2];
        gpu.color_intensity[3] = light.intensity;
    }
}

void check_hr(HRESULT hr, std::string_view operation) {
    if (FAILED(hr)) {
        throw std::runtime_error(std::format("{} failed (HRESULT 0x{:08X})", operation, static_cast<unsigned long>(hr)));
    }
}

std::string wide_to_utf8(const wchar_t* value) {
    if (!value || *value == L'\0') {
        return {};
    }

    const int required = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    if (required <= 1) {
        return {};
    }

    std::string output(static_cast<std::size_t>(required), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, -1, output.data(), required, nullptr, nullptr);
    output.resize(static_cast<std::size_t>(required - 1));
    return output;
}

const char* yes_no(bool value) {
    return value ? "yes" : "no";
}

std::pair<int, int> feature_level_parts(D3D_FEATURE_LEVEL level) {
    switch (level) {
        case D3D_FEATURE_LEVEL_12_1: return {12, 1};
        case D3D_FEATURE_LEVEL_12_0: return {12, 0};
        case D3D_FEATURE_LEVEL_11_1: return {11, 1};
        case D3D_FEATURE_LEVEL_11_0: return {11, 0};
        default: return {0, 0};
    }
}

class D3D12Renderer final : public RenderBackend, public D3D12NativeAccess {
public:
    ~D3D12Renderer() override {
        shutdown();
    }

    bool initialize(SDL_Window* window) override {
        window_ = window;

        try {
            enable_debug_validation_if_available();

            const SDL_PropertiesID properties = SDL_GetWindowProperties(window_);
            hwnd_ = static_cast<HWND>(SDL_GetPointerProperty(
                properties,
                SDL_PROP_WINDOW_WIN32_HWND_POINTER,
                nullptr
            ));

            if (!hwnd_) {
                throw std::runtime_error("SDL did not expose a Win32 HWND for the window.");
            }

            check_hr(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory_)), "CreateDXGIFactory2");

            create_device();
            query_capabilities();
            create_command_objects();
            create_swap_chain();
            create_descriptor_heaps();
            create_lighting_buffers();
            create_render_targets();
            create_depth_target();
            create_pipeline();
            create_ui_pipeline();
            create_sprite_geometry();
            create_primitive_geometry();
            create_sync_objects();
            log_capabilities();

            return true;
        } catch (const std::exception& ex) {
            log::error(std::format("D3D12 initialization failed: {}", ex.what()));
            shutdown();
            return false;
        }
    }

    void resize(int pixel_width, int pixel_height) override {
        if (!device_ || !swap_chain_ || pixel_width <= 0 || pixel_height <= 0) {
            return;
        }

        const UINT new_width = static_cast<UINT>(pixel_width);
        const UINT new_height = static_cast<UINT>(pixel_height);
        if (new_width == width_ && new_height == height_) {
            return;
        }

        try {
            wait_for_gpu();

            depth_target_.Reset();
            for (auto& target : render_targets_) {
                target.Reset();
            }

            const DXGI_SWAP_CHAIN_DESC desc = get_swap_chain_desc();
            check_hr(
                swap_chain_->ResizeBuffers(kFrameCount, new_width, new_height, desc.BufferDesc.Format, desc.Flags),
                "IDXGISwapChain::ResizeBuffers"
            );

            width_ = new_width;
            height_ = new_height;
            frame_index_ = swap_chain_->GetCurrentBackBufferIndex();
            create_render_targets();
            create_depth_target();
        } catch (const std::exception& ex) {
            log::error(std::format("D3D12 resize failed: {}", ex.what()));
        }
    }

    void set_vsync_enabled(bool enabled) override { vsync_enabled_ = enabled; }
    [[nodiscard]] bool vsync_enabled() const override { return vsync_enabled_; }
    [[nodiscard]] const RenderFrameStats& frame_stats() const override { return frame_stats_; }

    bool begin_frame(const RenderFrameConfig& config = {}) override {
        if (!device_ || width_ == 0 || height_ == 0 || frame_open_) {
            return false;
        }

        try {
            check_hr(command_allocators_[frame_index_]->Reset(), "ID3D12CommandAllocator::Reset");
            check_hr(command_list_->Reset(command_allocators_[frame_index_].Get(), pipeline_state_.Get()), "ID3D12GraphicsCommandList::Reset");
            lighting_view_cursor_ = 0;
            ui_draws_this_frame_ = 0;
            ui_vertex_cursor_ = 0;
            primitive_instance_cursor_ = 0;
            frame_stats_ = {};

            D3D12_RESOURCE_BARRIER to_render{};
            to_render.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            to_render.Transition.pResource = render_targets_[frame_index_].Get();
            to_render.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            to_render.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
            to_render.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
            command_list_->ResourceBarrier(1, &to_render);

            const D3D12_VIEWPORT viewport{
                0.0f,
                0.0f,
                static_cast<float>(width_),
                static_cast<float>(height_),
                0.0f,
                1.0f
            };
            const D3D12_RECT scissor{
                0,
                0,
                static_cast<LONG>(width_),
                static_cast<LONG>(height_)
            };
            command_list_->RSSetViewports(1, &viewport);
            command_list_->RSSetScissorRects(1, &scissor);

            const auto rtv = current_rtv();
            const auto dsv = dsv_heap_->GetCPUDescriptorHandleForHeapStart();
            command_list_->ClearRenderTargetView(rtv, config.clear_color.data(), 0, nullptr);
            command_list_->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
            command_list_->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
            frame_open_ = true;
            return true;
        } catch (const std::exception& ex) {
            log::error(std::format("D3D12 begin frame failed: {}", ex.what()));
            frame_open_ = false;
            return false;
        }
    }

    void render_scene(
        const Scene& scene,
        double total_seconds,
        const Camera* camera_override = nullptr,
        const RenderViewport* requested_viewport = nullptr
    ) override {
        if (!frame_open_ || !device_ || width_ == 0 || height_ == 0) {
            return;
        }

        try {
            ensure_world_geometry(scene.world);

            int view_x = 0;
            int view_y = 0;
            int view_width = static_cast<int>(width_);
            int view_height = static_cast<int>(height_);
            if (requested_viewport && requested_viewport->width > 0 && requested_viewport->height > 0) {
                view_x = std::clamp(requested_viewport->x, 0, static_cast<int>(width_));
                view_y = std::clamp(requested_viewport->y, 0, static_cast<int>(height_));
                view_width = std::clamp(requested_viewport->width, 0, static_cast<int>(width_) - view_x);
                view_height = std::clamp(requested_viewport->height, 0, static_cast<int>(height_) - view_y);
                if (view_width <= 0 || view_height <= 0) {
                    return;
                }
            }

            const D3D12_VIEWPORT viewport{
                static_cast<float>(view_x),
                static_cast<float>(view_y),
                static_cast<float>(view_width),
                static_cast<float>(view_height),
                0.0f,
                1.0f
            };
            const D3D12_RECT scissor{
                static_cast<LONG>(view_x),
                static_cast<LONG>(view_y),
                static_cast<LONG>(view_x + view_width),
                static_cast<LONG>(view_y + view_height)
            };
            command_list_->RSSetViewports(1, &viewport);
            command_list_->RSSetScissorRects(1, &scissor);

            const auto rtv = current_rtv();
            const auto dsv = dsv_heap_->GetCPUDescriptorHandleForHeapStart();
            // Clear only this view's depth. This is important when tools render
            // more than one scene region into the same frame.
            command_list_->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 1, &scissor);
            command_list_->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

            command_list_->SetPipelineState(pipeline_state_.Get());
            command_list_->SetGraphicsRootSignature(root_signature_.Get());
            if (srv_heap_ && texture_array_) {
                ID3D12DescriptorHeap* heaps[] = {srv_heap_.Get()};
                command_list_->SetDescriptorHeaps(1, heaps);
                command_list_->SetGraphicsRootDescriptorTable(1, srv_heap_->GetGPUDescriptorHandleForHeapStart());
            }
            command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

            using namespace DirectX;
            const float aspect = static_cast<float>(view_width) / static_cast<float>(std::max(view_height, 1));
            const Camera& camera = camera_override ? *camera_override : scene.camera;
            const ViewFrustum frustum = make_view_frustum(camera, aspect);
            ++frame_stats_.scene_passes;
            upload_lighting_constants(scene, camera, frustum);
            const float cos_pitch = std::cos(camera.pitch);
            const XMVECTOR eye = XMVectorSet(camera.position.x, camera.position.y, camera.position.z, 1.0f);
            const XMVECTOR forward = XMVectorSet(
                std::sin(camera.yaw) * cos_pitch,
                std::sin(camera.pitch),
                std::cos(camera.yaw) * cos_pitch,
                0.0f
            );
            const XMVECTOR target = XMVectorAdd(eye, forward);
            const XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
            const XMMATRIX view = XMMatrixLookAtLH(eye, target, up);
            const float fov = XMConvertToRadians(std::clamp(camera.vertical_fov_degrees, 30.0f, 130.0f));
            const XMMATRIX projection = XMMatrixPerspectiveFovLH(
                fov,
                aspect,
                std::max(camera.near_plane, 0.001f),
                std::max(camera.far_plane, camera.near_plane + 1.0f)
            );
            const XMMATRIX view_projection = view * projection;

            if (world_index_count_ > 0 && geometry_vertex_buffer_ && geometry_index_buffer_ && texture_array_) {
                command_list_->IASetVertexBuffers(0, 1, &geometry_vertex_view_);
                command_list_->IASetIndexBuffer(&geometry_index_view_);

                DrawConstants constants{};
                XMStoreFloat4x4(&constants.view_projection, view_projection);
                XMStoreFloat4x4(&constants.model, XMMatrixIdentity());
                constants.tint[0] = 1.0f;
                constants.tint[1] = 1.0f;
                constants.tint[2] = 1.0f;
                constants.tint[3] = 1.0f;
                command_list_->SetGraphicsRoot32BitConstants(0, 48, &constants, 0);
                command_list_->DrawIndexedInstanced(world_index_count_, 1, 0, 0, 0);
                ++frame_stats_.world_draw_calls;
            }

            if (texture_array_) {
                for (auto& group : primitive_instance_scratch_) group.clear();
                for (const Entity& entity : scene.entities) {
                    if (!entity.enabled || !entity.mesh_renderer) continue;
                    ++frame_stats_.mesh_entities_considered;
                    const auto primitive_index = static_cast<std::size_t>(entity.mesh_renderer->primitive);
                    if (primitive_index >= primitive_index_counts_.size() || primitive_index_counts_[primitive_index] == 0
                        || !primitive_vertex_buffers_[primitive_index] || !primitive_index_buffers_[primitive_index]) continue;

                    const auto t = entity_world_transform(scene, entity);
                    const float bounding_radius = 0.5f * std::sqrt(
                        t.scale.x * t.scale.x + t.scale.y * t.scale.y + t.scale.z * t.scale.z);
                    if (!frustum.intersects_sphere(t.position, bounding_radius)) {
                        ++frame_stats_.mesh_instances_culled;
                        continue;
                    }

                    const auto& mesh = *entity.mesh_renderer;
                    PrimitiveInstanceData instance{};
                    const XMMATRIX model = XMMatrixScaling(t.scale.x, t.scale.y, t.scale.z)
                        * XMMatrixRotationX(t.rotation.x)
                        * XMMatrixRotationY(t.rotation.y)
                        * XMMatrixRotationZ(t.rotation.z)
                        * XMMatrixTranslation(t.position.x, t.position.y, t.position.z);
                    XMStoreFloat4x4(&instance.model, model);
                    const auto& material = mesh.resolved_material;
                    const bool use_material = mesh.material_resolved;
                    instance.tint[0] = mesh.color[0] * (use_material ? material.base_color[0] : 1.0f);
                    instance.tint[1] = mesh.color[1] * (use_material ? material.base_color[1] : 1.0f);
                    instance.tint[2] = mesh.color[2] * (use_material ? material.base_color[2] : 1.0f);
                    instance.tint[3] = mesh.color[3] * (use_material ? material.base_color[3] : 1.0f);
                    const TextureId effective_texture = use_material && mesh.resolved_material_texture != kInvalidTexture
                        ? mesh.resolved_material_texture : mesh.texture;
                    instance.draw_params[0] = (effective_texture != kInvalidTexture && effective_texture < scene.world.textures().size())
                        ? static_cast<float>(effective_texture + 1u) : 0.0f;
                    instance.draw_params[1] = 1.0f;
                    if (use_material) {
                        instance.emission[0] = material.emission_color[0];
                        instance.emission[1] = material.emission_color[1];
                        instance.emission[2] = material.emission_color[2];
                        instance.emission[3] = material.emission_strength;
                        instance.material_params[0] = material.shader == BuiltinMaterialShader::Unlit ? 1.0f : 0.0f;
                        instance.material_params[2] = material.alpha_cutoff;
                    }
                    primitive_instance_scratch_[primitive_index].push_back(instance);
                    ++frame_stats_.mesh_instances_visible;
                }

                std::size_t total_instances = 0;
                for (const auto& group : primitive_instance_scratch_) total_instances += group.size();
                if (total_instances > 0) {
                    ensure_primitive_instance_capacity(primitive_instance_cursor_ + total_instances);
                    command_list_->SetPipelineState(primitive_instanced_pipeline_state_.Get());
                    DrawConstants constants{};
                    XMStoreFloat4x4(&constants.view_projection, view_projection);
                    command_list_->SetGraphicsRoot32BitConstants(0, 48, &constants, 0);

                    for (std::size_t primitive_index = 0; primitive_index < primitive_instance_scratch_.size(); ++primitive_index) {
                        const auto& group = primitive_instance_scratch_[primitive_index];
                        if (group.empty()) continue;
                        const std::size_t offset_instances = primitive_instance_cursor_;
                        const std::size_t bytes = group.size() * sizeof(PrimitiveInstanceData);
                        std::memcpy(primitive_instance_mapped_[frame_index_] + offset_instances * sizeof(PrimitiveInstanceData),
                            group.data(), bytes);

                        D3D12_VERTEX_BUFFER_VIEW views[2]{};
                        views[0] = primitive_vertex_views_[primitive_index];
                        views[1].BufferLocation = primitive_instance_buffers_[frame_index_]->GetGPUVirtualAddress()
                            + static_cast<UINT64>(offset_instances * sizeof(PrimitiveInstanceData));
                        views[1].SizeInBytes = static_cast<UINT>(bytes);
                        views[1].StrideInBytes = sizeof(PrimitiveInstanceData);
                        command_list_->IASetVertexBuffers(0, 2, views);
                        command_list_->IASetIndexBuffer(&primitive_index_views_[primitive_index]);
                        command_list_->DrawIndexedInstanced(primitive_index_counts_[primitive_index],
                            static_cast<UINT>(group.size()), 0, 0, 0);
                        primitive_instance_cursor_ += group.size();
                        ++frame_stats_.mesh_draw_calls;
                    }
                    command_list_->SetPipelineState(pipeline_state_.Get());
                }
            }

            if (sprite_index_count_ > 0 && sprite_vertex_buffer_ && sprite_index_buffer_ && texture_array_) {
                sprite_instance_scratch_.clear();
                for (const Entity& entity : scene.entities) {
                    if (!entity.enabled || !entity.sprite_renderer) continue;
                    ++frame_stats_.sprite_entities_considered;
                    const SpriteRendererComponent& sprite = *entity.sprite_renderer;
                    const auto world_transform = entity_world_transform(scene, entity);
                    const float sprite_width = std::abs(sprite.size.x * world_transform.scale.x);
                    const float sprite_height = std::abs(sprite.size.z * world_transform.scale.y);
                    const float sprite_radius = 0.5f * std::sqrt(
                        sprite_width * sprite_width + sprite_height * sprite_height);
                    if (!frustum.intersects_sphere(world_transform.position, sprite_radius)) {
                        ++frame_stats_.sprite_entities_culled;
                        continue;
                    }

                    const ResolvedSpriteFrame resolved = resolve_sprite_frame(scene, entity, sprite, total_seconds);
                    if (resolved.texture == kInvalidTexture || resolved.texture >= scene.world.textures().size()) continue;

                    PrimitiveInstanceData instance{};
                    const XMMATRIX model = XMMatrixScaling(
                            sprite.size.x * world_transform.scale.x,
                            sprite.size.z * world_transform.scale.y,
                            world_transform.scale.z)
                        * XMMatrixRotationY(camera.yaw)
                        * XMMatrixTranslation(
                            world_transform.position.x,
                            world_transform.position.y,
                            world_transform.position.z);
                    XMStoreFloat4x4(&instance.model, model);
                    instance.tint[0] = sprite.color[0];
                    instance.tint[1] = sprite.color[1];
                    instance.tint[2] = sprite.color[2];
                    instance.tint[3] = sprite.color[3];
                    instance.draw_params[0] = static_cast<float>(resolved.texture + 1u);
                    instance.draw_params[1] = 1.0f;
                    sprite_instance_scratch_.push_back(instance);
                    ++frame_stats_.sprite_entities_visible;
                }

                if (!sprite_instance_scratch_.empty()) {
                    ensure_primitive_instance_capacity(primitive_instance_cursor_ + sprite_instance_scratch_.size());
                    command_list_->SetPipelineState(primitive_instanced_pipeline_state_.Get());
                    DrawConstants constants{};
                    XMStoreFloat4x4(&constants.view_projection, view_projection);
                    command_list_->SetGraphicsRoot32BitConstants(0, 48, &constants, 0);

                    const std::size_t offset_instances = primitive_instance_cursor_;
                    const std::size_t bytes = sprite_instance_scratch_.size() * sizeof(PrimitiveInstanceData);
                    if (bytes > (std::numeric_limits<UINT>::max)()) {
                        throw std::runtime_error("sprite instance buffer view exceeds D3D12 byte limit");
                    }
                    std::memcpy(primitive_instance_mapped_[frame_index_] + offset_instances * sizeof(PrimitiveInstanceData),
                        sprite_instance_scratch_.data(), bytes);

                    D3D12_VERTEX_BUFFER_VIEW views[2]{};
                    views[0] = sprite_vertex_view_;
                    views[1].BufferLocation = primitive_instance_buffers_[frame_index_]->GetGPUVirtualAddress()
                        + static_cast<UINT64>(offset_instances * sizeof(PrimitiveInstanceData));
                    views[1].SizeInBytes = static_cast<UINT>(bytes);
                    views[1].StrideInBytes = sizeof(PrimitiveInstanceData);
                    command_list_->IASetVertexBuffers(0, 2, views);
                    command_list_->IASetIndexBuffer(&sprite_index_view_);
                    command_list_->DrawIndexedInstanced(
                        sprite_index_count_, static_cast<UINT>(sprite_instance_scratch_.size()), 0, 0, 0);
                    primitive_instance_cursor_ += sprite_instance_scratch_.size();
                    ++frame_stats_.sprite_draw_calls;
                    command_list_->SetPipelineState(pipeline_state_.Get());
                }
            }
        } catch (const std::exception& ex) {
            log::error(std::format("D3D12 scene render failed: {}", ex.what()));
        }
    }

    void render_ui(
        const UiRenderPacket& packet,
        const RenderViewport* requested_viewport = nullptr
    ) override {
        if (!frame_open_ || !device_ || packet.vertices.empty() || !packet.atlas || !packet.atlas->valid()) {
            return;
        }

        try {
            int view_x = 0;
            int view_y = 0;
            int view_width = static_cast<int>(width_);
            int view_height = static_cast<int>(height_);
            if (requested_viewport && requested_viewport->width > 0 && requested_viewport->height > 0) {
                view_x = std::clamp(requested_viewport->x, 0, static_cast<int>(width_));
                view_y = std::clamp(requested_viewport->y, 0, static_cast<int>(height_));
                view_width = std::clamp(requested_viewport->width, 0, static_cast<int>(width_) - view_x);
                view_height = std::clamp(requested_viewport->height, 0, static_cast<int>(height_) - view_y);
                if (view_width <= 0 || view_height <= 0) return;
            }

            const std::size_t vertex_begin = ui_vertex_cursor_;
            const std::size_t vertex_end = vertex_begin + packet.vertices.size();
            if (vertex_end < vertex_begin) throw std::runtime_error("UI vertex cursor overflow");
            ensure_ui_vertex_capacity(vertex_end);
            ensure_ui_atlas(packet);
            if (!ui_vertex_buffers_[frame_index_] || !ui_vertex_mapped_[frame_index_] || !ui_atlas_texture_ || !ui_srv_heap_) {
                return;
            }

            const UINT64 vertex_bytes = static_cast<UINT64>(packet.vertices.size() * sizeof(UiDrawVertex));
            const UINT64 vertex_offset_bytes = static_cast<UINT64>(vertex_begin * sizeof(UiDrawVertex));
            std::memcpy(ui_vertex_mapped_[frame_index_] + vertex_offset_bytes, packet.vertices.data(), static_cast<std::size_t>(vertex_bytes));
            D3D12_VERTEX_BUFFER_VIEW vertex_view{};
            vertex_view.BufferLocation = ui_vertex_buffers_[frame_index_]->GetGPUVirtualAddress() + vertex_offset_bytes;
            vertex_view.SizeInBytes = static_cast<UINT>(vertex_bytes);
            vertex_view.StrideInBytes = sizeof(UiDrawVertex);

            const D3D12_VIEWPORT viewport{
                static_cast<float>(view_x), static_cast<float>(view_y),
                static_cast<float>(view_width), static_cast<float>(view_height), 0.0f, 1.0f};
            const D3D12_RECT scissor{
                static_cast<LONG>(view_x), static_cast<LONG>(view_y),
                static_cast<LONG>(view_x + view_width), static_cast<LONG>(view_y + view_height)};
            command_list_->RSSetViewports(1, &viewport);
            command_list_->RSSetScissorRects(1, &scissor);
            const auto rtv = current_rtv();
            command_list_->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

            command_list_->SetPipelineState(ui_pipeline_state_.Get());
            command_list_->SetGraphicsRootSignature(ui_root_signature_.Get());
            ID3D12DescriptorHeap* heaps[] = {ui_srv_heap_.Get()};
            command_list_->SetDescriptorHeaps(1, heaps);
            command_list_->SetGraphicsRootDescriptorTable(1, ui_srv_heap_->GetGPUDescriptorHandleForHeapStart());
            const float ui_constants[4]{static_cast<float>(view_width), static_cast<float>(view_height), 0.0f, 0.0f};
            command_list_->SetGraphicsRoot32BitConstants(0, 4, ui_constants, 0);
            command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            command_list_->IASetVertexBuffers(0, 1, &vertex_view);
            command_list_->IASetIndexBuffer(nullptr);
            command_list_->DrawInstanced(static_cast<UINT>(packet.vertices.size()), 1, 0, 0);
            ui_vertex_cursor_ = vertex_end;
            ++ui_draws_this_frame_;
            ++frame_stats_.ui_draw_calls;
        } catch (const std::exception& ex) {
            log::error(std::format("D3D12 UI render failed: {}", ex.what()));
        }
    }

    bool end_frame() override {
        if (!frame_open_) {
            return false;
        }

        try {
            D3D12_RESOURCE_BARRIER to_present{};
            to_present.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            to_present.Transition.pResource = render_targets_[frame_index_].Get();
            to_present.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            to_present.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
            to_present.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
            command_list_->ResourceBarrier(1, &to_present);

            check_hr(command_list_->Close(), "ID3D12GraphicsCommandList::Close");
            ID3D12CommandList* lists[] = {command_list_.Get()};
            command_queue_->ExecuteCommandLists(1, lists);
            const UINT sync_interval = vsync_enabled_ ? 1u : 0u;
            const UINT present_flags = (!vsync_enabled_ && tearing_supported_) ? DXGI_PRESENT_ALLOW_TEARING : 0u;
            check_hr(swap_chain_->Present(sync_interval, present_flags), "IDXGISwapChain::Present");
            move_to_next_frame();
            frame_open_ = false;
            return true;
        } catch (const std::exception& ex) {
            log::error(std::format("D3D12 end frame failed: {}", ex.what()));
            frame_open_ = false;
            return false;
        }
    }

    [[nodiscard]] int target_width() const override { return static_cast<int>(width_); }
    [[nodiscard]] int target_height() const override { return static_cast<int>(height_); }

    [[nodiscard]] ID3D12Device* d3d12_device() const override { return device_.Get(); }
    [[nodiscard]] ID3D12CommandQueue* d3d12_command_queue() const override { return command_queue_.Get(); }
    [[nodiscard]] ID3D12GraphicsCommandList* d3d12_command_list() const override { return command_list_.Get(); }
    [[nodiscard]] DXGI_FORMAT d3d12_backbuffer_format() const override { return kBackBufferFormat; }

    [[nodiscard]] bool d3d12_copy_backbuffer_to_texture(ID3D12Resource* destination) override {
        if (!frame_open_ || !command_list_ || !destination || !render_targets_[frame_index_]) {
            return false;
        }

        const D3D12_RESOURCE_DESC source_desc = render_targets_[frame_index_]->GetDesc();
        const D3D12_RESOURCE_DESC destination_desc = destination->GetDesc();
        if (destination_desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D
            || source_desc.Width != destination_desc.Width
            || source_desc.Height != destination_desc.Height
            || source_desc.Format != destination_desc.Format
            || source_desc.SampleDesc.Count != destination_desc.SampleDesc.Count) {
            return false;
        }

        D3D12_RESOURCE_BARRIER barriers[2]{};
        barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[0].Transition.pResource = render_targets_[frame_index_].Get();
        barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;

        barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[1].Transition.pResource = destination;
        barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;

        command_list_->ResourceBarrier(2, barriers);
        command_list_->CopyResource(destination, render_targets_[frame_index_].Get());

        std::swap(barriers[0].Transition.StateBefore, barriers[0].Transition.StateAfter);
        std::swap(barriers[1].Transition.StateBefore, barriers[1].Transition.StateAfter);
        command_list_->ResourceBarrier(2, barriers);
        return true;
    }

    void d3d12_wait_for_gpu() override {
        if (command_queue_ && fence_) {
            wait_for_gpu();
        }
    }

    [[nodiscard]] bool d3d12_prepare_overlay(ID3D12DescriptorHeap* shader_visible_srv_heap) override {
        if (!frame_open_ || !command_list_ || !shader_visible_srv_heap) {
            return false;
        }

        const D3D12_DESCRIPTOR_HEAP_DESC heap_desc = shader_visible_srv_heap->GetDesc();
        if (heap_desc.Type != D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
            || (heap_desc.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) == 0) {
            return false;
        }

        // The scene pass may have left its world-texture heap and DSV bound.
        // Dear ImGui supplies GPU handles from its own heap, so both pieces of
        // state must be switched before ImGui_ImplDX12_RenderDrawData().
        const auto rtv = current_rtv();
        command_list_->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        ID3D12DescriptorHeap* heaps[] = {shader_visible_srv_heap};
        command_list_->SetDescriptorHeaps(1, heaps);
        return true;
    }

    void shutdown() override {
        if (command_queue_ && fence_) {
            wait_for_gpu();
        }

        if (fence_event_) {
            CloseHandle(fence_event_);
            fence_event_ = nullptr;
        }

        sprite_index_buffer_.Reset();
        sprite_vertex_buffer_.Reset();
        for (auto& buffer : primitive_index_buffers_) buffer.Reset();
        for (std::size_t frame = 0; frame < kFrameCount; ++frame) {
            if (primitive_instance_buffers_[frame] && primitive_instance_mapped_[frame]) {
                primitive_instance_buffers_[frame]->Unmap(0, nullptr);
            }
            primitive_instance_mapped_[frame] = nullptr;
            primitive_instance_buffers_[frame].Reset();
        }
        primitive_instance_capacity_ = 0;
        primitive_instance_cursor_ = 0;
        for (auto& group : primitive_instance_scratch_) group.clear();
        for (auto& buffer : primitive_vertex_buffers_) buffer.Reset();
        geometry_index_buffer_.Reset();
        geometry_vertex_buffer_.Reset();
        texture_array_.Reset();
        for (std::size_t frame = 0; frame < lighting_buffers_.size(); ++frame) {
            if (lighting_buffers_[frame] && lighting_mapped_[frame]) lighting_buffers_[frame]->Unmap(0, nullptr);
            lighting_mapped_[frame] = nullptr;
            lighting_buffers_[frame].Reset();
        }
        for (std::size_t frame = 0; frame < ui_vertex_buffers_.size(); ++frame) {
            if (ui_vertex_buffers_[frame] && ui_vertex_mapped_[frame]) ui_vertex_buffers_[frame]->Unmap(0, nullptr);
            ui_vertex_mapped_[frame] = nullptr;
            ui_vertex_buffers_[frame].Reset();
        }
        ui_vertex_capacity_ = 0;
        ui_atlas_upload_.Reset();
        ui_atlas_texture_.Reset();
        ui_atlas_source_ = nullptr;
        ui_atlas_revision_ = 0;
        ui_atlas_width_ = ui_atlas_height_ = 0;
        ui_pipeline_state_.Reset();
        ui_root_signature_.Reset();
        primitive_instanced_pipeline_state_.Reset();
        pipeline_state_.Reset();
        root_signature_.Reset();
        depth_target_.Reset();
        ui_srv_heap_.Reset();
        srv_heap_.Reset();
        dsv_heap_.Reset();

        for (auto& target : render_targets_) {
            target.Reset();
        }
        for (auto& allocator : command_allocators_) {
            allocator.Reset();
        }

        command_list_.Reset();
        rtv_heap_.Reset();
        swap_chain_.Reset();
        command_queue_.Reset();
        fence_.Reset();
        device_.Reset();
        adapter_.Reset();
        factory_.Reset();

        hwnd_ = nullptr;
        window_ = nullptr;
        width_ = 0;
        height_ = 0;
        frame_index_ = 0;
        rtv_descriptor_size_ = 0;
        fence_values_.fill(0);
        next_fence_value_ = 1;
        geometry_vertex_view_ = {};
        geometry_index_view_ = {};
        sprite_vertex_view_ = {};
        sprite_index_view_ = {};
        primitive_vertex_views_.fill({});
        primitive_index_views_.fill({});
        primitive_index_counts_.fill(0);
        world_index_count_ = 0;
        sprite_index_count_ = 0;
        cached_world_revision_ = 0;
        capabilities_ = {};
        frame_open_ = false;
    }

    std::string_view name() const override {
        return "Direct3D 12";
    }

    const RendererCapabilities& capabilities() const override {
        return capabilities_;
    }

private:
    void enable_debug_validation_if_available() {
#if defined(_DEBUG)
        ComPtr<ID3D12Debug> debug;
        if (FAILED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
            log::warn("D3D12 debug layer is not available. Install Windows Graphics Tools for validation messages.");
            return;
        }

        debug->EnableDebugLayer();

        const char* gpu_validation = std::getenv("VESPERA_GPU_VALIDATION");
        if (!gpu_validation) gpu_validation = std::getenv("SECTORLINE_GPU_VALIDATION"); // deprecated alias
        if (gpu_validation && std::string_view(gpu_validation) == "1") {
            ComPtr<ID3D12Debug1> debug1;
            if (SUCCEEDED(debug.As(&debug1))) {
                debug1->SetEnableGPUBasedValidation(TRUE);
                log::info("D3D12 GPU-based validation enabled by VESPERA_GPU_VALIDATION=1.");
            }
        }
#endif
    }

    void create_device() {
        ComPtr<IDXGIAdapter1> candidate;

        for (UINT adapter_index = 0;
             factory_->EnumAdapterByGpuPreference(
                 adapter_index,
                 DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                 IID_PPV_ARGS(&candidate)
             ) != DXGI_ERROR_NOT_FOUND;
             ++adapter_index) {

            DXGI_ADAPTER_DESC1 desc{};
            candidate->GetDesc1(&desc);
            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
                candidate.Reset();
                continue;
            }

            if (SUCCEEDED(D3D12CreateDevice(candidate.Get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr))) {
                adapter_ = candidate;
                break;
            }

            candidate.Reset();
        }

        if (!adapter_) {
            throw std::runtime_error("No Direct3D 12 capable hardware adapter was found.");
        }

        check_hr(
            D3D12CreateDevice(adapter_.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device_)),
            "D3D12CreateDevice"
        );
    }

    void query_capabilities() {
        DXGI_ADAPTER_DESC1 desc{};
        check_hr(adapter_->GetDesc1(&desc), "IDXGIAdapter1::GetDesc1");
        capabilities_.adapter_name = wide_to_utf8(desc.Description);
        capabilities_.dedicated_video_memory_bytes = static_cast<unsigned long long>(desc.DedicatedVideoMemory);

        std::array<D3D_FEATURE_LEVEL, 4> requested_levels{
            D3D_FEATURE_LEVEL_12_1,
            D3D_FEATURE_LEVEL_12_0,
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
        };
        D3D12_FEATURE_DATA_FEATURE_LEVELS feature_levels{};
        feature_levels.NumFeatureLevels = static_cast<UINT>(requested_levels.size());
        feature_levels.pFeatureLevelsRequested = requested_levels.data();
        if (SUCCEEDED(device_->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &feature_levels, sizeof(feature_levels)))) {
            const auto [major, minor] = feature_level_parts(feature_levels.MaxSupportedFeatureLevel);
            capabilities_.feature_level_major = major;
            capabilities_.feature_level_minor = minor;
        }

        D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5{};
        if (SUCCEEDED(device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5)))) {
            if (options5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_0) {
                capabilities_.hardware_ray_tracing = true;
                capabilities_.ray_tracing_tier_major = 1;
                capabilities_.ray_tracing_tier_minor = options5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_1 ? 1 : 0;
            }
        }

        D3D12_FEATURE_DATA_D3D12_OPTIONS6 options6{};
        if (SUCCEEDED(device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS6, &options6, sizeof(options6)))) {
            capabilities_.variable_rate_shading = options6.VariableShadingRateTier != D3D12_VARIABLE_SHADING_RATE_TIER_NOT_SUPPORTED;
        }

        D3D12_FEATURE_DATA_D3D12_OPTIONS7 options7{};
        if (SUCCEEDED(device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &options7, sizeof(options7)))) {
            capabilities_.mesh_shaders = options7.MeshShaderTier != D3D12_MESH_SHADER_TIER_NOT_SUPPORTED;
        }
    }

    void log_capabilities() const {
        const double vram_gib = static_cast<double>(capabilities_.dedicated_video_memory_bytes) / (1024.0 * 1024.0 * 1024.0);
        log::info(std::format("GPU: {} ({:.2f} GiB dedicated VRAM)", capabilities_.adapter_name, vram_gib));
        log::info(std::format(
            "D3D feature level: {}.{} | Hardware RT: {}{} | VRS: {} | Mesh shaders: {}",
            capabilities_.feature_level_major,
            capabilities_.feature_level_minor,
            yes_no(capabilities_.hardware_ray_tracing),
            capabilities_.hardware_ray_tracing
                ? std::format(" (tier {}.{})", capabilities_.ray_tracing_tier_major, capabilities_.ray_tracing_tier_minor)
                : std::string{},
            yes_no(capabilities_.variable_rate_shading),
            yes_no(capabilities_.mesh_shaders)
        ));
        log::info(std::format("Point lighting: up to {} active unshadowed lights per view", kMaxActivePointLights));
    }

    void create_command_objects() {
        D3D12_COMMAND_QUEUE_DESC queue_desc{};
        queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        queue_desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
        queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        check_hr(device_->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&command_queue_)), "CreateCommandQueue");

        for (auto& allocator : command_allocators_) {
            check_hr(
                device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)),
                "CreateCommandAllocator"
            );
        }

        check_hr(
            device_->CreateCommandList(
                0,
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                command_allocators_[0].Get(),
                nullptr,
                IID_PPV_ARGS(&command_list_)
            ),
            "CreateCommandList"
        );
        check_hr(command_list_->Close(), "Close initial command list");
    }

    void create_swap_chain() {
        int pixel_width = 0;
        int pixel_height = 0;
        if (!SDL_GetWindowSizeInPixels(window_, &pixel_width, &pixel_height)) {
            throw std::runtime_error(std::format("SDL_GetWindowSizeInPixels failed: {}", SDL_GetError()));
        }

        width_ = static_cast<UINT>(std::max(pixel_width, 1));
        height_ = static_cast<UINT>(std::max(pixel_height, 1));

        tearing_supported_ = false;
        ComPtr<IDXGIFactory5> factory5;
        if (SUCCEEDED(factory_.As(&factory5))) {
            BOOL allow_tearing = FALSE;
            if (SUCCEEDED(factory5->CheckFeatureSupport(
                    DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow_tearing, sizeof(allow_tearing)))) {
                tearing_supported_ = allow_tearing == TRUE;
            }
        }

        DXGI_SWAP_CHAIN_DESC1 swap_desc{};
        swap_desc.Width = width_;
        swap_desc.Height = height_;
        swap_desc.Format = kBackBufferFormat;
        swap_desc.Stereo = FALSE;
        swap_desc.SampleDesc.Count = 1;
        swap_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swap_desc.BufferCount = kFrameCount;
        swap_desc.Scaling = DXGI_SCALING_STRETCH;
        swap_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        swap_desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
        swap_desc.Flags = tearing_supported_ ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

        ComPtr<IDXGISwapChain1> swap_chain1;
        check_hr(
            factory_->CreateSwapChainForHwnd(
                command_queue_.Get(),
                hwnd_,
                &swap_desc,
                nullptr,
                nullptr,
                &swap_chain1
            ),
            "CreateSwapChainForHwnd"
        );

        check_hr(factory_->MakeWindowAssociation(hwnd_, DXGI_MWA_NO_ALT_ENTER), "MakeWindowAssociation");
        check_hr(swap_chain1.As(&swap_chain_), "Query IDXGISwapChain3");
        frame_index_ = swap_chain_->GetCurrentBackBufferIndex();
    }

    void create_descriptor_heaps() {
        D3D12_DESCRIPTOR_HEAP_DESC rtv_desc{};
        rtv_desc.NumDescriptors = kFrameCount;
        rtv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtv_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        check_hr(device_->CreateDescriptorHeap(&rtv_desc, IID_PPV_ARGS(&rtv_heap_)), "Create RTV descriptor heap");
        rtv_descriptor_size_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        D3D12_DESCRIPTOR_HEAP_DESC dsv_desc{};
        dsv_desc.NumDescriptors = 1;
        dsv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        dsv_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        check_hr(device_->CreateDescriptorHeap(&dsv_desc, IID_PPV_ARGS(&dsv_heap_)), "Create DSV descriptor heap");

        D3D12_DESCRIPTOR_HEAP_DESC srv_desc{};
        srv_desc.NumDescriptors = 1;
        srv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srv_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        check_hr(device_->CreateDescriptorHeap(&srv_desc, IID_PPV_ARGS(&srv_heap_)), "Create SRV descriptor heap");

        D3D12_DESCRIPTOR_HEAP_DESC ui_srv_desc{};
        ui_srv_desc.NumDescriptors = 1;
        ui_srv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        ui_srv_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        check_hr(device_->CreateDescriptorHeap(&ui_srv_desc, IID_PPV_ARGS(&ui_srv_heap_)), "Create UI SRV descriptor heap");
    }

    void create_lighting_buffers() {
        constexpr UINT64 buffer_size = kLightingConstantStride * kMaxLightingViewsPerFrame;
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_UPLOAD;
        heap.CreationNodeMask = 1;
        heap.VisibleNodeMask = 1;

        D3D12_RESOURCE_DESC resource{};
        resource.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resource.Width = buffer_size;
        resource.Height = 1;
        resource.DepthOrArraySize = 1;
        resource.MipLevels = 1;
        resource.SampleDesc.Count = 1;
        resource.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        for (UINT frame = 0; frame < kFrameCount; ++frame) {
            check_hr(device_->CreateCommittedResource(
                &heap,
                D3D12_HEAP_FLAG_NONE,
                &resource,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(&lighting_buffers_[frame])), "Create lighting constant buffer");
            D3D12_RANGE no_read{0, 0};
            check_hr(lighting_buffers_[frame]->Map(
                0, &no_read, reinterpret_cast<void**>(&lighting_mapped_[frame])),
                "Map lighting constant buffer");
            std::memset(lighting_mapped_[frame], 0, static_cast<std::size_t>(buffer_size));
        }
    }

    void upload_lighting_constants(const Scene& scene, const Camera& camera, const ViewFrustum& frustum) {
        if (!lighting_buffers_[frame_index_] || !lighting_mapped_[frame_index_]) return;
        // A frame may contain multiple editor views. Each draw sequence must keep
        // its own immutable CBV region until the GPU has consumed the command list;
        // otherwise a later Game view upload would overwrite Scene-view lighting.
        const std::size_t slot = (std::min)(lighting_view_cursor_, kMaxLightingViewsPerFrame - 1);
        ++lighting_view_cursor_;
        LightingConstants constants{};
        fill_lighting_constants(scene, camera, frustum, constants, frame_stats_);
        const UINT64 offset = static_cast<UINT64>(slot) * kLightingConstantStride;
        std::memcpy(lighting_mapped_[frame_index_] + offset, &constants, sizeof(constants));
        command_list_->SetGraphicsRootConstantBufferView(
            2, lighting_buffers_[frame_index_]->GetGPUVirtualAddress() + offset);
    }

    void create_render_targets() {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
        for (UINT i = 0; i < kFrameCount; ++i) {
            check_hr(swap_chain_->GetBuffer(i, IID_PPV_ARGS(&render_targets_[i])), "Get swap-chain buffer");
            device_->CreateRenderTargetView(render_targets_[i].Get(), nullptr, handle);
            handle.ptr += rtv_descriptor_size_;
        }
    }

    void create_depth_target() {
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        heap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heap.CreationNodeMask = 1;
        heap.VisibleNodeMask = 1;

        D3D12_RESOURCE_DESC resource{};
        resource.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        resource.Alignment = 0;
        resource.Width = width_;
        resource.Height = height_;
        resource.DepthOrArraySize = 1;
        resource.MipLevels = 1;
        resource.Format = kDepthFormat;
        resource.SampleDesc.Count = 1;
        resource.SampleDesc.Quality = 0;
        resource.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        resource.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        D3D12_CLEAR_VALUE clear{};
        clear.Format = kDepthFormat;
        clear.DepthStencil.Depth = 1.0f;
        clear.DepthStencil.Stencil = 0;

        check_hr(
            device_->CreateCommittedResource(
                &heap,
                D3D12_HEAP_FLAG_NONE,
                &resource,
                D3D12_RESOURCE_STATE_DEPTH_WRITE,
                &clear,
                IID_PPV_ARGS(&depth_target_)
            ),
            "Create depth target"
        );

        D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
        dsv.Format = kDepthFormat;
        dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        dsv.Flags = D3D12_DSV_FLAG_NONE;
        device_->CreateDepthStencilView(depth_target_.Get(), &dsv, dsv_heap_->GetCPUDescriptorHandleForHeapStart());
    }

    ComPtr<ID3DBlob> compile_shader(const char* entry, const char* target) {
        UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
        flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
        flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

        ComPtr<ID3DBlob> bytecode;
        ComPtr<ID3DBlob> errors;
        const HRESULT hr = D3DCompile(
            kShaderSource.data(),
            kShaderSource.size(),
            "sectorline_builtin_world.hlsl",
            nullptr,
            nullptr,
            entry,
            target,
            flags,
            0,
            &bytecode,
            &errors
        );

        if (errors && errors->GetBufferPointer()) {
            const auto* message = static_cast<const char*>(errors->GetBufferPointer());
            if (FAILED(hr)) {
                log::error(std::format("HLSL compiler: {}", message));
            } else {
                log::warn(std::format("HLSL compiler: {}", message));
            }
        }

        check_hr(hr, std::format("D3DCompile {}", entry));
        return bytecode;
    }

    void create_pipeline() {
        D3D12_DESCRIPTOR_RANGE texture_range{};
        texture_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        texture_range.NumDescriptors = 1;
        texture_range.BaseShaderRegister = 0;
        texture_range.RegisterSpace = 0;
        texture_range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        std::array<D3D12_ROOT_PARAMETER, 3> root_parameters{};
        root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        root_parameters[0].Constants.ShaderRegister = 0;
        root_parameters[0].Constants.RegisterSpace = 0;
        root_parameters[0].Constants.Num32BitValues = 48;
        root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        root_parameters[1].DescriptorTable.NumDescriptorRanges = 1;
        root_parameters[1].DescriptorTable.pDescriptorRanges = &texture_range;
        root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        root_parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        root_parameters[2].Descriptor.ShaderRegister = 1;
        root_parameters[2].Descriptor.RegisterSpace = 0;
        root_parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_STATIC_SAMPLER_DESC sampler{};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.MipLODBias = 0.0f;
        sampler.MaxAnisotropy = 1;
        sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
        sampler.MinLOD = 0.0f;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;
        sampler.ShaderRegister = 0;
        sampler.RegisterSpace = 0;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC root_desc{};
        root_desc.NumParameters = static_cast<UINT>(root_parameters.size());
        root_desc.pParameters = root_parameters.data();
        root_desc.NumStaticSamplers = 1;
        root_desc.pStaticSamplers = &sampler;
        root_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ComPtr<ID3DBlob> serialized_root;
        ComPtr<ID3DBlob> root_errors;
        const HRESULT root_hr = D3D12SerializeRootSignature(
            &root_desc,
            D3D_ROOT_SIGNATURE_VERSION_1,
            &serialized_root,
            &root_errors
        );
        if (FAILED(root_hr) && root_errors && root_errors->GetBufferPointer()) {
            log::error(std::format("Root signature compiler: {}", static_cast<const char*>(root_errors->GetBufferPointer())));
        }
        check_hr(root_hr, "D3D12SerializeRootSignature");
        check_hr(
            device_->CreateRootSignature(
                0,
                serialized_root->GetBufferPointer(),
                serialized_root->GetBufferSize(),
                IID_PPV_ARGS(&root_signature_)
            ),
            "CreateRootSignature"
        );

        const ComPtr<ID3DBlob> vertex_shader = compile_shader("vs_main", "vs_5_1");
        const ComPtr<ID3DBlob> instanced_vertex_shader = compile_shader("vs_instanced", "vs_5_1");
        const ComPtr<ID3DBlob> pixel_shader = compile_shader("ps_main", "ps_5_1");

        const std::array<D3D12_INPUT_ELEMENT_DESC, 4> input_layout{{
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 1, DXGI_FORMAT_R32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        }};

        D3D12_RASTERIZER_DESC rasterizer{};
        rasterizer.FillMode = D3D12_FILL_MODE_SOLID;
        rasterizer.CullMode = D3D12_CULL_MODE_NONE;
        rasterizer.FrontCounterClockwise = FALSE;
        rasterizer.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
        rasterizer.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
        rasterizer.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
        rasterizer.DepthClipEnable = TRUE;
        rasterizer.MultisampleEnable = FALSE;
        rasterizer.AntialiasedLineEnable = FALSE;
        rasterizer.ForcedSampleCount = 0;
        rasterizer.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

        D3D12_RENDER_TARGET_BLEND_DESC target_blend{};
        target_blend.BlendEnable = FALSE;
        target_blend.LogicOpEnable = FALSE;
        target_blend.SrcBlend = D3D12_BLEND_ONE;
        target_blend.DestBlend = D3D12_BLEND_ZERO;
        target_blend.BlendOp = D3D12_BLEND_OP_ADD;
        target_blend.SrcBlendAlpha = D3D12_BLEND_ONE;
        target_blend.DestBlendAlpha = D3D12_BLEND_ZERO;
        target_blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        target_blend.LogicOp = D3D12_LOGIC_OP_NOOP;
        target_blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

        D3D12_BLEND_DESC blend{};
        blend.AlphaToCoverageEnable = FALSE;
        blend.IndependentBlendEnable = FALSE;
        for (auto& target : blend.RenderTarget) {
            target = target_blend;
        }

        D3D12_DEPTH_STENCIL_DESC depth{};
        depth.DepthEnable = TRUE;
        depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        depth.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        depth.StencilEnable = FALSE;
        depth.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
        depth.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
        pso.pRootSignature = root_signature_.Get();
        pso.VS = {vertex_shader->GetBufferPointer(), vertex_shader->GetBufferSize()};
        pso.PS = {pixel_shader->GetBufferPointer(), pixel_shader->GetBufferSize()};
        pso.BlendState = blend;
        pso.SampleMask = std::numeric_limits<UINT>::max();
        pso.RasterizerState = rasterizer;
        pso.DepthStencilState = depth;
        pso.InputLayout = {input_layout.data(), static_cast<UINT>(input_layout.size())};
        pso.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
        pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pso.NumRenderTargets = 1;
        pso.RTVFormats[0] = kBackBufferFormat;
        pso.DSVFormat = kDepthFormat;
        pso.SampleDesc.Count = 1;
        pso.SampleDesc.Quality = 0;

        check_hr(device_->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&pipeline_state_)), "CreateGraphicsPipelineState");

        const std::array<D3D12_INPUT_ELEMENT_DESC, 12> instanced_input_layout{{
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 1, DXGI_FORMAT_R32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"INSTANCE_MODEL", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
            {"INSTANCE_MODEL", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
            {"INSTANCE_MODEL", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 32, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
            {"INSTANCE_MODEL", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 48, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
            {"INSTANCE_TINT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 64, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
            {"INSTANCE_DRAW", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 80, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
            {"INSTANCE_EMISSION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 96, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
            {"INSTANCE_MATERIAL", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 112, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        }};
        D3D12_GRAPHICS_PIPELINE_STATE_DESC instanced_pso = pso;
        instanced_pso.VS = {instanced_vertex_shader->GetBufferPointer(), instanced_vertex_shader->GetBufferSize()};
        instanced_pso.InputLayout = {instanced_input_layout.data(), static_cast<UINT>(instanced_input_layout.size())};
        check_hr(device_->CreateGraphicsPipelineState(
            &instanced_pso, IID_PPV_ARGS(&primitive_instanced_pipeline_state_)),
            "Create instanced primitive graphics pipeline state");
    }

    ComPtr<ID3DBlob> compile_ui_shader(const char* entry, const char* target) {
        UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
        flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
        flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif
        ComPtr<ID3DBlob> bytecode;
        ComPtr<ID3DBlob> errors;
        const HRESULT hr = D3DCompile(
            kUiShaderSource.data(), kUiShaderSource.size(), "vespera_builtin_ui.hlsl",
            nullptr, nullptr, entry, target, flags, 0, &bytecode, &errors);
        if (errors && errors->GetBufferPointer()) {
            const auto* message = static_cast<const char*>(errors->GetBufferPointer());
            if (FAILED(hr)) log::error(std::format("UI HLSL compiler: {}", message));
            else log::warn(std::format("UI HLSL compiler: {}", message));
        }
        check_hr(hr, std::format("D3DCompile UI {}", entry));
        return bytecode;
    }

    void create_ui_pipeline() {
        D3D12_DESCRIPTOR_RANGE texture_range{};
        texture_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        texture_range.NumDescriptors = 1;
        texture_range.BaseShaderRegister = 0;
        texture_range.RegisterSpace = 0;
        texture_range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        std::array<D3D12_ROOT_PARAMETER, 2> root_parameters{};
        root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        root_parameters[0].Constants.ShaderRegister = 0;
        root_parameters[0].Constants.RegisterSpace = 0;
        root_parameters[0].Constants.Num32BitValues = 4;
        root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        root_parameters[1].DescriptorTable.NumDescriptorRanges = 1;
        root_parameters[1].DescriptorTable.pDescriptorRanges = &texture_range;
        root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_STATIC_SAMPLER_DESC sampler{};
        // UI atlas content now includes antialiased native-font glyphs and
        // scalable skins. Linear filtering avoids the old pixel/Atari look.
        sampler.Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.MipLODBias = 0.0f;
        sampler.MaxAnisotropy = 1;
        sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
        sampler.MinLOD = 0.0f;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;
        sampler.ShaderRegister = 0;
        sampler.RegisterSpace = 0;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC root_desc{};
        root_desc.NumParameters = static_cast<UINT>(root_parameters.size());
        root_desc.pParameters = root_parameters.data();
        root_desc.NumStaticSamplers = 1;
        root_desc.pStaticSamplers = &sampler;
        root_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ComPtr<ID3DBlob> serialized;
        ComPtr<ID3DBlob> errors;
        const HRESULT root_hr = D3D12SerializeRootSignature(
            &root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &errors);
        if (FAILED(root_hr) && errors && errors->GetBufferPointer()) {
            log::error(std::format("UI root signature compiler: {}", static_cast<const char*>(errors->GetBufferPointer())));
        }
        check_hr(root_hr, "D3D12SerializeRootSignature UI");
        check_hr(device_->CreateRootSignature(
            0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(&ui_root_signature_)),
            "Create UI root signature");

        const auto vertex_shader = compile_ui_shader("ui_vs", "vs_5_1");
        const auto pixel_shader = compile_ui_shader("ui_ps", "ps_5_1");
        const std::array<D3D12_INPUT_ELEMENT_DESC, 3> input_layout{{
            {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        }};
        static_assert(sizeof(UiDrawVertex) == 20);

        D3D12_RASTERIZER_DESC rasterizer{};
        rasterizer.FillMode = D3D12_FILL_MODE_SOLID;
        rasterizer.CullMode = D3D12_CULL_MODE_NONE;
        rasterizer.FrontCounterClockwise = FALSE;
        rasterizer.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
        rasterizer.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
        rasterizer.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
        rasterizer.DepthClipEnable = TRUE;

        D3D12_RENDER_TARGET_BLEND_DESC target_blend{};
        target_blend.BlendEnable = TRUE;
        target_blend.LogicOpEnable = FALSE;
        target_blend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
        target_blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        target_blend.BlendOp = D3D12_BLEND_OP_ADD;
        target_blend.SrcBlendAlpha = D3D12_BLEND_ONE;
        target_blend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        target_blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        target_blend.LogicOp = D3D12_LOGIC_OP_NOOP;
        target_blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        D3D12_BLEND_DESC blend{};
        blend.RenderTarget[0] = target_blend;

        D3D12_DEPTH_STENCIL_DESC depth{};
        depth.DepthEnable = FALSE;
        depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        depth.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        depth.StencilEnable = FALSE;

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
        pso.pRootSignature = ui_root_signature_.Get();
        pso.VS = {vertex_shader->GetBufferPointer(), vertex_shader->GetBufferSize()};
        pso.PS = {pixel_shader->GetBufferPointer(), pixel_shader->GetBufferSize()};
        pso.BlendState = blend;
        pso.SampleMask = std::numeric_limits<UINT>::max();
        pso.RasterizerState = rasterizer;
        pso.DepthStencilState = depth;
        pso.InputLayout = {input_layout.data(), static_cast<UINT>(input_layout.size())};
        pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pso.NumRenderTargets = 1;
        pso.RTVFormats[0] = kBackBufferFormat;
        pso.DSVFormat = DXGI_FORMAT_UNKNOWN;
        pso.SampleDesc.Count = 1;
        check_hr(device_->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&ui_pipeline_state_)),
            "Create UI graphics pipeline state");
    }

    void ensure_primitive_instance_capacity(std::size_t required_instances) {
        if (required_instances <= primitive_instance_capacity_
            && primitive_instance_buffers_[0] && primitive_instance_buffers_[1]) return;
        if (required_instances == 0) return;
        if (primitive_instance_cursor_ != 0) {
            throw std::runtime_error("primitive instance buffer budget exceeded after an earlier scene pass");
        }
        if (primitive_instance_capacity_ > 0) wait_for_gpu();
        std::size_t next_capacity = std::max<std::size_t>(65536, primitive_instance_capacity_ == 0 ? 65536 : primitive_instance_capacity_);
        while (next_capacity < required_instances) {
            if (next_capacity > (std::numeric_limits<std::size_t>::max)() / 2) {
                throw std::runtime_error("primitive instance buffer capacity overflow");
            }
            next_capacity *= 2;
        }

        for (std::size_t frame = 0; frame < kFrameCount; ++frame) {
            if (primitive_instance_buffers_[frame] && primitive_instance_mapped_[frame]) {
                primitive_instance_buffers_[frame]->Unmap(0, nullptr);
            }
            primitive_instance_mapped_[frame] = nullptr;
            primitive_instance_buffers_[frame].Reset();

            D3D12_HEAP_PROPERTIES heap{};
            heap.Type = D3D12_HEAP_TYPE_UPLOAD;
            heap.CreationNodeMask = 1;
            heap.VisibleNodeMask = 1;
            D3D12_RESOURCE_DESC desc{};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            desc.Width = static_cast<UINT64>(next_capacity * sizeof(PrimitiveInstanceData));
            desc.Height = 1;
            desc.DepthOrArraySize = 1;
            desc.MipLevels = 1;
            desc.SampleDesc.Count = 1;
            desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            check_hr(device_->CreateCommittedResource(
                &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                IID_PPV_ARGS(&primitive_instance_buffers_[frame])), "Create primitive instance upload buffer");
            D3D12_RANGE no_read{0, 0};
            check_hr(primitive_instance_buffers_[frame]->Map(
                0, &no_read, reinterpret_cast<void**>(&primitive_instance_mapped_[frame])),
                "Map primitive instance upload buffer");
        }
        primitive_instance_capacity_ = next_capacity;
    }

    void ensure_ui_vertex_capacity(std::size_t required_vertices) {
        if (required_vertices <= ui_vertex_capacity_ && ui_vertex_buffers_[0] && ui_vertex_buffers_[1]) return;
        if (required_vertices == 0) return;
        if (ui_draws_this_frame_ != 0) {
            throw std::runtime_error("UI per-frame vertex budget exceeded after an earlier UI draw");
        }
        if (ui_vertex_capacity_ > 0) wait_for_gpu();
        std::size_t next_capacity = std::max<std::size_t>(65536, ui_vertex_capacity_ == 0 ? 65536 : ui_vertex_capacity_);
        while (next_capacity < required_vertices) {
            if (next_capacity > (std::numeric_limits<std::size_t>::max)() / 2) {
                throw std::runtime_error("UI vertex buffer capacity overflow");
            }
            next_capacity *= 2;
        }
        for (std::size_t frame = 0; frame < kFrameCount; ++frame) {
            if (ui_vertex_buffers_[frame] && ui_vertex_mapped_[frame]) {
                ui_vertex_buffers_[frame]->Unmap(0, nullptr);
            }
            ui_vertex_mapped_[frame] = nullptr;
            ui_vertex_buffers_[frame].Reset();

            D3D12_HEAP_PROPERTIES heap{};
            heap.Type = D3D12_HEAP_TYPE_UPLOAD;
            heap.CreationNodeMask = 1;
            heap.VisibleNodeMask = 1;
            D3D12_RESOURCE_DESC desc{};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            desc.Width = static_cast<UINT64>(next_capacity * sizeof(UiDrawVertex));
            desc.Height = 1;
            desc.DepthOrArraySize = 1;
            desc.MipLevels = 1;
            desc.SampleDesc.Count = 1;
            desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            check_hr(device_->CreateCommittedResource(
                &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                IID_PPV_ARGS(&ui_vertex_buffers_[frame])), "Create UI vertex upload buffer");
            D3D12_RANGE no_read{0, 0};
            check_hr(ui_vertex_buffers_[frame]->Map(
                0, &no_read, reinterpret_cast<void**>(&ui_vertex_mapped_[frame])), "Map UI vertex upload buffer");
        }
        ui_vertex_capacity_ = next_capacity;
    }

    void ensure_ui_atlas(const UiRenderPacket& packet) {
        if (!packet.atlas || !packet.atlas->valid()) return;
        if (ui_atlas_source_ == packet.atlas
            && ui_atlas_revision_ == packet.atlas_revision
            && ui_atlas_width_ == packet.atlas->width
            && ui_atlas_height_ == packet.atlas->height
            && ui_atlas_texture_) {
            return;
        }
        if (ui_draws_this_frame_ != 0) {
            throw std::runtime_error("multiple different UI atlases in one frame are not supported yet");
        }
        if (ui_atlas_texture_) wait_for_gpu();
        ui_atlas_texture_.Reset();
        ui_atlas_upload_.Reset();

        D3D12_HEAP_PROPERTIES default_heap{};
        default_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        default_heap.CreationNodeMask = 1;
        default_heap.VisibleNodeMask = 1;
        D3D12_RESOURCE_DESC texture_desc{};
        texture_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texture_desc.Width = packet.atlas->width;
        texture_desc.Height = packet.atlas->height;
        texture_desc.DepthOrArraySize = 1;
        texture_desc.MipLevels = 1;
        texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        texture_desc.SampleDesc.Count = 1;
        texture_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        check_hr(device_->CreateCommittedResource(
            &default_heap, D3D12_HEAP_FLAG_NONE, &texture_desc, D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr, IID_PPV_ARGS(&ui_atlas_texture_)), "Create UI atlas texture");

        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        UINT row_count = 0;
        UINT64 row_size = 0;
        UINT64 upload_size = 0;
        device_->GetCopyableFootprints(&texture_desc, 0, 1, 0, &footprint, &row_count, &row_size, &upload_size);
        D3D12_HEAP_PROPERTIES upload_heap{};
        upload_heap.Type = D3D12_HEAP_TYPE_UPLOAD;
        upload_heap.CreationNodeMask = 1;
        upload_heap.VisibleNodeMask = 1;
        D3D12_RESOURCE_DESC upload_desc{};
        upload_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        upload_desc.Width = upload_size;
        upload_desc.Height = 1;
        upload_desc.DepthOrArraySize = 1;
        upload_desc.MipLevels = 1;
        upload_desc.SampleDesc.Count = 1;
        upload_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        check_hr(device_->CreateCommittedResource(
            &upload_heap, D3D12_HEAP_FLAG_NONE, &upload_desc, D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr, IID_PPV_ARGS(&ui_atlas_upload_)), "Create UI atlas upload buffer");

        void* mapped = nullptr;
        D3D12_RANGE no_read{0, 0};
        check_hr(ui_atlas_upload_->Map(0, &no_read, &mapped), "Map UI atlas upload buffer");
        auto* bytes = static_cast<std::uint8_t*>(mapped) + footprint.Offset;
        const std::size_t source_row_bytes = static_cast<std::size_t>(packet.atlas->width) * 4u;
        for (UINT row = 0; row < row_count; ++row) {
            std::memcpy(bytes + static_cast<std::size_t>(row) * footprint.Footprint.RowPitch,
                packet.atlas->rgba8.data() + static_cast<std::size_t>(row) * source_row_bytes,
                std::min<std::size_t>(source_row_bytes, static_cast<std::size_t>(row_size)));
        }
        ui_atlas_upload_->Unmap(0, nullptr);

        D3D12_TEXTURE_COPY_LOCATION destination{};
        destination.pResource = ui_atlas_texture_.Get();
        destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        destination.SubresourceIndex = 0;
        D3D12_TEXTURE_COPY_LOCATION source{};
        source.pResource = ui_atlas_upload_.Get();
        source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        source.PlacedFootprint = footprint;
        command_list_->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
        D3D12_RESOURCE_BARRIER ready{};
        ready.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        ready.Transition.pResource = ui_atlas_texture_.Get();
        ready.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ready.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        ready.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        command_list_->ResourceBarrier(1, &ready);

        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Texture2D.MostDetailedMip = 0;
        srv.Texture2D.MipLevels = 1;
        srv.Texture2D.PlaneSlice = 0;
        srv.Texture2D.ResourceMinLODClamp = 0.0f;
        device_->CreateShaderResourceView(ui_atlas_texture_.Get(), &srv, ui_srv_heap_->GetCPUDescriptorHandleForHeapStart());

        ui_atlas_source_ = packet.atlas;
        ui_atlas_revision_ = packet.atlas_revision;
        ui_atlas_width_ = packet.atlas->width;
        ui_atlas_height_ = packet.atlas->height;
        log::info(std::format("Runtime UI atlas uploaded: {}x{} RGBA8", ui_atlas_width_, ui_atlas_height_));
    }

    void ensure_world_geometry(const SectorWorld& world) {
        if (cached_world_revision_ == world.revision()) {
            return;
        }

        wait_for_gpu();
        create_world_texture_array(world);
        const SectorMesh mesh = build_sector_mesh(world);
        create_geometry_buffers(mesh);
        cached_world_revision_ = world.revision();
        log::info(std::format(
            "World mesh rebuilt: {} sectors | {} vertices | {} triangles | {} textures",
            world.sectors().size(),
            mesh.vertices.size(),
            mesh.indices.size() / 3,
            world.textures().size()
        ));
    }

    void create_world_texture_array(const SectorWorld& world) {
        texture_array_.Reset();

        std::uint32_t texture_width = 1;
        std::uint32_t texture_height = 1;
        for (const TextureData& texture : world.textures()) {
            if (texture.valid()) {
                texture_width = texture.width;
                texture_height = texture.height;
                break;
            }
        }

        if (texture_width > 16384u || texture_height > 16384u) {
            throw std::runtime_error("World texture dimensions exceed the 0.1.x safety limit (16384 pixels).");
        }

        const std::size_t layer_count_size = world.textures().size() + 1u;
        if (layer_count_size > static_cast<std::size_t>(std::numeric_limits<UINT16>::max())) {
            throw std::runtime_error("Too many world texture layers for the D3D12 texture array.");
        }
        const UINT layer_count = static_cast<UINT>(layer_count_size);

        const std::size_t pixels_per_layer = static_cast<std::size_t>(texture_width)
            * static_cast<std::size_t>(texture_height) * 4u;
        std::vector<std::uint8_t> layer_pixels(pixels_per_layer * layer_count_size, 255u);

        // Layer zero is always plain white so untextured materials remain valid.
        std::fill(layer_pixels.begin(), layer_pixels.begin() + static_cast<std::ptrdiff_t>(pixels_per_layer), 255u);

        for (std::size_t texture_index = 0; texture_index < world.textures().size(); ++texture_index) {
            const TextureData& texture = world.textures()[texture_index];
            auto* destination = layer_pixels.data() + (texture_index + 1u) * pixels_per_layer;

            if (texture.valid() && texture.width == texture_width && texture.height == texture_height) {
                std::memcpy(destination, texture.rgba8.data(), pixels_per_layer);
                continue;
            }

            // Invalid or differently sized textures get a conspicuous checker
            // instead of crashing or sampling undefined memory. Full importer
            // resampling belongs in the later asset pipeline.
            for (std::uint32_t y = 0; y < texture_height; ++y) {
                for (std::uint32_t x = 0; x < texture_width; ++x) {
                    const bool alternate = ((x / 8u) + (y / 8u)) % 2u != 0u;
                    const std::size_t pixel = (static_cast<std::size_t>(y) * texture_width + x) * 4u;
                    destination[pixel + 0] = 255u;
                    destination[pixel + 1] = alternate ? 0u : 40u;
                    destination[pixel + 2] = 255u;
                    destination[pixel + 3] = 255u;
                }
            }

            log::warn(std::format(
                "Texture '{}' does not match the {}x{} RGBA8 world texture-array format; using fallback pixels.",
                texture.name,
                texture_width,
                texture_height
            ));
        }

        D3D12_HEAP_PROPERTIES default_heap{};
        default_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        default_heap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        default_heap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        default_heap.CreationNodeMask = 1;
        default_heap.VisibleNodeMask = 1;

        D3D12_RESOURCE_DESC texture_desc{};
        texture_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texture_desc.Alignment = 0;
        texture_desc.Width = texture_width;
        texture_desc.Height = texture_height;
        texture_desc.DepthOrArraySize = static_cast<UINT16>(layer_count);
        texture_desc.MipLevels = 1;
        texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        texture_desc.SampleDesc.Count = 1;
        texture_desc.SampleDesc.Quality = 0;
        texture_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        texture_desc.Flags = D3D12_RESOURCE_FLAG_NONE;

        check_hr(
            device_->CreateCommittedResource(
                &default_heap,
                D3D12_HEAP_FLAG_NONE,
                &texture_desc,
                D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr,
                IID_PPV_ARGS(&texture_array_)
            ),
            "Create world texture array"
        );

        std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> layouts(layer_count);
        std::vector<UINT> row_counts(layer_count);
        std::vector<UINT64> row_sizes(layer_count);
        UINT64 upload_bytes = 0;
        device_->GetCopyableFootprints(
            &texture_desc,
            0,
            layer_count,
            0,
            layouts.data(),
            row_counts.data(),
            row_sizes.data(),
            &upload_bytes
        );

        D3D12_HEAP_PROPERTIES upload_heap{};
        upload_heap.Type = D3D12_HEAP_TYPE_UPLOAD;
        upload_heap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        upload_heap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        upload_heap.CreationNodeMask = 1;
        upload_heap.VisibleNodeMask = 1;

        D3D12_RESOURCE_DESC upload_desc{};
        upload_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        upload_desc.Alignment = 0;
        upload_desc.Width = std::max<UINT64>(upload_bytes, 1u);
        upload_desc.Height = 1;
        upload_desc.DepthOrArraySize = 1;
        upload_desc.MipLevels = 1;
        upload_desc.Format = DXGI_FORMAT_UNKNOWN;
        upload_desc.SampleDesc.Count = 1;
        upload_desc.SampleDesc.Quality = 0;
        upload_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        upload_desc.Flags = D3D12_RESOURCE_FLAG_NONE;

        ComPtr<ID3D12Resource> upload_buffer;
        check_hr(
            device_->CreateCommittedResource(
                &upload_heap,
                D3D12_HEAP_FLAG_NONE,
                &upload_desc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(&upload_buffer)
            ),
            "Create world texture upload buffer"
        );

        void* mapped = nullptr;
        D3D12_RANGE read_range{0, 0};
        check_hr(upload_buffer->Map(0, &read_range, &mapped), "Map world texture upload buffer");
        auto* mapped_bytes = static_cast<std::uint8_t*>(mapped);
        for (UINT layer = 0; layer < layer_count; ++layer) {
            const auto* source = layer_pixels.data() + static_cast<std::size_t>(layer) * pixels_per_layer;
            const auto& layout = layouts[layer];
            auto* destination = mapped_bytes + layout.Offset;
            const std::size_t source_row_bytes = static_cast<std::size_t>(texture_width) * 4u;
            for (UINT row = 0; row < row_counts[layer]; ++row) {
                std::memcpy(
                    destination + static_cast<std::size_t>(row) * layout.Footprint.RowPitch,
                    source + static_cast<std::size_t>(row) * source_row_bytes,
                    std::min<std::size_t>(source_row_bytes, static_cast<std::size_t>(row_sizes[layer]))
                );
            }
        }
        upload_buffer->Unmap(0, nullptr);

        ComPtr<ID3D12CommandAllocator> upload_allocator;
        check_hr(
            device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&upload_allocator)),
            "Create texture upload command allocator"
        );

        ComPtr<ID3D12GraphicsCommandList> upload_list;
        check_hr(
            device_->CreateCommandList(
                0,
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                upload_allocator.Get(),
                nullptr,
                IID_PPV_ARGS(&upload_list)
            ),
            "Create texture upload command list"
        );

        for (UINT layer = 0; layer < layer_count; ++layer) {
            D3D12_TEXTURE_COPY_LOCATION destination{};
            destination.pResource = texture_array_.Get();
            destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            destination.SubresourceIndex = layer;

            D3D12_TEXTURE_COPY_LOCATION source{};
            source.pResource = upload_buffer.Get();
            source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            source.PlacedFootprint = layouts[layer];

            upload_list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
        }

        D3D12_RESOURCE_BARRIER ready{};
        ready.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        ready.Transition.pResource = texture_array_.Get();
        ready.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ready.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        ready.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        upload_list->ResourceBarrier(1, &ready);
        check_hr(upload_list->Close(), "Close texture upload command list");

        ID3D12CommandList* upload_lists[] = {upload_list.Get()};
        command_queue_->ExecuteCommandLists(1, upload_lists);
        wait_for_gpu();

        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        srv.Texture2DArray.MostDetailedMip = 0;
        srv.Texture2DArray.MipLevels = 1;
        srv.Texture2DArray.FirstArraySlice = 0;
        srv.Texture2DArray.ArraySize = layer_count;
        srv.Texture2DArray.PlaneSlice = 0;
        srv.Texture2DArray.ResourceMinLODClamp = 0.0f;
        device_->CreateShaderResourceView(texture_array_.Get(), &srv, srv_heap_->GetCPUDescriptorHandleForHeapStart());

        log::info(std::format(
            "Scene texture array uploaded: {} textures + fallback | {}x{} RGBA8 | point filtered",
            world.textures().size(),
            texture_width,
            texture_height
        ));
    }

    void create_sprite_geometry() {
        const std::array<SectorMeshVertex, 4> vertices{{
            {{-0.5f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}, 0.0f},
            {{ 0.5f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}, 0.0f},
            {{ 0.5f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}, 0.0f},
            {{-0.5f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}, 0.0f},
        }};
        const std::array<std::uint32_t, 6> indices{{0, 1, 2, 0, 2, 3}};

        auto create_upload_buffer = [&](const void* source, UINT64 size_bytes, ComPtr<ID3D12Resource>& resource, std::string_view label) {
            D3D12_HEAP_PROPERTIES heap{};
            heap.Type = D3D12_HEAP_TYPE_UPLOAD;
            heap.CreationNodeMask = 1;
            heap.VisibleNodeMask = 1;

            D3D12_RESOURCE_DESC desc{};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            desc.Width = size_bytes;
            desc.Height = 1;
            desc.DepthOrArraySize = 1;
            desc.MipLevels = 1;
            desc.SampleDesc.Count = 1;
            desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

            check_hr(
                device_->CreateCommittedResource(
                    &heap,
                    D3D12_HEAP_FLAG_NONE,
                    &desc,
                    D3D12_RESOURCE_STATE_GENERIC_READ,
                    nullptr,
                    IID_PPV_ARGS(&resource)
                ),
                std::format("Create {}", label)
            );

            void* mapped = nullptr;
            D3D12_RANGE read_range{0, 0};
            check_hr(resource->Map(0, &read_range, &mapped), std::format("Map {}", label));
            std::memcpy(mapped, source, static_cast<std::size_t>(size_bytes));
            resource->Unmap(0, nullptr);
        };

        const UINT64 vertex_bytes = static_cast<UINT64>(vertices.size() * sizeof(SectorMeshVertex));
        create_upload_buffer(vertices.data(), vertex_bytes, sprite_vertex_buffer_, "sprite vertex buffer");
        sprite_vertex_view_.BufferLocation = sprite_vertex_buffer_->GetGPUVirtualAddress();
        sprite_vertex_view_.SizeInBytes = static_cast<UINT>(vertex_bytes);
        sprite_vertex_view_.StrideInBytes = sizeof(SectorMeshVertex);

        const UINT64 index_bytes = static_cast<UINT64>(indices.size() * sizeof(std::uint32_t));
        create_upload_buffer(indices.data(), index_bytes, sprite_index_buffer_, "sprite index buffer");
        sprite_index_view_.BufferLocation = sprite_index_buffer_->GetGPUVirtualAddress();
        sprite_index_view_.SizeInBytes = static_cast<UINT>(index_bytes);
        sprite_index_view_.Format = DXGI_FORMAT_R32_UINT;
        sprite_index_count_ = static_cast<UINT>(indices.size());
    }

    void create_primitive_geometry() {
        auto append_vertex = [](SectorMesh& mesh, float x, float y, float z, float u, float v) {
            mesh.vertices.push_back({{x,y,z},{1.0f,1.0f,1.0f},{u,v},0.0f});
        };
        auto append_quad = [&](SectorMesh& mesh, Vec3 a, Vec3 b, Vec3 c, Vec3 d) {
            const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
            append_vertex(mesh,a.x,a.y,a.z,0,1); append_vertex(mesh,b.x,b.y,b.z,1,1);
            append_vertex(mesh,c.x,c.y,c.z,1,0); append_vertex(mesh,d.x,d.y,d.z,0,0);
            mesh.indices.insert(mesh.indices.end(),{base,base+1,base+2,base,base+2,base+3});
        };
        std::array<SectorMesh,4> meshes;
        // Cube: 1m centered primitive with independent UVs per face.
        auto& cube=meshes[static_cast<std::size_t>(PrimitiveMeshType::Cube)];
        append_quad(cube,{-0.5f,-0.5f,-0.5f},{0.5f,-0.5f,-0.5f},{0.5f,0.5f,-0.5f},{-0.5f,0.5f,-0.5f});
        append_quad(cube,{0.5f,-0.5f,0.5f},{-0.5f,-0.5f,0.5f},{-0.5f,0.5f,0.5f},{0.5f,0.5f,0.5f});
        append_quad(cube,{-0.5f,-0.5f,0.5f},{-0.5f,-0.5f,-0.5f},{-0.5f,0.5f,-0.5f},{-0.5f,0.5f,0.5f});
        append_quad(cube,{0.5f,-0.5f,-0.5f},{0.5f,-0.5f,0.5f},{0.5f,0.5f,0.5f},{0.5f,0.5f,-0.5f});
        append_quad(cube,{-0.5f,0.5f,-0.5f},{0.5f,0.5f,-0.5f},{0.5f,0.5f,0.5f},{-0.5f,0.5f,0.5f});
        append_quad(cube,{-0.5f,-0.5f,0.5f},{0.5f,-0.5f,0.5f},{0.5f,-0.5f,-0.5f},{-0.5f,-0.5f,-0.5f});
        auto& plane=meshes[static_cast<std::size_t>(PrimitiveMeshType::Plane)];
        append_quad(plane,{-0.5f,0,-0.5f},{0.5f,0,-0.5f},{0.5f,0,0.5f},{-0.5f,0,0.5f});
        constexpr int segments=16;
        auto& cylinder=meshes[static_cast<std::size_t>(PrimitiveMeshType::Cylinder)];
        for(int i=0;i<segments;++i){
            const float a0=6.28318530718f*float(i)/segments, a1=6.28318530718f*float(i+1)/segments;
            const float x0=std::cos(a0)*0.5f,z0=std::sin(a0)*0.5f,x1=std::cos(a1)*0.5f,z1=std::sin(a1)*0.5f;
            append_quad(cylinder,{x0,-0.5f,z0},{x1,-0.5f,z1},{x1,0.5f,z1},{x0,0.5f,z0});
            auto base=static_cast<std::uint32_t>(cylinder.vertices.size());
            append_vertex(cylinder,0,0.5f,0,0.5f,0.5f); append_vertex(cylinder,x0,0.5f,z0,0,0); append_vertex(cylinder,x1,0.5f,z1,1,0);
            cylinder.indices.insert(cylinder.indices.end(),{base,base+1,base+2});
            base=static_cast<std::uint32_t>(cylinder.vertices.size());
            append_vertex(cylinder,0,-0.5f,0,0.5f,0.5f); append_vertex(cylinder,x1,-0.5f,z1,1,1); append_vertex(cylinder,x0,-0.5f,z0,0,1);
            cylinder.indices.insert(cylinder.indices.end(),{base,base+1,base+2});
        }
        auto& sphere=meshes[static_cast<std::size_t>(PrimitiveMeshType::Sphere)];
        constexpr int stacks=10, slices=16;
        for(int y=0;y<=stacks;++y){
            const float v=float(y)/stacks, phi=v*3.14159265359f;
            for(int x=0;x<=slices;++x){
                const float u=float(x)/slices, theta=u*6.28318530718f;
                const float sp=std::sin(phi);
                append_vertex(sphere,std::cos(theta)*sp*0.5f,std::cos(phi)*0.5f,std::sin(theta)*sp*0.5f,u,1.0f-v);
            }
        }
        for(int y=0;y<stacks;++y) for(int x=0;x<slices;++x){
            const std::uint32_t a=y*(slices+1)+x,b=a+slices+1;
            sphere.indices.insert(sphere.indices.end(),{a,b,a+1,a+1,b,b+1});
        }
        auto create_upload_buffer = [&](const void* source, UINT64 size_bytes, ComPtr<ID3D12Resource>& resource, std::string_view label) {
            D3D12_HEAP_PROPERTIES heap{}; heap.Type=D3D12_HEAP_TYPE_UPLOAD; heap.CreationNodeMask=1; heap.VisibleNodeMask=1;
            D3D12_RESOURCE_DESC desc{}; desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; desc.Width=size_bytes; desc.Height=1;
            desc.DepthOrArraySize=1; desc.MipLevels=1; desc.SampleDesc.Count=1; desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            check_hr(device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&resource)),std::format("Create {}",label));
            void* mapped=nullptr; D3D12_RANGE read_range{0,0}; check_hr(resource->Map(0,&read_range,&mapped),std::format("Map {}",label));
            std::memcpy(mapped,source,static_cast<std::size_t>(size_bytes)); resource->Unmap(0,nullptr);
        };
        for(std::size_t i=0;i<meshes.size();++i){
            const auto& mesh=meshes[i]; if(mesh.vertices.empty()||mesh.indices.empty()) continue;
            const UINT64 vb=static_cast<UINT64>(mesh.vertices.size()*sizeof(SectorMeshVertex));
            create_upload_buffer(mesh.vertices.data(),vb,primitive_vertex_buffers_[i],"primitive vertex buffer");
            primitive_vertex_views_[i].BufferLocation=primitive_vertex_buffers_[i]->GetGPUVirtualAddress(); primitive_vertex_views_[i].SizeInBytes=static_cast<UINT>(vb); primitive_vertex_views_[i].StrideInBytes=sizeof(SectorMeshVertex);
            const UINT64 ib=static_cast<UINT64>(mesh.indices.size()*sizeof(std::uint32_t));
            create_upload_buffer(mesh.indices.data(),ib,primitive_index_buffers_[i],"primitive index buffer");
            primitive_index_views_[i].BufferLocation=primitive_index_buffers_[i]->GetGPUVirtualAddress(); primitive_index_views_[i].SizeInBytes=static_cast<UINT>(ib); primitive_index_views_[i].Format=DXGI_FORMAT_R32_UINT;
            primitive_index_counts_[i]=static_cast<UINT>(mesh.indices.size());
        }
    }

    void create_geometry_buffers(const SectorMesh& mesh) {
        geometry_vertex_buffer_.Reset();
        geometry_index_buffer_.Reset();
        geometry_vertex_view_ = {};
        geometry_index_view_ = {};
        world_index_count_ = 0;

        if (mesh.vertices.empty() || mesh.indices.empty()) {
            return;
        }

        auto create_upload_buffer = [&](const void* source, UINT64 size_bytes, ComPtr<ID3D12Resource>& resource, std::string_view label) {
            D3D12_HEAP_PROPERTIES heap{};
            heap.Type = D3D12_HEAP_TYPE_UPLOAD;
            heap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
            heap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
            heap.CreationNodeMask = 1;
            heap.VisibleNodeMask = 1;

            D3D12_RESOURCE_DESC desc{};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            desc.Alignment = 0;
            desc.Width = size_bytes;
            desc.Height = 1;
            desc.DepthOrArraySize = 1;
            desc.MipLevels = 1;
            desc.Format = DXGI_FORMAT_UNKNOWN;
            desc.SampleDesc.Count = 1;
            desc.SampleDesc.Quality = 0;
            desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            desc.Flags = D3D12_RESOURCE_FLAG_NONE;

            check_hr(
                device_->CreateCommittedResource(
                    &heap,
                    D3D12_HEAP_FLAG_NONE,
                    &desc,
                    D3D12_RESOURCE_STATE_GENERIC_READ,
                    nullptr,
                    IID_PPV_ARGS(&resource)
                ),
                std::format("Create {}", label)
            );

            void* mapped = nullptr;
            D3D12_RANGE read_range{0, 0};
            check_hr(resource->Map(0, &read_range, &mapped), std::format("Map {}", label));
            std::memcpy(mapped, source, static_cast<std::size_t>(size_bytes));
            resource->Unmap(0, nullptr);
        };

        const UINT64 vertex_bytes = static_cast<UINT64>(mesh.vertices.size() * sizeof(SectorMeshVertex));
        create_upload_buffer(mesh.vertices.data(), vertex_bytes, geometry_vertex_buffer_, "world vertex buffer");
        geometry_vertex_view_.BufferLocation = geometry_vertex_buffer_->GetGPUVirtualAddress();
        geometry_vertex_view_.SizeInBytes = static_cast<UINT>(vertex_bytes);
        geometry_vertex_view_.StrideInBytes = sizeof(SectorMeshVertex);

        const UINT64 index_bytes = static_cast<UINT64>(mesh.indices.size() * sizeof(std::uint32_t));
        create_upload_buffer(mesh.indices.data(), index_bytes, geometry_index_buffer_, "world index buffer");
        geometry_index_view_.BufferLocation = geometry_index_buffer_->GetGPUVirtualAddress();
        geometry_index_view_.SizeInBytes = static_cast<UINT>(index_bytes);
        geometry_index_view_.Format = DXGI_FORMAT_R32_UINT;
        world_index_count_ = static_cast<UINT>(mesh.indices.size());
    }

    void create_sync_objects() {
        check_hr(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)), "CreateFence");
        fence_values_.fill(0);
        next_fence_value_ = 1;
        fence_event_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (!fence_event_) {
            throw std::runtime_error("CreateEvent failed for D3D12 fence synchronization.");
        }
    }

    void wait_for_gpu() {
        if (!command_queue_ || !fence_ || !fence_event_) {
            return;
        }

        const UINT64 value = next_fence_value_++;
        check_hr(command_queue_->Signal(fence_.Get(), value), "Signal fence");
        if (fence_->GetCompletedValue() < value) {
            check_hr(fence_->SetEventOnCompletion(value, fence_event_), "SetEventOnCompletion");
            WaitForSingleObject(fence_event_, INFINITE);
        }
    }

    void move_to_next_frame() {
        const UINT current_frame = frame_index_;
        const UINT64 signal_value = next_fence_value_++;
        fence_values_[current_frame] = signal_value;
        check_hr(command_queue_->Signal(fence_.Get(), signal_value), "Signal frame fence");

        frame_index_ = swap_chain_->GetCurrentBackBufferIndex();
        const UINT64 wait_value = fence_values_[frame_index_];
        if (wait_value != 0 && fence_->GetCompletedValue() < wait_value) {
            check_hr(fence_->SetEventOnCompletion(wait_value, fence_event_), "Wait for next frame");
            WaitForSingleObject(fence_event_, INFINITE);
        }
    }

    [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE current_rtv() const {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>(frame_index_) * rtv_descriptor_size_;
        return handle;
    }

    [[nodiscard]] DXGI_SWAP_CHAIN_DESC get_swap_chain_desc() const {
        DXGI_SWAP_CHAIN_DESC desc{};
        check_hr(swap_chain_->GetDesc(&desc), "IDXGISwapChain::GetDesc");
        return desc;
    }

private:
    SDL_Window* window_ = nullptr;
    HWND hwnd_ = nullptr;

    ComPtr<IDXGIFactory6> factory_;
    ComPtr<IDXGIAdapter1> adapter_;
    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12CommandQueue> command_queue_;
    ComPtr<IDXGISwapChain3> swap_chain_;
    ComPtr<ID3D12DescriptorHeap> rtv_heap_;
    ComPtr<ID3D12DescriptorHeap> dsv_heap_;
    ComPtr<ID3D12DescriptorHeap> srv_heap_;
    ComPtr<ID3D12DescriptorHeap> ui_srv_heap_;
    std::array<ComPtr<ID3D12Resource>, kFrameCount> render_targets_;
    ComPtr<ID3D12Resource> depth_target_;
    ComPtr<ID3D12Resource> texture_array_;
    std::array<ComPtr<ID3D12Resource>, kFrameCount> lighting_buffers_{};
    std::array<std::uint8_t*, kFrameCount> lighting_mapped_{};
    std::size_t lighting_view_cursor_ = 0;
    std::array<ComPtr<ID3D12CommandAllocator>, kFrameCount> command_allocators_;
    ComPtr<ID3D12GraphicsCommandList> command_list_;
    ComPtr<ID3D12RootSignature> root_signature_;
    ComPtr<ID3D12PipelineState> pipeline_state_;
    ComPtr<ID3D12PipelineState> primitive_instanced_pipeline_state_;
    ComPtr<ID3D12RootSignature> ui_root_signature_;
    ComPtr<ID3D12PipelineState> ui_pipeline_state_;
    std::array<ComPtr<ID3D12Resource>, kFrameCount> ui_vertex_buffers_{};
    std::array<std::uint8_t*, kFrameCount> ui_vertex_mapped_{};
    std::size_t ui_vertex_capacity_ = 0;
    ComPtr<ID3D12Resource> ui_atlas_texture_;
    ComPtr<ID3D12Resource> ui_atlas_upload_;
    const TextureData* ui_atlas_source_ = nullptr;
    std::uint64_t ui_atlas_revision_ = 0;
    std::uint32_t ui_atlas_width_ = 0;
    std::uint32_t ui_atlas_height_ = 0;
    std::size_t ui_draws_this_frame_ = 0;
    std::size_t ui_vertex_cursor_ = 0;
    std::array<ComPtr<ID3D12Resource>, 4> primitive_vertex_buffers_{};
    std::array<ComPtr<ID3D12Resource>, 4> primitive_index_buffers_{};
    std::array<D3D12_VERTEX_BUFFER_VIEW, 4> primitive_vertex_views_{};
    std::array<D3D12_INDEX_BUFFER_VIEW, 4> primitive_index_views_{};
    std::array<UINT, 4> primitive_index_counts_{};
    std::array<ComPtr<ID3D12Resource>, kFrameCount> primitive_instance_buffers_{};
    std::array<std::uint8_t*, kFrameCount> primitive_instance_mapped_{};
    std::size_t primitive_instance_capacity_ = 0;
    std::size_t primitive_instance_cursor_ = 0;
    std::array<std::vector<PrimitiveInstanceData>, 4> primitive_instance_scratch_{};
    std::vector<PrimitiveInstanceData> sprite_instance_scratch_{};

    ComPtr<ID3D12Resource> geometry_vertex_buffer_;
    ComPtr<ID3D12Resource> geometry_index_buffer_;
    D3D12_VERTEX_BUFFER_VIEW geometry_vertex_view_{};
    D3D12_INDEX_BUFFER_VIEW geometry_index_view_{};
    ComPtr<ID3D12Resource> sprite_vertex_buffer_;
    ComPtr<ID3D12Resource> sprite_index_buffer_;
    D3D12_VERTEX_BUFFER_VIEW sprite_vertex_view_{};
    D3D12_INDEX_BUFFER_VIEW sprite_index_view_{};
    ComPtr<ID3D12Fence> fence_;

    HANDLE fence_event_ = nullptr;
    std::array<UINT64, kFrameCount> fence_values_{};
    UINT64 next_fence_value_ = 1;
    UINT frame_index_ = 0;
    UINT rtv_descriptor_size_ = 0;
    UINT width_ = 0;
    UINT height_ = 0;
    UINT world_index_count_ = 0;
    UINT sprite_index_count_ = 0;
    std::uint64_t cached_world_revision_ = 0;
    RendererCapabilities capabilities_{};
    RenderFrameStats frame_stats_{};
    bool vsync_enabled_ = true;
    bool tearing_supported_ = false;
    bool frame_open_ = false;
};

} // namespace

D3D12NativeAccess* d3d12_native_access(RenderBackend* backend) {
    return dynamic_cast<D3D12NativeAccess*>(backend);
}

const D3D12NativeAccess* d3d12_native_access(const RenderBackend* backend) {
    return dynamic_cast<const D3D12NativeAccess*>(backend);
}

std::unique_ptr<RenderBackend> create_default_render_backend() {
    return std::make_unique<D3D12Renderer>();
}

} // namespace vespera
