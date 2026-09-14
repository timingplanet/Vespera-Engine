#include <vespera/ui/rmlui_surface.hpp>

#include <vespera/assets/texture_importer.hpp>
#include <vespera/core/log.hpp>
#include <vespera/input/input.hpp>
#include <vespera/render/texture_data.hpp>

#include <RmlUi/Core.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/EventListener.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/RenderInterfaceCompatibility.h>
#include <RmlUi/Core/Vertex.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace vespera {
namespace {

constexpr std::uint32_t kMaxAtlasDimension = 8192u;

std::filesystem::path path_from_utf8(std::string_view value) {
#ifdef _WIN32
    std::u8string utf8;
    utf8.reserve(value.size());
    for (unsigned char c : value) utf8.push_back(static_cast<char8_t>(c));
    return std::filesystem::path(utf8);
#else
    return std::filesystem::path(std::string(value));
#endif
}

std::string rml_color(const std::array<float, 4>& color) {
    const auto channel = [](float value) {
        return static_cast<int>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
    };
    return std::format("#{:02X}{:02X}{:02X}{:02X}",
        channel(color[0]), channel(color[1]), channel(color[2]), channel(color[3]));
}

bool parse_float_strict(std::string_view text, float& value) {
    const char* first = text.data();
    const char* last = first + text.size();
    const auto result = std::from_chars(first, last, value);
    return result.ec == std::errc{} && result.ptr == last;
}

struct CapturedDraw {
    std::vector<Rml::Vertex> vertices;
    std::vector<int> indices;
    Rml::TextureHandle texture = 0;
    Rml::Vector2f translation{};
    bool scissor_enabled = false;
    int scissor_x = 0;
    int scissor_y = 0;
    int scissor_w = 0;
    int scissor_h = 0;
};

struct AtlasPlacement {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct CapturedTexture {
    TextureData pixels;
    AtlasPlacement placement{};
};

class CaptureRenderInterface final : public Rml::RenderInterfaceCompatibility {
public:
    void begin_capture(int width, int height) {
        viewport_width_ = std::max(width, 1);
        viewport_height_ = std::max(height, 1);
        draws_.clear();
        warnings_.clear();
        scissor_enabled_ = false;
        scissor_x_ = 0;
        scissor_y_ = 0;
        scissor_w_ = viewport_width_;
        scissor_h_ = viewport_height_;
    }

    [[nodiscard]] const std::vector<CapturedDraw>& draws() const { return draws_; }
    [[nodiscard]] const TextureData& atlas() const { return atlas_; }
    [[nodiscard]] std::uint64_t atlas_revision() const { return atlas_revision_; }
    [[nodiscard]] const std::vector<std::string>& warnings() const { return warnings_; }

    void RenderGeometry(
        Rml::Vertex* vertices,
        int num_vertices,
        int* indices,
        int num_indices,
        Rml::TextureHandle texture,
        const Rml::Vector2f& translation
    ) override {
        if (!vertices || !indices || num_vertices <= 0 || num_indices <= 0) return;
        CapturedDraw draw;
        draw.vertices.assign(vertices, vertices + num_vertices);
        draw.indices.assign(indices, indices + num_indices);
        draw.texture = texture;
        draw.translation = translation;
        draw.scissor_enabled = scissor_enabled_;
        draw.scissor_x = scissor_x_;
        draw.scissor_y = scissor_y_;
        draw.scissor_w = scissor_w_;
        draw.scissor_h = scissor_h_;
        draws_.push_back(std::move(draw));
    }

    void EnableScissorRegion(bool enable) override { scissor_enabled_ = enable; }

    void SetScissorRegion(int x, int y, int width, int height) override {
        scissor_x_ = x;
        scissor_y_ = y;
        scissor_w_ = std::max(width, 0);
        scissor_h_ = std::max(height, 0);
    }

    bool LoadTexture(
        Rml::TextureHandle& texture_handle,
        Rml::Vector2i& texture_dimensions,
        const Rml::String& source
    ) override {
        const std::filesystem::path path = path_from_utf8(source);
        TextureImportResult imported = import_texture(path, path.filename().string());
        if (!imported) {
            warnings_.push_back(std::format("RmlUi image '{}' could not be loaded: {}", source, imported.message));
            return false;
        }
        const auto handle = allocate_texture(std::move(imported.texture));
        const auto& stored = textures_.at(handle).pixels;
        texture_dimensions = Rml::Vector2i(static_cast<int>(stored.width), static_cast<int>(stored.height));
        texture_handle = handle;
        return true;
    }

    bool GenerateTexture(
        Rml::TextureHandle& texture_handle,
        const Rml::byte* source,
        const Rml::Vector2i& source_dimensions
    ) override {
        if (!source || source_dimensions.x <= 0 || source_dimensions.y <= 0) return false;
        const std::size_t pixel_count = static_cast<std::size_t>(source_dimensions.x)
            * static_cast<std::size_t>(source_dimensions.y);
        if (pixel_count > (std::numeric_limits<std::size_t>::max)() / 4u) return false;

        TextureData texture;
        texture.name = "RmlUi generated texture";
        texture.width = static_cast<std::uint32_t>(source_dimensions.x);
        texture.height = static_cast<std::uint32_t>(source_dimensions.y);
        texture.rgba8.assign(source, source + pixel_count * 4u);
        // RenderInterfaceCompatibility already converts RmlUi 6.x premultiplied
        // generated texture data back to the straight-alpha format expected by
        // Vespera's existing UI blend state.
        texture_handle = allocate_texture(std::move(texture));
        return true;
    }

    void ReleaseTexture(Rml::TextureHandle texture) override {
        if (textures_.erase(texture) != 0u) atlas_dirty_ = true;
    }

    void SetTransform(const Rml::Matrix4f* transform) override {
        if (transform && !warned_transform_) {
            warnings_.push_back(
                "RmlUi compatibility renderer received a transform. Advanced GPU transforms are not yet implemented by the Vespera packet adapter."
            );
            warned_transform_ = true;
        }
    }

    bool rebuild_atlas_if_needed() {
        if (!atlas_dirty_ && atlas_.valid()) return true;

        std::vector<std::pair<Rml::TextureHandle, CapturedTexture*>> items;
        items.reserve(textures_.size());
        std::uint32_t widest = 2;
        std::uint64_t total_area = 4;
        for (auto& [handle, texture] : textures_) {
            if (!texture.pixels.valid()) continue;
            items.emplace_back(handle, &texture);
            widest = std::max(widest, texture.pixels.width);
            total_area += static_cast<std::uint64_t>(texture.pixels.width + 2u)
                * static_cast<std::uint64_t>(texture.pixels.height + 2u);
        }

        if (widest > kMaxAtlasDimension) {
            warnings_.push_back(std::format("RmlUi texture width {} exceeds the Vespera compatibility atlas limit {}.", widest, kMaxAtlasDimension));
            return false;
        }

        std::uint32_t atlas_width = 1024u;
        while (atlas_width < widest + 2u && atlas_width < kMaxAtlasDimension) atlas_width *= 2u;
        while (static_cast<std::uint64_t>(atlas_width) * static_cast<std::uint64_t>(atlas_width) < total_area
            && atlas_width < 4096u) {
            atlas_width *= 2u;
        }
        atlas_width = std::min(atlas_width, kMaxAtlasDimension);

        std::sort(items.begin(), items.end(), [](const auto& lhs, const auto& rhs) {
            return lhs.second->pixels.height > rhs.second->pixels.height;
        });

        int cursor_x = 2;
        int cursor_y = 2;
        int row_height = 0;
        int required_height = 4;
        for (auto& [_, texture] : items) {
            const int width = static_cast<int>(texture->pixels.width);
            const int height = static_cast<int>(texture->pixels.height);
            if (cursor_x + width + 2 > static_cast<int>(atlas_width)) {
                cursor_x = 2;
                cursor_y += row_height + 2;
                row_height = 0;
            }
            if (cursor_y + height + 2 > static_cast<int>(kMaxAtlasDimension)) {
                warnings_.push_back("RmlUi generated textures exceed the 8192px Vespera compatibility atlas height.");
                return false;
            }
            texture->placement = {cursor_x, cursor_y, width, height};
            cursor_x += width + 2;
            row_height = std::max(row_height, height);
            required_height = std::max(required_height, cursor_y + height + 2);
        }

        std::uint32_t atlas_height = 4u;
        while (atlas_height < static_cast<std::uint32_t>(required_height) && atlas_height < kMaxAtlasDimension) atlas_height *= 2u;
        atlas_height = std::min(atlas_height, kMaxAtlasDimension);

        atlas_.name = "Vespera RmlUi compatibility atlas";
        atlas_.width = atlas_width;
        atlas_.height = atlas_height;
        atlas_.rgba8.assign(static_cast<std::size_t>(atlas_width) * atlas_height * 4u, 0u);

        // White texel for untextured geometry.
        for (int y = 0; y < 2; ++y) {
            for (int x = 0; x < 2; ++x) {
                const std::size_t p = (static_cast<std::size_t>(y) * atlas_width + static_cast<std::size_t>(x)) * 4u;
                atlas_.rgba8[p + 0] = 255u;
                atlas_.rgba8[p + 1] = 255u;
                atlas_.rgba8[p + 2] = 255u;
                atlas_.rgba8[p + 3] = 255u;
            }
        }

        for (auto& [_, texture] : items) {
            const auto& src = texture->pixels;
            const auto& placement = texture->placement;
            for (int row = 0; row < placement.height; ++row) {
                const std::size_t src_offset = static_cast<std::size_t>(row) * src.width * 4u;
                const std::size_t dst_offset = (
                    static_cast<std::size_t>(placement.y + row) * atlas_width
                    + static_cast<std::size_t>(placement.x)
                ) * 4u;
                std::memcpy(atlas_.rgba8.data() + dst_offset, src.rgba8.data() + src_offset, static_cast<std::size_t>(placement.width) * 4u);
            }
        }

        atlas_dirty_ = false;
        ++atlas_revision_;
        return true;
    }

    [[nodiscard]] std::optional<AtlasPlacement> placement(Rml::TextureHandle texture) const {
        if (texture == 0) return AtlasPlacement{0, 0, 2, 2};
        const auto it = textures_.find(texture);
        if (it == textures_.end()) return std::nullopt;
        return it->second.placement;
    }

private:
    Rml::TextureHandle allocate_texture(TextureData texture) {
        const Rml::TextureHandle handle = next_texture_handle_++;
        textures_.emplace(handle, CapturedTexture{std::move(texture), {}});
        atlas_dirty_ = true;
        return handle;
    }

    int viewport_width_ = 1;
    int viewport_height_ = 1;
    bool scissor_enabled_ = false;
    int scissor_x_ = 0;
    int scissor_y_ = 0;
    int scissor_w_ = 1;
    int scissor_h_ = 1;
    bool warned_transform_ = false;
    Rml::TextureHandle next_texture_handle_ = 1;
    std::unordered_map<Rml::TextureHandle, CapturedTexture> textures_;
    TextureData atlas_;
    bool atlas_dirty_ = true;
    std::uint64_t atlas_revision_ = 1;
    std::vector<CapturedDraw> draws_;
    std::vector<std::string> warnings_;
};

struct ClipVertex {
    float x = 0;
    float y = 0;
    float u = 0;
    float v = 0;
    float r = 1;
    float g = 1;
    float b = 1;
    float a = 1;
};

enum class ClipEdge { Left, Right, Top, Bottom };

float component_for_edge(const ClipVertex& v, ClipEdge edge) {
    return (edge == ClipEdge::Left || edge == ClipEdge::Right) ? v.x : v.y;
}

bool inside_edge(const ClipVertex& v, ClipEdge edge, float bound) {
    switch (edge) {
        case ClipEdge::Left: return v.x >= bound;
        case ClipEdge::Right: return v.x <= bound;
        case ClipEdge::Top: return v.y >= bound;
        case ClipEdge::Bottom: return v.y <= bound;
    }
    return true;
}

ClipVertex lerp_vertex(const ClipVertex& a, const ClipVertex& b, float t) {
    const auto mix = [t](float x, float y) { return x + (y - x) * t; };
    return {
        mix(a.x, b.x), mix(a.y, b.y), mix(a.u, b.u), mix(a.v, b.v),
        mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b), mix(a.a, b.a)
    };
}

std::vector<ClipVertex> clip_polygon_edge(
    const std::vector<ClipVertex>& input,
    ClipEdge edge,
    float bound
) {
    std::vector<ClipVertex> output;
    if (input.empty()) return output;
    output.reserve(input.size() + 2u);
    ClipVertex previous = input.back();
    bool previous_inside = inside_edge(previous, edge, bound);

    for (const ClipVertex& current : input) {
        const bool current_inside = inside_edge(current, edge, bound);
        if (current_inside != previous_inside) {
            const float previous_component = component_for_edge(previous, edge);
            const float current_component = component_for_edge(current, edge);
            const float denominator = current_component - previous_component;
            const float t = std::abs(denominator) > 1e-6f ? (bound - previous_component) / denominator : 0.0f;
            output.push_back(lerp_vertex(previous, current, std::clamp(t, 0.0f, 1.0f)));
        }
        if (current_inside) output.push_back(current);
        previous = current;
        previous_inside = current_inside;
    }
    return output;
}

std::vector<ClipVertex> clip_triangle(
    std::array<ClipVertex, 3> triangle,
    int x,
    int y,
    int width,
    int height
) {
    std::vector<ClipVertex> polygon(triangle.begin(), triangle.end());
    const float left = static_cast<float>(x);
    const float top = static_cast<float>(y);
    const float right = static_cast<float>(x + width);
    const float bottom = static_cast<float>(y + height);
    polygon = clip_polygon_edge(polygon, ClipEdge::Left, left);
    polygon = clip_polygon_edge(polygon, ClipEdge::Right, right);
    polygon = clip_polygon_edge(polygon, ClipEdge::Top, top);
    polygon = clip_polygon_edge(polygon, ClipEdge::Bottom, bottom);
    return polygon;
}

std::uint8_t to_byte(float value) {
    return static_cast<std::uint8_t>(std::clamp(std::lround(value * 255.0f), 0l, 255l));
}

std::uint32_t pack_rgba(const ClipVertex& vertex) {
    const std::uint32_t r = to_byte(vertex.r);
    const std::uint32_t g = to_byte(vertex.g);
    const std::uint32_t b = to_byte(vertex.b);
    const std::uint32_t a = to_byte(vertex.a);
    return r | (g << 8u) | (b << 16u) | (a << 24u);
}

class ClickListener final : public Rml::EventListener {
public:
    explicit ClickListener(std::unordered_map<std::string, int>& clicks) : clicks_(clicks) {}

    void ProcessEvent(Rml::Event& event) override {
        if (Rml::Element* target = event.GetTargetElement()) {
            if (!target->GetId().empty()) ++clicks_[target->GetId()];
        }
    }

private:
    std::unordered_map<std::string, int>& clicks_;
};

Rml::Input::KeyIdentifier map_key(Key key) {
    using namespace Rml::Input;
    switch (key) {
        case Key::W: return KI_W;
        case Key::A: return KI_A;
        case Key::S: return KI_S;
        case Key::D: return KI_D;
        case Key::LeftShift: return KI_LSHIFT;
        case Key::Escape: return KI_ESCAPE;
        case Key::Space: return KI_SPACE;
        case Key::Enter: return KI_RETURN;
        case Key::Tab: return KI_TAB;
        case Key::Up: return KI_UP;
        case Key::Down: return KI_DOWN;
        case Key::Left: return KI_LEFT;
        case Key::Right: return KI_RIGHT;
        case Key::L: return KI_L;
        case Key::Backspace: return KI_BACK;
        case Key::Count: break;
    }
    return KI_UNKNOWN;
}

int modifier_state(const InputSystem& input) {
    int modifiers = 0;
    if (input.down(Key::LeftShift)) modifiers |= Rml::Input::KM_SHIFT;
    return modifiers;
}

std::string escape_rml_text(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (char c : value) {
        switch (c) {
            case '&': result += "&amp;"; break;
            case '<': result += "&lt;"; break;
            case '>': result += "&gt;"; break;
            case '\n': result += "<br/>"; break;
            case '\r': break;
            default: result.push_back(c); break;
        }
    }
    return result;
}

std::vector<std::filesystem::path> default_font_candidates() {
    std::vector<std::filesystem::path> result;
#ifdef _WIN32
    result.emplace_back("C:/Windows/Fonts/segoeui.ttf");
    result.emplace_back("C:/Windows/Fonts/arial.ttf");
#else
    result.emplace_back("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
    result.emplace_back("/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf");
#endif
    return result;
}

struct CoreOwner {
    std::mutex mutex;
    int active_surfaces = 0;
    CaptureRenderInterface* render_interface = nullptr;
};

CoreOwner& core_owner() {
    static CoreOwner owner;
    return owner;
}

} // namespace

class RmlUiSurface::Impl {
public:
    Result initialize(
        const std::filesystem::path& document_path,
        int viewport_width,
        int viewport_height,
        const std::vector<std::filesystem::path>& font_paths
    ) {
        if (context_) return {false, "RmlUi surface is already initialized."};
        viewport_width_ = std::max(viewport_width, 1);
        viewport_height_ = std::max(viewport_height, 1);
        document_path_ = std::filesystem::absolute(document_path).lexically_normal();
        if (!std::filesystem::exists(document_path_)) {
            return {false, std::format("RmlUi document does not exist: {}", document_path_.string())};
        }

        auto& owner = core_owner();
        {
            std::scoped_lock lock(owner.mutex);
            if (owner.active_surfaces != 0) {
                return {false, "Vespera 0.9.9 currently supports one active RmlUi surface per process."};
            }
            Rml::SetRenderInterface(render_interface_.GetAdaptedInterface());
            if (!Rml::Initialise()) {
                Rml::SetRenderInterface(nullptr);
                return {false, "RmlUi::Initialise failed."};
            }
            owner.active_surfaces = 1;
            owner.render_interface = &render_interface_;
            owns_core_ = true;
        }

        const std::string context_name = std::format("vespera-rmlui-{}", reinterpret_cast<std::uintptr_t>(this));
        context_name_ = context_name;
        context_ = Rml::CreateContext(context_name_, Rml::Vector2i(viewport_width_, viewport_height_));
        if (!context_) {
            shutdown();
            return {false, "RmlUi::CreateContext failed."};
        }
        context_->EnableMouseCursor(false);

        std::vector<std::filesystem::path> candidates = font_paths;
        if (candidates.empty()) candidates = default_font_candidates();
        bool font_loaded = false;
        for (const auto& font : candidates) {
            if (!std::filesystem::exists(font)) continue;
            if (Rml::LoadFontFace(font.string())) {
                font_loaded = true;
                loaded_fonts_.push_back(font);
                break;
            }
        }
        if (!font_loaded) {
            warnings_.push_back("No RmlUi font could be loaded. Pass a project/system TTF/OTF path to RmlUiSurface::initialize.");
        }

        document_ = context_->LoadDocument(document_path_.string());
        if (!document_) {
            shutdown();
            return {false, std::format("RmlUi could not load document: {}", document_path_.string())};
        }
        click_listener_ = std::make_unique<ClickListener>(clicks_);
        document_->AddEventListener("click", click_listener_.get(), false);
        document_->Show(Rml::ModalFlag::None, Rml::FocusFlag::Auto);
        context_->Update();

        log::info(std::format(
            "Vespera RmlUi surface ready: {} | {}x{} | compatibility packet renderer",
            document_path_.filename().string(), viewport_width_, viewport_height_));
        return {true, "Vespera RmlUi surface initialized."};
    }

    void shutdown() {
        if (document_ && click_listener_) {
            document_->RemoveEventListener("click", click_listener_.get(), false);
        }
        click_listener_.reset();
        document_ = nullptr;
        if (context_) {
            Rml::RemoveContext(context_name_);
            context_ = nullptr;
        }
        clicks_.clear();
        generic_disabled_.clear();
        loaded_fonts_.clear();
        warnings_.clear();

        if (owns_core_) {
            auto& owner = core_owner();
            std::scoped_lock lock(owner.mutex);
            Rml::Shutdown();
            Rml::SetRenderInterface(nullptr);
            owner.active_surfaces = 0;
            owner.render_interface = nullptr;
            owns_core_ = false;
        }
    }

    bool reload() {
        if (!context_ || document_path_.empty()) return false;
        if (document_ && click_listener_) document_->RemoveEventListener("click", click_listener_.get(), false);
        if (document_) {
            context_->UnloadDocument(document_);
            document_ = nullptr;
            context_->Update();
        }
        clicks_.clear();
        generic_disabled_.clear();
        document_ = context_->LoadDocument(document_path_.string());
        if (!document_) {
            warnings_.push_back(std::format("RmlUi reload failed for {}", document_path_.string()));
            return false;
        }
        document_->AddEventListener("click", click_listener_.get(), false);
        document_->Show(Rml::ModalFlag::None, Rml::FocusFlag::Auto);
        context_->Update();
        return true;
    }

    void resize(int width, int height) {
        viewport_width_ = std::max(width, 1);
        viewport_height_ = std::max(height, 1);
        if (context_) context_->SetDimensions(Rml::Vector2i(viewport_width_, viewport_height_));
    }

    void process_input(const InputSystem& input) {
        if (!context_) return;
        const int modifiers = modifier_state(input);
        context_->ProcessMouseMove(
            static_cast<int>(std::lround(input.mouse_x())),
            static_cast<int>(std::lround(input.mouse_y())),
            modifiers);

        const std::array<std::pair<MouseButton, int>, 3> mouse_buttons{{
            {MouseButton::Left, 0}, {MouseButton::Right, 1}, {MouseButton::Middle, 2}
        }};
        for (const auto& [button, index] : mouse_buttons) {
            if (input.mouse_pressed(button)) context_->ProcessMouseButtonDown(index, modifiers);
            if (input.mouse_released(button)) context_->ProcessMouseButtonUp(index, modifiers);
        }

        constexpr std::array<Key, 15> keys{{
            Key::W, Key::A, Key::S, Key::D, Key::LeftShift, Key::Escape, Key::Space,
            Key::Enter, Key::Tab, Key::Up, Key::Down, Key::Left, Key::Right, Key::L, Key::Backspace
        }};
        for (Key key : keys) {
            const Rml::Input::KeyIdentifier identifier = map_key(key);
            if (input.pressed(key)) context_->ProcessKeyDown(identifier, modifiers);
            for (std::uint16_t repeat = 0; repeat < input.repeat_count(key); ++repeat) {
                context_->ProcessKeyDown(identifier, modifiers);
            }
            if (input.released(key)) context_->ProcessKeyUp(identifier, modifiers);
        }
        if (!input.text_input().empty()) context_->ProcessTextInput(std::string(input.text_input()));
    }

    UiRenderPacket build_packet() {
        UiRenderPacket packet;
        packet.viewport_width = viewport_width_;
        packet.viewport_height = viewport_height_;
        if (!context_) {
            packet.warnings.push_back("RmlUi surface is not initialized.");
            return packet;
        }

        render_interface_.begin_capture(viewport_width_, viewport_height_);
        context_->Update();
        context_->Render();
        if (!render_interface_.rebuild_atlas_if_needed()) {
            packet.warnings.insert(packet.warnings.end(), render_interface_.warnings().begin(), render_interface_.warnings().end());
            return packet;
        }

        const TextureData& atlas = render_interface_.atlas();
        packet.atlas = &atlas;
        packet.atlas_revision = render_interface_.atlas_revision();

        for (const CapturedDraw& draw : render_interface_.draws()) {
            const auto placement_opt = render_interface_.placement(draw.texture);
            if (!placement_opt) {
                packet.warnings.push_back(std::format("RmlUi draw referenced missing texture handle {}.", draw.texture));
                continue;
            }
            const AtlasPlacement placement = *placement_opt;
            const float atlas_w = static_cast<float>(atlas.width);
            const float atlas_h = static_cast<float>(atlas.height);

            const auto convert = [&](const Rml::Vertex& source) {
                ClipVertex vertex;
                vertex.x = source.position.x + draw.translation.x;
                vertex.y = source.position.y + draw.translation.y;
                vertex.u = (static_cast<float>(placement.x) + source.tex_coord.x * static_cast<float>(placement.width)) / atlas_w;
                vertex.v = (static_cast<float>(placement.y) + source.tex_coord.y * static_cast<float>(placement.height)) / atlas_h;
                vertex.r = static_cast<float>(source.colour.red) / 255.0f;
                vertex.g = static_cast<float>(source.colour.green) / 255.0f;
                vertex.b = static_cast<float>(source.colour.blue) / 255.0f;
                vertex.a = static_cast<float>(source.colour.alpha) / 255.0f;
                return vertex;
            };

            for (std::size_t i = 0; i + 2u < draw.indices.size(); i += 3u) {
                const int ia = draw.indices[i + 0u];
                const int ib = draw.indices[i + 1u];
                const int ic = draw.indices[i + 2u];
                if (ia < 0 || ib < 0 || ic < 0
                    || static_cast<std::size_t>(ia) >= draw.vertices.size()
                    || static_cast<std::size_t>(ib) >= draw.vertices.size()
                    || static_cast<std::size_t>(ic) >= draw.vertices.size()) {
                    continue;
                }

                std::vector<ClipVertex> polygon;
                const std::array<ClipVertex, 3> triangle{{
                    convert(draw.vertices[static_cast<std::size_t>(ia)]),
                    convert(draw.vertices[static_cast<std::size_t>(ib)]),
                    convert(draw.vertices[static_cast<std::size_t>(ic)])
                }};
                if (draw.scissor_enabled) {
                    const int sx = std::clamp(draw.scissor_x, 0, viewport_width_);
                    const int sy = std::clamp(draw.scissor_y, 0, viewport_height_);
                    const int sw = std::clamp(draw.scissor_w, 0, viewport_width_ - sx);
                    const int sh = std::clamp(draw.scissor_h, 0, viewport_height_ - sy);
                    if (sw <= 0 || sh <= 0) continue;
                    polygon = clip_triangle(triangle, sx, sy, sw, sh);
                } else {
                    polygon.assign(triangle.begin(), triangle.end());
                }
                if (polygon.size() < 3u) continue;

                for (std::size_t fan = 1; fan + 1u < polygon.size(); ++fan) {
                    const std::array<ClipVertex, 3> output{{polygon[0], polygon[fan], polygon[fan + 1u]}};
                    for (const ClipVertex& vertex : output) {
                        packet.vertices.push_back({vertex.x, vertex.y, vertex.u, vertex.v, pack_rgba(vertex)});
                    }
                }
            }
        }

        packet.warnings.insert(packet.warnings.end(), warnings_.begin(), warnings_.end());
        packet.warnings.insert(packet.warnings.end(), render_interface_.warnings().begin(), render_interface_.warnings().end());
        return packet;
    }

    bool exists(std::string_view id) const { return find(id) != nullptr; }

    bool set_text(std::string_view id, std::string_view value) {
        if (Rml::Element* element = find(id)) {
            element->SetInnerRML(escape_rml_text(value));
            return true;
        }
        return false;
    }

    std::string text(std::string_view id) const {
        if (Rml::Element* element = find(id)) return element->GetInnerRML();
        return {};
    }

    bool set_value(std::string_view id, std::string_view new_value) {
        if (Rml::Element* element = find(id)) {
            if (auto* control = dynamic_cast<Rml::ElementFormControl*>(element)) {
                control->SetValue(std::string(new_value));
                return true;
            }
            if (element->GetTagName() == "progress") {
                element->SetAttribute("value", std::string(new_value));
                return true;
            }
        }
        return false;
    }

    std::string value(std::string_view id) const {
        if (Rml::Element* element = find(id)) {
            if (auto* control = dynamic_cast<Rml::ElementFormControl*>(element)) return control->GetValue();
            if (element->GetTagName() == "progress") return element->GetAttribute<Rml::String>("value", "0");
        }
        return {};
    }

    bool set_property(std::string_view id, std::string_view property, std::string_view value) {
        if (Rml::Element* element = find(id)) return element->SetProperty(std::string(property), std::string(value));
        return false;
    }

    bool set_class(std::string_view id, std::string_view class_name, bool enabled) {
        if (Rml::Element* element = find(id)) {
            element->SetClass(std::string(class_name), enabled);
            return true;
        }
        return false;
    }

    bool set_disabled(std::string_view id, bool disabled_value) {
        if (Rml::Element* element = find(id)) {
            if (auto* control = dynamic_cast<Rml::ElementFormControl*>(element)) {
                control->SetDisabled(disabled_value);
                return true;
            }
            const std::string key(id);
            if (disabled_value) {
                if (!element->SetProperty("pointer-events", "none")) return false;
                generic_disabled_.insert(key);
                element->SetClass("vespera-disabled", true);
            } else {
                // Restore authored RCSS instead of forcing a local `auto` override.
                element->RemoveProperty("pointer-events");
                generic_disabled_.erase(key);
                element->SetClass("vespera-disabled", false);
            }
            return true;
        }
        return false;
    }

    bool disabled(std::string_view id, bool& disabled_value) const {
        if (Rml::Element* element = find(id)) {
            if (auto* control = dynamic_cast<Rml::ElementFormControl*>(element)) {
                disabled_value = control->IsDisabled();
                return true;
            }
            disabled_value = generic_disabled_.contains(std::string(id));
            return true;
        }
        return false;
    }

    bool set_visible(std::string_view id, bool visible_value) {
        if (Rml::Element* element = find(id)) {
            if (visible_value) {
                // Restore the document/stylesheet-authored display mode instead of
                // forcing `block` and accidentally destroying flex/inline layout.
                element->RemoveProperty("display");
                return true;
            }
            return element->SetProperty("display", "none");
        }
        return false;
    }

    bool visible(std::string_view id, bool& visible_value) const {
        if (Rml::Element* element = find(id)) {
            visible_value = element->IsVisible(false);
            return true;
        }
        return false;
    }

    bool set_interactable(std::string_view id, bool interactable_value) {
        return set_disabled(id, !interactable_value);
    }

    bool interactable(std::string_view id, bool& interactable_value) const {
        bool disabled_value = false;
        if (!disabled(id, disabled_value)) return false;
        interactable_value = !disabled_value;
        return true;
    }

    bool set_read_only(std::string_view, bool) {
        // RmlUi 6.2 does not expose an HTML-style readonly semantic for its text
        // controls. Do not silently substitute disabled, which changes focus and
        // pseudo-class behavior.
        return false;
    }

    bool read_only(std::string_view, bool&) const { return false; }

    bool set_numeric_value(std::string_view id, float numeric_value) {
        return set_value(id, std::format("{}", numeric_value));
    }

    bool numeric_value(std::string_view id, float& numeric_value) const {
        const std::string text_value = value(id);
        return !text_value.empty() && parse_float_strict(text_value, numeric_value);
    }

    bool set_color(std::string_view id, UiColorSlot slot, const std::array<float, 4>& color) {
        if (slot == UiColorSlot::Visual) return set_property(id, "background-color", rml_color(color));
        if (slot == UiColorSlot::Text) return set_property(id, "color", rml_color(color));
        // Progress fill/background are separate generated elements in RmlUi and
        // should be authored with classes/properties rather than guessed here.
        return false;
    }

    bool set_asset(std::string_view id, UiAssetSlot slot, const AssetReference&, std::string_view resolved_path) {
        Rml::Element* element = find(id);
        if (!element || resolved_path.empty()) return false;
        if (slot == UiAssetSlot::Image && element->GetTagName() == "img") {
            element->SetAttribute("src", std::string(resolved_path));
            return true;
        }
        // Runtime font switching needs a loaded face/family mapping. Do not
        // pretend a filesystem path is an RCSS font-family name.
        return false;
    }

    bool focus(std::string_view id, bool focus_visible) {
        bool disabled_value = false;
        if (disabled(id, disabled_value) && disabled_value) return false;
        if (Rml::Element* element = find(id)) return element->Focus(focus_visible);
        return false;
    }

    bool blur(std::string_view id) {
        if (Rml::Element* element = find(id)) {
            element->Blur();
            return true;
        }
        return false;
    }

    bool click(std::string_view id) {
        bool disabled_value = false;
        if (disabled(id, disabled_value) && disabled_value) return false;
        if (Rml::Element* element = find(id)) {
            element->Click();
            return true;
        }
        return false;
    }

    int consume_clicks(std::string_view id) {
        const auto it = clicks_.find(std::string(id));
        if (it == clicks_.end()) return 0;
        const int count = it->second;
        clicks_.erase(it);
        return count;
    }

    std::string hovered_id() const {
        if (!context_) return {};
        if (Rml::Element* element = context_->GetHoverElement()) return element->GetId();
        return {};
    }

    std::string focused_id() const {
        if (!context_) return {};
        if (Rml::Element* element = context_->GetFocusElement()) return element->GetId();
        return {};
    }

    bool initialized() const { return context_ != nullptr && document_ != nullptr; }
    const std::vector<std::string>& warnings() const { return warnings_; }
    const std::filesystem::path& document_path() const { return document_path_; }

private:
    Rml::Element* find(std::string_view id) const {
        if (!document_) return nullptr;
        return document_->GetElementById(std::string(id));
    }

    CaptureRenderInterface render_interface_;
    Rml::Context* context_ = nullptr;
    Rml::ElementDocument* document_ = nullptr;
    std::unique_ptr<ClickListener> click_listener_;
    std::unordered_map<std::string, int> clicks_;
    std::unordered_set<std::string> generic_disabled_;
    std::filesystem::path document_path_;
    std::vector<std::filesystem::path> loaded_fonts_;
    std::vector<std::string> warnings_;
    std::string context_name_;
    int viewport_width_ = 1;
    int viewport_height_ = 1;
    bool owns_core_ = false;
};

RmlUiSurface::RmlUiSurface() : impl_(std::make_unique<Impl>()) {}
RmlUiSurface::~RmlUiSurface() { shutdown(); }
RmlUiSurface::RmlUiSurface(RmlUiSurface&&) noexcept = default;
RmlUiSurface& RmlUiSurface::operator=(RmlUiSurface&&) noexcept = default;

RmlUiSurface::Result RmlUiSurface::initialize(
    const std::filesystem::path& document_path,
    int viewport_width,
    int viewport_height,
    const std::vector<std::filesystem::path>& font_paths
) {
    return impl_->initialize(document_path, viewport_width, viewport_height, font_paths);
}

void RmlUiSurface::shutdown() { if (impl_) impl_->shutdown(); }
bool RmlUiSurface::initialized() const { return impl_ && impl_->initialized(); }
bool RmlUiSurface::reload() { return impl_ && impl_->reload(); }
void RmlUiSurface::resize(int width, int height) { impl_->resize(width, height); }
void RmlUiSurface::process_input(const InputSystem& input) { impl_->process_input(input); }
UiRenderPacket RmlUiSurface::build_packet() { return impl_->build_packet(); }
bool RmlUiSurface::exists(std::string_view id) const { return impl_ && impl_->exists(id); }
bool RmlUiSurface::set_text(std::string_view id, std::string_view text) { return impl_->set_text(id, text); }
std::string RmlUiSurface::text(std::string_view id) const { return impl_->text(id); }
bool RmlUiSurface::set_value(std::string_view id, std::string_view value) { return impl_ && impl_->set_value(id, value); }
std::string RmlUiSurface::value(std::string_view id) const { return impl_ ? impl_->value(id) : std::string{}; }
bool RmlUiSurface::set_property(std::string_view id, std::string_view property, std::string_view value) { return impl_ && impl_->set_property(id, property, value); }
bool RmlUiSurface::set_class(std::string_view id, std::string_view class_name, bool enabled) { return impl_ && impl_->set_class(id, class_name, enabled); }
bool RmlUiSurface::set_disabled(std::string_view id, bool disabled_value) { return impl_ && impl_->set_disabled(id, disabled_value); }
bool RmlUiSurface::disabled(std::string_view id, bool& disabled_value) const { return impl_ && impl_->disabled(id, disabled_value); }
bool RmlUiSurface::set_visible(std::string_view id, bool visible_value) { return impl_ && impl_->set_visible(id, visible_value); }
bool RmlUiSurface::visible(std::string_view id, bool& visible_value) const { return impl_ && impl_->visible(id, visible_value); }
bool RmlUiSurface::set_interactable(std::string_view id, bool interactable_value) { return impl_ && impl_->set_interactable(id, interactable_value); }
bool RmlUiSurface::interactable(std::string_view id, bool& interactable_value) const { return impl_ && impl_->interactable(id, interactable_value); }
bool RmlUiSurface::set_read_only(std::string_view id, bool read_only_value) { return impl_ && impl_->set_read_only(id, read_only_value); }
bool RmlUiSurface::read_only(std::string_view id, bool& read_only_value) const { return impl_ && impl_->read_only(id, read_only_value); }
bool RmlUiSurface::set_numeric_value(std::string_view id, float numeric_value) { return impl_ && impl_->set_numeric_value(id, numeric_value); }
bool RmlUiSurface::numeric_value(std::string_view id, float& numeric_value) const { return impl_ && impl_->numeric_value(id, numeric_value); }
bool RmlUiSurface::set_color(std::string_view id, UiColorSlot slot, const std::array<float, 4>& color) { return impl_ && impl_->set_color(id, slot, color); }
bool RmlUiSurface::set_asset(std::string_view id, UiAssetSlot slot, const AssetReference& reference, std::string_view resolved_path) { return impl_ && impl_->set_asset(id, slot, reference, resolved_path); }
bool RmlUiSurface::focus(std::string_view id, bool visible) { return impl_ && impl_->focus(id, visible); }
bool RmlUiSurface::blur(std::string_view id) { return impl_ && impl_->blur(id); }
bool RmlUiSurface::click(std::string_view id) { return impl_ && impl_->click(id); }
int RmlUiSurface::consume_clicks(std::string_view id) { return impl_->consume_clicks(id); }
std::string RmlUiSurface::hovered_id() const { return impl_->hovered_id(); }
std::string RmlUiSurface::focused_id() const { return impl_->focused_id(); }
const std::vector<std::string>& RmlUiSurface::warnings() const { return impl_->warnings(); }
std::string RmlUiSurface::renderer_mode() const { return "RmlUi 6.2 -> Vespera UiRenderPacket"; }
std::filesystem::path RmlUiSurface::document_path() const { return impl_ ? impl_->document_path() : std::filesystem::path{}; }

} // namespace vespera
