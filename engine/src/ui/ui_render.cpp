#include <vespera/ui/ui_render.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <format>
#include <fstream>
#include <limits>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace vespera {
namespace {

constexpr int kAtlasMaxSize = 4096;
constexpr float kNativeFontRasterSize = 48.0f;
constexpr float kPi = 3.14159265358979323846f;

struct PendingRegion {
    std::string key;
    TextureData texture;
    bool glyph = false;
    float advance = 0.0f;
    float offset_x = 0.0f;
    float offset_y = 0.0f;
    float line_height = 0.0f;
};

struct FaceRequest {
    std::string key;
    std::string family;
    std::uint16_t weight = 400;
    bool italic = false;
    std::optional<std::filesystem::path> private_path;
    std::set<std::uint32_t> codepoints;
};

std::string asset_key(const AssetReference& reference) {
    if (!reference.asset_id.empty()) return "id:" + reference.asset_id;
    if (!reference.path.empty()) return "path:" + reference.path.generic_string();
    return {};
}

std::string font_face_key(const UiTextProperties& text) {
    std::string key = asset_key(text.font);
    if (key.empty()) key = "system:" + (text.font_family.empty() ? std::string("Segoe UI") : text.font_family);
    return std::format("{}|w{}|i{}", key, std::clamp<int>(text.font_weight, 100, 900), text.italic ? 1 : 0);
}

std::string glyph_key(std::string_view face_key, std::uint32_t cp) {
    return std::format("{}|u{}", face_key, cp);
}

std::uint32_t pack_color(const std::array<float, 4>& color, float opacity = 1.0f) {
    const auto to_byte = [](float value) -> std::uint32_t {
        return static_cast<std::uint32_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
    };
    const std::uint32_t r = to_byte(color[0]);
    const std::uint32_t g = to_byte(color[1]);
    const std::uint32_t b = to_byte(color[2]);
    const std::uint32_t a = to_byte(color[3] * std::clamp(opacity, 0.0f, 1.0f));
    return r | (g << 8u) | (b << 16u) | (a << 24u);
}

std::array<float, 4> multiply_color(const std::array<float, 4>& a, const std::array<float, 4>& b) {
    return {a[0] * b[0], a[1] * b[1], a[2] * b[2], a[3] * b[3]};
}

bool intersect_rect(const UiRect& a, const UiRect& b, UiRect& out) {
    const float x0 = std::max(a.x, b.x);
    const float y0 = std::max(a.y, b.y);
    const float x1 = std::min(a.x + a.width, b.x + b.width);
    const float y1 = std::min(a.y + a.height, b.y + b.height);
    if (x1 <= x0 || y1 <= y0) return false;
    out = {x0, y0, x1 - x0, y1 - y0};
    return true;
}

bool same_rect(const UiRect& a, const UiRect& b) {
    return std::abs(a.x-b.x)<0.01f && std::abs(a.y-b.y)<0.01f
        && std::abs(a.width-b.width)<0.01f && std::abs(a.height-b.height)<0.01f;
}

void append_triangle(std::vector<UiDrawVertex>& vertices,
                     float x0,float y0,float x1,float y1,float x2,float y2,
                     std::uint32_t color) {
    vertices.push_back({x0,y0,0.0f,0.0f,color});
    vertices.push_back({x1,y1,0.0f,0.0f,color});
    vertices.push_back({x2,y2,0.0f,0.0f,color});
}

void append_quad(
    std::vector<UiDrawVertex>& vertices,
    const UiRect& rect,
    std::uint32_t color,
    float u0,
    float v0,
    float u1,
    float v1,
    const UiRect* clip = nullptr
) {
    if (rect.width <= 0.0f || rect.height <= 0.0f) return;
    UiRect draw = rect;
    float cu0 = u0, cv0 = v0, cu1 = u1, cv1 = v1;
    if (clip) {
        UiRect clipped;
        if (!intersect_rect(rect, *clip, clipped)) return;
        const float inv_w = rect.width > 0.0f ? 1.0f / rect.width : 0.0f;
        const float inv_h = rect.height > 0.0f ? 1.0f / rect.height : 0.0f;
        const float tx0 = (clipped.x - rect.x) * inv_w;
        const float ty0 = (clipped.y - rect.y) * inv_h;
        const float tx1 = (clipped.x + clipped.width - rect.x) * inv_w;
        const float ty1 = (clipped.y + clipped.height - rect.y) * inv_h;
        cu0 = u0 + (u1 - u0) * tx0;
        cv0 = v0 + (v1 - v0) * ty0;
        cu1 = u0 + (u1 - u0) * tx1;
        cv1 = v0 + (v1 - v0) * ty1;
        draw = clipped;
    }

    const float x0 = draw.x;
    const float y0 = draw.y;
    const float x1 = draw.x + draw.width;
    const float y1 = draw.y + draw.height;
    const std::array<UiDrawVertex, 6> quad{{
        {x0, y0, cu0, cv0, color}, {x1, y0, cu1, cv0, color}, {x1, y1, cu1, cv1, color},
        {x0, y0, cu0, cv0, color}, {x1, y1, cu1, cv1, color}, {x0, y1, cu0, cv1, color},
    }};
    vertices.insert(vertices.end(), quad.begin(), quad.end());
}

void append_rounded_rect(std::vector<UiDrawVertex>& vertices, const UiRect& rect,
                         float radius, std::uint32_t color, const UiRect* clip=nullptr) {
    if (rect.width <= 0 || rect.height <= 0) return;
    radius = std::clamp(radius, 0.0f, std::min(rect.width, rect.height)*0.5f);
    if (radius < 0.75f) {
        append_quad(vertices, rect, color, 0,0,0,0, clip);
        return;
    }
    if (clip) {
        UiRect clipped;
        if (!intersect_rect(rect,*clip,clipped)) return;
        if (!same_rect(rect,clipped)) {
            // Deterministic rectangular clipping is preferred to leaking rounded
            // geometry outside a ScrollView. Rounded mask clipping comes later.
            append_quad(vertices, rect, color, 0,0,0,0, clip);
            return;
        }
    }

    constexpr int segments = 7;
    std::vector<UiVec2> perimeter;
    perimeter.reserve(segments*4+4);
    const std::array<UiVec2,4> centers{{
        {rect.x+radius, rect.y+radius},
        {rect.x+rect.width-radius, rect.y+radius},
        {rect.x+rect.width-radius, rect.y+rect.height-radius},
        {rect.x+radius, rect.y+rect.height-radius},
    }};
    const std::array<float,4> starts{{kPi, 1.5f*kPi, 0.0f, 0.5f*kPi}};
    for (int corner=0; corner<4; ++corner) {
        for (int i=0;i<=segments;++i) {
            const float a=starts[corner]+(0.5f*kPi)*(static_cast<float>(i)/segments);
            perimeter.push_back({centers[corner].x+std::cos(a)*radius,
                                 centers[corner].y+std::sin(a)*radius});
        }
    }
    const float cx=rect.x+rect.width*0.5f, cy=rect.y+rect.height*0.5f;
    for (std::size_t i=0;i<perimeter.size();++i) {
        const auto& a=perimeter[i];
        const auto& b=perimeter[(i+1)%perimeter.size()];
        append_triangle(vertices,cx,cy,a.x,a.y,b.x,b.y,color);
    }
}

void append_textured_rounded_rect(std::vector<UiDrawVertex>& vertices, const UiRect& rect,
                                  float radius, std::uint32_t color,
                                  float u0,float v0,float u1,float v1,
                                  const UiRect* clip=nullptr) {
    if (rect.width <= 0 || rect.height <= 0) return;
    radius = std::clamp(radius, 0.0f, std::min(rect.width, rect.height)*0.5f);
    if (radius < 0.75f) { append_quad(vertices,rect,color,u0,v0,u1,v1,clip); return; }
    if (clip) {
        UiRect clipped; if(!intersect_rect(rect,*clip,clipped)) return;
        if(!same_rect(rect,clipped)){ append_quad(vertices,rect,color,u0,v0,u1,v1,clip); return; }
    }
    constexpr int segments=7;
    std::vector<UiVec2> perimeter; perimeter.reserve(segments*4+4);
    const std::array<UiVec2,4> centers{{
        {rect.x+radius,rect.y+radius},{rect.x+rect.width-radius,rect.y+radius},
        {rect.x+rect.width-radius,rect.y+rect.height-radius},{rect.x+radius,rect.y+rect.height-radius}}};
    const std::array<float,4> starts{{kPi,1.5f*kPi,0.0f,0.5f*kPi}};
    for(int corner=0;corner<4;++corner) for(int i=0;i<=segments;++i){const float a=starts[corner]+0.5f*kPi*(static_cast<float>(i)/segments);perimeter.push_back({centers[corner].x+std::cos(a)*radius,centers[corner].y+std::sin(a)*radius});}
    const float cx=rect.x+rect.width*0.5f,cy=rect.y+rect.height*0.5f;
    auto make_vertex=[&](float x,float y){const float tx=(x-rect.x)/rect.width,ty=(y-rect.y)/rect.height;return UiDrawVertex{x,y,u0+(u1-u0)*tx,v0+(v1-v0)*ty,color};};
    const auto center=make_vertex(cx,cy);
    for(std::size_t i=0;i<perimeter.size();++i){const auto a=make_vertex(perimeter[i].x,perimeter[i].y);const auto b=make_vertex(perimeter[(i+1)%perimeter.size()].x,perimeter[(i+1)%perimeter.size()].y);vertices.push_back(center);vertices.push_back(a);vertices.push_back(b);}
}

UiRect inset_rect(UiRect r, float inset) {
    inset = std::max(0.0f,inset);
    r.x += inset; r.y += inset;
    r.width = std::max(0.0f, r.width-inset*2.0f);
    r.height = std::max(0.0f, r.height-inset*2.0f);
    return r;
}

void append_shadow(std::vector<UiDrawVertex>& vertices, const UiRect& rect,
                   const UiSurfaceProperties& surface, const UiRect* clip) {
    if (surface.shadow_color[3] <= 0.001f) return;
    const float softness=std::clamp(surface.shadow_softness,0.0f,32.0f);
    const int layers=softness <= 0.5f ? 1 : std::clamp(static_cast<int>(std::ceil(softness/2.0f)),2,10);
    for (int i=layers-1;i>=0;--i) {
        const float t=layers==1 ? 0.0f : static_cast<float>(i)/static_cast<float>(layers-1);
        const float spread=softness*t;
        UiRect sr{rect.x+surface.shadow_offset.x-spread,
                  rect.y+surface.shadow_offset.y-spread,
                  rect.width+spread*2.0f,rect.height+spread*2.0f};
        auto c=surface.shadow_color;
        c[3] *= layers==1 ? 1.0f : (0.16f + (1.0f-t)*0.18f);
        append_rounded_rect(vertices,sr,surface.corner_radius+spread,pack_color(c,surface.opacity),clip);
    }
}

void append_surface_fill(std::vector<UiDrawVertex>& vertices, const UiRect& rect,
                         const UiSurfaceProperties& surface, const std::array<float,4>& fill,
                         const UiRect* clip) {
    append_shadow(vertices,rect,surface,clip);
    const float border=std::clamp(surface.border_width,0.0f,std::min(rect.width,rect.height)*0.5f);
    if (border > 0.01f && surface.border_color[3] > 0.001f) {
        append_rounded_rect(vertices,rect,surface.corner_radius,pack_color(surface.border_color,surface.opacity),clip);
        const UiRect inner=inset_rect(rect,border);
        append_rounded_rect(vertices,inner,std::max(0.0f,surface.corner_radius-border),pack_color(fill,surface.opacity),clip);
    } else {
        append_rounded_rect(vertices,rect,surface.corner_radius,pack_color(fill,surface.opacity),clip);
    }
}

UiRect fit_contain(const UiRect& rect, float source_w, float source_h) {
    if (source_w <= 0 || source_h <= 0 || rect.width <= 0 || rect.height <= 0) return rect;
    const float scale=std::min(rect.width/source_w,rect.height/source_h);
    const float w=source_w*scale,h=source_h*scale;
    return {rect.x+(rect.width-w)*0.5f,rect.y+(rect.height-h)*0.5f,w,h};
}

void append_image(std::vector<UiDrawVertex>& vertices, const UiRect& rect,
                  const UiRenderCache::AtlasUv& uv, const UiSurfaceProperties& surface,
                  std::uint32_t color, const UiRect* clip) {
    if (uv.pixel_width==0 || uv.pixel_height==0) return;
    const bool nine = surface.nine_slice.left>0 || surface.nine_slice.top>0
        || surface.nine_slice.right>0 || surface.nine_slice.bottom>0;
    if (nine) {
        const float sw=static_cast<float>(uv.pixel_width), sh=static_cast<float>(uv.pixel_height);
        float l=std::clamp(surface.nine_slice.left,0.0f,sw*0.5f);
        float r=std::clamp(surface.nine_slice.right,0.0f,sw*0.5f);
        float t=std::clamp(surface.nine_slice.top,0.0f,sh*0.5f);
        float b=std::clamp(surface.nine_slice.bottom,0.0f,sh*0.5f);
        const float dst_scale=std::min(1.0f,std::min(rect.width/std::max(1.0f,l+r),rect.height/std::max(1.0f,t+b)));
        const float dl=l*dst_scale,dr=r*dst_scale,dt=t*dst_scale,db=b*dst_scale;
        const float xs[4]{rect.x,rect.x+dl,rect.x+rect.width-dr,rect.x+rect.width};
        const float ys[4]{rect.y,rect.y+dt,rect.y+rect.height-db,rect.y+rect.height};
        const float us[4]{uv.u0,uv.u0+(uv.u1-uv.u0)*(l/sw),uv.u1-(uv.u1-uv.u0)*(r/sw),uv.u1};
        const float vs[4]{uv.v0,uv.v0+(uv.v1-uv.v0)*(t/sh),uv.v1-(uv.v1-uv.v0)*(b/sh),uv.v1};
        for(int y=0;y<3;++y) for(int x=0;x<3;++x)
            append_quad(vertices,{xs[x],ys[y],xs[x+1]-xs[x],ys[y+1]-ys[y]},color,us[x],vs[y],us[x+1],vs[y+1],clip);
        return;
    }
    if (surface.image_fit == UiImageFit::Contain) {
        const UiRect draw=fit_contain(rect,static_cast<float>(uv.pixel_width),static_cast<float>(uv.pixel_height));
        append_textured_rounded_rect(vertices,draw,surface.corner_radius,color,uv.u0,uv.v0,uv.u1,uv.v1,clip);
        return;
    }
    if (surface.image_fit == UiImageFit::Cover) {
        const float src_aspect=static_cast<float>(uv.pixel_width)/std::max(1.0f,static_cast<float>(uv.pixel_height));
        const float dst_aspect=rect.width/std::max(1.0f,rect.height);
        float u0=uv.u0,v0=uv.v0,u1=uv.u1,v1=uv.v1;
        if (dst_aspect > src_aspect) {
            const float visible=src_aspect/dst_aspect;
            const float trim=(1.0f-visible)*0.5f*(uv.v1-uv.v0);
            v0+=trim;v1-=trim;
        } else {
            const float visible=dst_aspect/src_aspect;
            const float trim=(1.0f-visible)*0.5f*(uv.u1-uv.u0);
            u0+=trim;u1-=trim;
        }
        append_textured_rounded_rect(vertices,rect,surface.corner_radius,color,u0,v0,u1,v1,clip);
        return;
    }
    append_textured_rounded_rect(vertices,rect,surface.corner_radius,color,uv.u0,uv.v0,uv.u1,uv.v1,clip);
}

// Deterministic fallback bitmap glyphs used on non-Windows backends and when a
// platform font could not be resolved. Production Windows UI normally uses the
// native antialiased glyph atlas below.
std::array<std::uint8_t, 7> glyph_rows(char input) {
    unsigned char raw = static_cast<unsigned char>(input);
    char c = static_cast<char>(std::toupper(raw));
    switch (c) {
        case 'A': return {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}; case 'B': return {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E};
        case 'C': return {0x0F,0x10,0x10,0x10,0x10,0x10,0x0F}; case 'D': return {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E};
        case 'E': return {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}; case 'F': return {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10};
        case 'G': return {0x0F,0x10,0x10,0x17,0x11,0x11,0x0F}; case 'H': return {0x11,0x11,0x11,0x1F,0x11,0x11,0x11};
        case 'I': return {0x1F,0x04,0x04,0x04,0x04,0x04,0x1F}; case 'J': return {0x01,0x01,0x01,0x01,0x11,0x11,0x0E};
        case 'K': return {0x11,0x12,0x14,0x18,0x14,0x12,0x11}; case 'L': return {0x10,0x10,0x10,0x10,0x10,0x10,0x1F};
        case 'M': return {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}; case 'N': return {0x11,0x19,0x15,0x13,0x11,0x11,0x11};
        case 'O': return {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}; case 'P': return {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10};
        case 'Q': return {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}; case 'R': return {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11};
        case 'S': return {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}; case 'T': return {0x1F,0x04,0x04,0x04,0x04,0x04,0x04};
        case 'U': return {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}; case 'V': return {0x11,0x11,0x11,0x11,0x11,0x0A,0x04};
        case 'W': return {0x11,0x11,0x11,0x15,0x15,0x1B,0x11}; case 'X': return {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11};
        case 'Y': return {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}; case 'Z': return {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F};
        case '0': return {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}; case '1': return {0x04,0x0C,0x14,0x04,0x04,0x04,0x1F};
        case '2': return {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}; case '3': return {0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E};
        case '4': return {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}; case '5': return {0x1F,0x10,0x10,0x1E,0x01,0x01,0x1E};
        case '6': return {0x0E,0x10,0x10,0x1E,0x11,0x11,0x0E}; case '7': return {0x1F,0x01,0x02,0x04,0x08,0x08,0x08};
        case '8': return {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}; case '9': return {0x0E,0x11,0x11,0x0F,0x01,0x01,0x0E};
        case '.': return {0,0,0,0,0,0x0C,0x0C}; case ',': return {0,0,0,0,0,0x0C,0x08}; case ':': return {0,0x0C,0x0C,0,0x0C,0x0C,0};
        case '-': return {0,0,0,0x1F,0,0,0}; case '_': return {0,0,0,0,0,0,0x1F}; case '/': return {0x01,0x02,0x02,0x04,0x08,0x08,0x10};
        case '!': return {0x04,0x04,0x04,0x04,0x04,0,0x04}; case '?': return {0x0E,0x11,0x01,0x02,0x04,0,0x04};
        case '+': return {0,0x04,0x04,0x1F,0x04,0x04,0}; case '=': return {0,0,0x1F,0,0x1F,0,0};
        case '(': return {0x02,0x04,0x08,0x08,0x08,0x04,0x02}; case ')': return {0x08,0x04,0x02,0x02,0x02,0x04,0x08};
        case '[': return {0x0E,0x08,0x08,0x08,0x08,0x08,0x0E}; case ']': return {0x0E,0x02,0x02,0x02,0x02,0x02,0x0E};
        case '#': return {0x0A,0x1F,0x0A,0x0A,0x1F,0x0A,0x0A}; case '%': return {0x19,0x19,0x02,0x04,0x08,0x13,0x13};
        case ' ': return {}; default: return {0x1F,0x11,0x05,0x04,0x04,0,0x04};
    }
}

std::u32string decode_utf8(std::string_view input) {
    std::u32string out;
    for (std::size_t i=0;i<input.size();) {
        const auto c=static_cast<unsigned char>(input[i]);
        std::uint32_t cp=0; std::size_t n=1;
        if (c < 0x80) cp=c;
        else if ((c&0xE0)==0xC0 && i+1<input.size()) { cp=c&0x1F; n=2; }
        else if ((c&0xF0)==0xE0 && i+2<input.size()) { cp=c&0x0F; n=3; }
        else if ((c&0xF8)==0xF0 && i+3<input.size()) { cp=c&0x07; n=4; }
        else { out.push_back(U'\uFFFD'); ++i; continue; }
        bool valid=true;
        for(std::size_t j=1;j<n;++j){ const auto cc=static_cast<unsigned char>(input[i+j]); if((cc&0xC0)!=0x80){valid=false;break;} cp=(cp<<6)|(cc&0x3F); }
        if(!valid){out.push_back(U'\uFFFD');++i;continue;}
        out.push_back(static_cast<char32_t>(cp)); i+=n;
    }
    return out;
}

float fallback_advance(float font_size) { return std::max(font_size,1.0f)*(6.0f/7.0f); }
float fallback_line_height(float font_size) { return std::max(font_size,1.0f)*(8.0f/7.0f); }

void append_fallback_text(std::vector<UiDrawVertex>& vertices,const UiRect& rect,std::string_view text,
                          float font_size,std::uint32_t color,UiHorizontalAlignment horizontal,
                          UiVerticalAlignment vertical,const UiRect* clip=nullptr) {
    if(text.empty()||rect.width<=0||rect.height<=0)return;
    const float pixel=std::max(font_size,1.0f)/7.0f,advance=fallback_advance(font_size);
    std::vector<std::string_view> lines; std::size_t start=0;
    while(start<=text.size()){auto end=text.find('\n',start);if(end==std::string_view::npos){lines.push_back(text.substr(start));break;}lines.push_back(text.substr(start,end-start));start=end+1;}
    if(lines.empty()) lines.push_back({});
    const float total_h=lines.size()*fallback_line_height(font_size); float y=rect.y;
    if(vertical==UiVerticalAlignment::Middle)y+=(rect.height-total_h)*0.5f; else if(vertical==UiVerticalAlignment::Bottom)y+=rect.height-total_h;
    for(auto line:lines){float x=rect.x;const float width=line.size()*advance;if(horizontal==UiHorizontalAlignment::Center)x+=(rect.width-width)*0.5f;else if(horizontal==UiHorizontalAlignment::Right)x+=rect.width-width;
        for(char c:line){const auto rows=glyph_rows(c);for(int row=0;row<7;++row)for(int col=0;col<5;++col){const std::uint8_t bit=static_cast<std::uint8_t>(1u<<(4-col));if((rows[row]&bit)==0)continue;append_quad(vertices,{x+col*pixel,y+row*pixel,std::max(pixel,1.0f),std::max(pixel,1.0f)},color,0,0,0,0,clip);}x+=advance;}y+=fallback_line_height(font_size);if(y>rect.y+rect.height)break;}
}

float glyph_advance(const std::unordered_map<std::string,UiRenderCache::FontGlyph>& glyphs,
                    std::string_view face,std::uint32_t cp,float font_size) {
    auto it=glyphs.find(glyph_key(face,cp));
    if(it==glyphs.end()) it=glyphs.find(glyph_key(face,U'?'));
    if(it==glyphs.end()) return fallback_advance(font_size);
    return it->second.advance*(font_size/std::max(1.0f,it->second.raster_size));
}

float measure_line(const std::unordered_map<std::string,UiRenderCache::FontGlyph>& glyphs,
                   std::string_view face,const std::u32string& line,float font_size) {
    float w=0;for(auto cp:line)w+=glyph_advance(glyphs,face,static_cast<std::uint32_t>(cp),font_size);return w;
}

std::vector<std::u32string> wrap_text(const std::unordered_map<std::string,UiRenderCache::FontGlyph>& glyphs,
                                     std::string_view face,const std::u32string& text,float font_size,
                                     float max_width,bool wrap) {
    std::vector<std::u32string> lines; std::u32string current; std::size_t last_space=std::u32string::npos;
    auto flush=[&](){lines.push_back(current);current.clear();last_space=std::u32string::npos;};
    for(char32_t cp:text){
        if(cp==U'\r')continue;
        if(cp==U'\n'){flush();continue;}
        current.push_back(cp); if(cp==U' '||cp==U'\t')last_space=current.size()-1;
        if(wrap&&max_width>1.0f&&measure_line(glyphs,face,current,font_size)>max_width&&current.size()>1){
            if(last_space!=std::u32string::npos&&last_space>0){
                std::u32string remainder=current.substr(last_space+1); current.resize(last_space); flush(); current=std::move(remainder);
            }else{ char32_t tail=current.back();current.pop_back();flush();current.push_back(tail); }
        }
    }
    if(!current.empty()||lines.empty())lines.push_back(current);
    return lines;
}

void append_atlas_text(std::vector<UiDrawVertex>& vertices,const UiRect& rect,std::string_view utf8,
                       const UiTextProperties& text,float scale_factor,std::uint32_t color,
                       const std::unordered_map<std::string,UiRenderCache::FontGlyph>& glyphs,
                       const UiRect* clip=nullptr) {
    if(utf8.empty()||rect.width<=0||rect.height<=0)return;
    const float font_size=std::max(1.0f,text.font_size*scale_factor);
    const std::string face=font_face_key(text);
    if(glyphs.find(glyph_key(face,U'A'))==glyphs.end()){
        append_fallback_text(vertices,rect,utf8,font_size,color,text.horizontal_alignment,text.vertical_alignment,clip);return;
    }
    const auto cps=decode_utf8(utf8); const auto lines=wrap_text(glyphs,face,cps,font_size,rect.width,text.wrap);
    auto base=glyphs.find(glyph_key(face,U'M')); if(base==glyphs.end())base=glyphs.find(glyph_key(face,U'A'));
    const float raster=base==glyphs.end()?kNativeFontRasterSize:base->second.raster_size;
    const float base_line=base==glyphs.end()?font_size*1.2f:base->second.line_height*(font_size/raster);
    const float line_h=base_line*std::clamp(text.line_spacing,0.5f,4.0f);
    const float total_h=lines.size()*line_h;float y=rect.y;
    if(text.vertical_alignment==UiVerticalAlignment::Middle)y+=(rect.height-total_h)*0.5f;else if(text.vertical_alignment==UiVerticalAlignment::Bottom)y+=rect.height-total_h;
    for(const auto& line:lines){float x=rect.x;const float width=measure_line(glyphs,face,line,font_size);if(text.horizontal_alignment==UiHorizontalAlignment::Center)x+=(rect.width-width)*0.5f;else if(text.horizontal_alignment==UiHorizontalAlignment::Right)x+=rect.width-width;
        for(char32_t cp:line){auto it=glyphs.find(glyph_key(face,static_cast<std::uint32_t>(cp)));if(it==glyphs.end())it=glyphs.find(glyph_key(face,U'?'));if(it==glyphs.end()){x+=fallback_advance(font_size);continue;}const auto& g=it->second;const float gs=font_size/std::max(1.0f,g.raster_size);UiRect gr{x+g.offset_x*gs,y+g.offset_y*gs,g.uv.pixel_width*gs,g.uv.pixel_height*gs};append_quad(vertices,gr,color,g.uv.u0,g.uv.v0,g.uv.u1,g.uv.v1,clip);x+=g.advance*gs;}y+=line_h;if(y>rect.y+rect.height)break;}
}

void append_styled_text(std::vector<UiDrawVertex>& vertices,const UiRect& rect,std::string_view utf8,
                        const UiTextProperties& text,float scale_factor,
                        const std::unordered_map<std::string,UiRenderCache::FontGlyph>& glyphs,
                        const UiRect* clip=nullptr,float opacity=1.0f) {
    if(text.shadow_color[3]>0.001f&&(std::abs(text.shadow_offset.x)>0.01f||std::abs(text.shadow_offset.y)>0.01f)){
        UiRect sr=rect;sr.x+=text.shadow_offset.x*scale_factor;sr.y+=text.shadow_offset.y*scale_factor;
        append_atlas_text(vertices,sr,utf8,text,scale_factor,pack_color(text.shadow_color,opacity),glyphs,clip);
    }
    append_atlas_text(vertices,rect,utf8,text,scale_factor,pack_color(text.color,opacity),glyphs,clip);
}

#ifdef _WIN32
std::uint16_t be16(const std::uint8_t* p){return static_cast<std::uint16_t>((p[0]<<8u)|p[1]);}
std::uint32_t be32(const std::uint8_t* p){return (static_cast<std::uint32_t>(p[0])<<24u)|(static_cast<std::uint32_t>(p[1])<<16u)|(static_cast<std::uint32_t>(p[2])<<8u)|p[3];}

std::optional<std::wstring> sfnt_family_name(const std::filesystem::path& path){
    std::ifstream in(path,std::ios::binary);if(!in)return std::nullopt;std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(in)),{});if(data.size()<12)return std::nullopt;
    const auto tables=be16(data.data()+4);std::uint32_t noff=0,nlen=0;
    for(std::uint16_t i=0;i<tables;++i){const std::size_t o=12u+static_cast<std::size_t>(i)*16u;if(o+16>data.size())break;if(std::memcmp(data.data()+o,"name",4)==0){noff=be32(data.data()+o+8);nlen=be32(data.data()+o+12);break;}}
    if(!noff||noff+nlen>data.size()||nlen<6)return std::nullopt;const auto* t=data.data()+noff;const auto count=be16(t+2),strOff=be16(t+4);if(6u+count*12u>nlen)return std::nullopt;
    struct Candidate{int score=-1;std::wstring value;};Candidate best;
    for(std::uint16_t i=0;i<count;++i){const auto* r=t+6u+i*12u;const auto platform=be16(r),language=be16(r+4),nameId=be16(r+6),len=be16(r+8),off=be16(r+10);if(nameId!=16&&nameId!=1)continue;if(static_cast<std::uint32_t>(strOff)+off+len>nlen)continue;int score=(nameId==16?20:10)+(platform==3?8:0)+(language==0x0409?2:0);if(score<=best.score)continue;std::wstring w;const auto* bytes=t+strOff+off;if(platform==3){for(std::uint16_t j=0;j+1<len;j+=2)w.push_back(static_cast<wchar_t>(be16(bytes+j)));}else{for(std::uint16_t j=0;j<len;++j)w.push_back(static_cast<wchar_t>(bytes[j]));}if(!w.empty())best={score,std::move(w)};}
    if(best.score<0)return std::nullopt;return best.value;
}

std::wstring widen_utf8(std::string_view text){if(text.empty())return {};int n=MultiByteToWideChar(CP_UTF8,0,text.data(),static_cast<int>(text.size()),nullptr,0);if(n<=0)return {};std::wstring out(static_cast<std::size_t>(n),L'\0');MultiByteToWideChar(CP_UTF8,0,text.data(),static_cast<int>(text.size()),out.data(),n);return out;}

std::vector<PendingRegion> rasterize_face(const FaceRequest& face,std::vector<std::string>& warnings){
    std::vector<PendingRegion> out;HDC dc=CreateCompatibleDC(nullptr);if(!dc){warnings.push_back("UI font rasterizer could not create a GDI device context");return out;}
    const std::wstring family=widen_utf8(face.family.empty()?"Segoe UI":face.family);
    HFONT font=CreateFontW(-static_cast<int>(kNativeFontRasterSize),0,0,0,std::clamp<int>(face.weight,100,900),face.italic?TRUE:FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_TT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH|FF_DONTCARE,family.c_str());
    if(!font){DeleteDC(dc);warnings.push_back("UI font rasterizer could not create font family '"+face.family+"'");return out;}HGDIOBJ oldFont=SelectObject(dc,font);TEXTMETRICW tm{};GetTextMetricsW(dc,&tm);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(255,255,255));
    for(const auto cp:face.codepoints){if(cp>0xFFFFu)continue;wchar_t wc=static_cast<wchar_t>(cp);SIZE size{};if(!GetTextExtentPoint32W(dc,&wc,1,&size))continue;const int pad = 4;
        const int w = std::max<int>(8, static_cast<int>(size.cx) + pad * 2);
        const int h = std::max<int>(8, static_cast<int>(tm.tmHeight) + 2);BITMAPINFO bmi{};bmi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bmi.bmiHeader.biWidth=w;bmi.bmiHeader.biHeight=-h;bmi.bmiHeader.biPlanes=1;bmi.bmiHeader.biBitCount=32;bmi.bmiHeader.biCompression=BI_RGB;void* bits=nullptr;HBITMAP bmp=CreateDIBSection(dc,&bmi,DIB_RGB_COLORS,&bits,nullptr,0);if(!bmp||!bits){if(bmp)DeleteObject(bmp);continue;}HGDIOBJ oldBmp=SelectObject(dc,bmp);std::memset(bits,0,static_cast<std::size_t>(w)*h*4u);TextOutW(dc,pad,0,&wc,1);TextureData tex;tex.name=glyph_key(face.key,cp);tex.width=w;tex.height=h;tex.rgba8.resize(static_cast<std::size_t>(w)*h*4u);const auto* src=static_cast<const std::uint8_t*>(bits);for(std::size_t i=0;i<static_cast<std::size_t>(w)*h;++i){const std::uint8_t cov=std::max({src[i*4u+0],src[i*4u+1],src[i*4u+2]});tex.rgba8[i*4u+0]=255;tex.rgba8[i*4u+1]=255;tex.rgba8[i*4u+2]=255;tex.rgba8[i*4u+3]=cov;}SelectObject(dc,oldBmp);DeleteObject(bmp);PendingRegion region;region.key=tex.name;region.texture=std::move(tex);region.glyph=true;region.advance=static_cast<float>(size.cx);region.offset_x=-static_cast<float>(pad);region.offset_y=0;region.line_height=static_cast<float>(tm.tmHeight);out.push_back(std::move(region));}
    SelectObject(dc,oldFont);DeleteObject(font);DeleteDC(dc);return out;
}
void collect_text_codepoints(std::set<std::uint32_t>& set,std::string_view text){for(char32_t cp:decode_utf8(text))set.insert(static_cast<std::uint32_t>(cp));}
#endif

} // namespace

UiRenderCache::~UiRenderCache(){clear();}

void UiRenderCache::clear(){
#ifdef _WIN32
    for(const auto& path:loaded_font_paths_)RemoveFontResourceExW(path.wstring().c_str(),FR_PRIVATE,nullptr);
#endif
    loaded_font_paths_.clear();atlas_uvs_.clear();font_glyphs_.clear();font_families_.clear();asset_warnings_.clear();atlas_={};atlas_.name="Vespera UI Atlas";atlas_.width=1;atlas_.height=1;atlas_.rgba8={255,255,255,255};++atlas_revision_;
}

void UiRenderCache::prepare_images(const UiDocument& document,const UiTextureResolver& resolver,const UiFontResolver& font_resolver){
#ifdef _WIN32
    for(const auto& path:loaded_font_paths_)RemoveFontResourceExW(path.wstring().c_str(),FR_PRIVATE,nullptr);
#endif
    loaded_font_paths_.clear();atlas_uvs_.clear();font_glyphs_.clear();font_families_.clear();asset_warnings_.clear();
    std::vector<PendingRegion> regions;std::unordered_set<std::string> seen_images;
    if(resolver){for(const auto& node:document.nodes()){const std::string key=asset_key(node.visual.image);if(key.empty()||seen_images.contains(key))continue;seen_images.insert(key);const auto resolved=resolver(node.visual.image);if(!resolved||!resolved->valid()){asset_warnings_.push_back(std::format("UI image '{}' could not be decoded",key));continue;}if(resolved->width>kAtlasMaxSize||resolved->height>kAtlasMaxSize){asset_warnings_.push_back(std::format("UI image '{}' exceeds {}x{} atlas limit",key,kAtlasMaxSize,kAtlasMaxSize));continue;}regions.push_back({key,*resolved,false});}}

#ifdef _WIN32
    std::unordered_map<std::string,FaceRequest> faces;
    for(const auto& node:document.nodes()){
        const bool text_capable=node.type==UiNodeType::Text||node.type==UiNodeType::Button||node.type==UiNodeType::ProgressBar||node.type==UiNodeType::TextInput||node.type==UiNodeType::Modal||node.type==UiNodeType::Tooltip||node.type==UiNodeType::Tabs||node.type==UiNodeType::Panel||node.type==UiNodeType::ScrollView;
        if(!text_capable)continue;const std::string key=font_face_key(node.text);auto& face=faces[key];face.key=key;face.family=node.text.font_family.empty()?"Segoe UI":node.text.font_family;face.weight=node.text.font_weight;face.italic=node.text.italic;
        if(!node.text.font.empty()&&font_resolver){const auto path=font_resolver(node.text.font);if(path){face.private_path=*path;}}
        collect_text_codepoints(face.codepoints,node.text.text);if(node.type==UiNodeType::TextInput)collect_text_codepoints(face.codepoints,node.input.placeholder);
        for(std::uint32_t cp=32;cp<=126;++cp)face.codepoints.insert(cp);for(std::uint32_t cp=160;cp<=255;++cp)face.codepoints.insert(cp);face.codepoints.insert(0x2022);face.codepoints.insert(0x2013);face.codepoints.insert(0x2014);
    }
    std::unordered_set<std::string> private_loaded;
    for(auto& [key,face]:faces){
        if(face.private_path){const auto normalized=face.private_path->lexically_normal().string();if(!private_loaded.contains(normalized)){if(AddFontResourceExW(face.private_path->wstring().c_str(),FR_PRIVATE,nullptr)>0){loaded_font_paths_.push_back(*face.private_path);private_loaded.insert(normalized);}else asset_warnings_.push_back("UI font asset could not be registered: "+face.private_path->string());}if(const auto family=sfnt_family_name(*face.private_path)){int needed=WideCharToMultiByte(CP_UTF8,0,family->c_str(),static_cast<int>(family->size()),nullptr,0,nullptr,nullptr);if(needed>0){std::string utf8(static_cast<std::size_t>(needed),'\0');WideCharToMultiByte(CP_UTF8,0,family->c_str(),static_cast<int>(family->size()),utf8.data(),needed,nullptr,nullptr);face.family=utf8;}}}
        font_families_[key]=face.family;auto glyph_regions=rasterize_face(face,asset_warnings_);regions.insert(regions.end(),std::make_move_iterator(glyph_regions.begin()),std::make_move_iterator(glyph_regions.end()));
    }
#else
    (void)font_resolver;
#endif

    struct Placement{int x=0,y=0,w=0,h=0;};std::vector<Placement> placements(regions.size());int cursor_x=1,cursor_y=1,row_height=0,atlas_width=2,atlas_height=2;
    for(std::size_t i=0;i<regions.size();++i){const int w=static_cast<int>(regions[i].texture.width),h=static_cast<int>(regions[i].texture.height);if(cursor_x+w+1>kAtlasMaxSize){cursor_x=1;cursor_y+=row_height+1;row_height=0;}if(cursor_y+h+1>kAtlasMaxSize){asset_warnings_.push_back(std::format("UI atlas is full; '{}' omitted",regions[i].key));continue;}placements[i]={cursor_x,cursor_y,w,h};cursor_x+=w+1;row_height=std::max(row_height,h);atlas_width=std::max(atlas_width,cursor_x+1);atlas_height=std::max(atlas_height,cursor_y+h+1);}
    atlas_width=std::clamp(atlas_width,1,kAtlasMaxSize);atlas_height=std::clamp(atlas_height,1,kAtlasMaxSize);TextureData next;next.name="Vespera UI Atlas";next.width=atlas_width;next.height=atlas_height;next.rgba8.assign(static_cast<std::size_t>(next.width)*next.height*4u,0);next.rgba8[0]=next.rgba8[1]=next.rgba8[2]=next.rgba8[3]=255;
    for(std::size_t i=0;i<regions.size();++i){const auto& p=placements[i];if(p.w<=0||p.h<=0)continue;const auto& source=regions[i].texture;for(int y=0;y<p.h;++y){const std::size_t src=static_cast<std::size_t>(y)*source.width*4u,dst=(static_cast<std::size_t>(p.y+y)*next.width+p.x)*4u;std::copy_n(source.rgba8.data()+src,static_cast<std::size_t>(p.w)*4u,next.rgba8.data()+dst);}const float iw=1.0f/next.width,ih=1.0f/next.height;AtlasUv uv{p.x*iw,p.y*ih,(p.x+p.w)*iw,(p.y+p.h)*ih,static_cast<std::uint32_t>(p.w),static_cast<std::uint32_t>(p.h)};if(regions[i].glyph){FontGlyph g;g.uv=uv;g.advance=regions[i].advance;g.offset_x=regions[i].offset_x;g.offset_y=regions[i].offset_y;g.line_height=regions[i].line_height;g.raster_size=kNativeFontRasterSize;font_glyphs_[regions[i].key]=g;}else atlas_uvs_[regions[i].key]=uv;}
    atlas_=std::move(next);++atlas_revision_;
}

UiRenderPacket UiRenderCache::build_packet(UiDocument& document,float viewport_width,float viewport_height,const UiPointerState& pointer,UiRuntimeState* runtime_state,const UiNavigationState& navigation,const UiTextInputState& text_input) const {
    UiRenderPacket packet;packet.viewport_width=std::max(0,static_cast<int>(std::lround(viewport_width)));packet.viewport_height=std::max(0,static_cast<int>(std::lround(viewport_height)));packet.atlas=&atlas_;packet.atlas_revision=atlas_revision_;packet.warnings=asset_warnings_;if(packet.viewport_width<=0||packet.viewport_height<=0)return packet;
    const UiLayoutResult layout=resolve_ui_layout(document,viewport_width,viewport_height);packet.warnings.insert(packet.warnings.end(),layout.warnings.begin(),layout.warnings.end());const UiRect viewport_clip{0,0,viewport_width,viewport_height};
    std::optional<UiNodeId> hovered;if(pointer.available)hovered=ui_hit_test(document,layout,pointer.position,true);
    if(runtime_state){runtime_state->clear_transient();runtime_state->hovered=hovered;if(pointer.primary_pressed&&hovered){runtime_state->pointer_capture=*hovered;runtime_state->focused=*hovered;}if(pointer.primary_down&&runtime_state->pointer_capture!=kInvalidUiNodeId)runtime_state->pressed=runtime_state->pointer_capture;if(pointer.primary_released){if(hovered&&runtime_state->pointer_capture==*hovered)runtime_state->pending_clicks.push_back(*hovered);runtime_state->pointer_capture=kInvalidUiNodeId;runtime_state->pressed.reset();}
        std::vector<UiNodeId> focusable;for(const auto& resolved:layout.nodes){if(!resolved.enabled)continue;const UiNode* node=document.find(resolved.id);if(!node)continue;if((node->type==UiNodeType::Button&&node->button.interactable)||(node->type==UiNodeType::TextInput&&!node->input.read_only))focusable.push_back(node->id);}if(!focusable.empty()&&(navigation.focus_next||navigation.focus_previous)){auto found=runtime_state->focused?std::find(focusable.begin(),focusable.end(),*runtime_state->focused):focusable.end();std::size_t index=found==focusable.end()?(navigation.focus_previous?focusable.size()-1:0):static_cast<std::size_t>(std::distance(focusable.begin(),found));if(found!=focusable.end()){if(navigation.focus_next)index=(index+1)%focusable.size();else index=(index+focusable.size()-1)%focusable.size();}runtime_state->focused=focusable[index];}if(navigation.activate_pressed&&runtime_state->focused){runtime_state->pending_clicks.push_back(*runtime_state->focused);runtime_state->pressed=runtime_state->focused;}if(runtime_state->focused){UiNode* focused=document.find(*runtime_state->focused);if(focused&&focused->type==UiNodeType::TextInput&&!focused->input.read_only){if(text_input.clear)focused->text.text.clear();if(text_input.backspace&&!focused->text.text.empty()){std::size_t erase_at=focused->text.text.size()-1;while(erase_at>0&&(static_cast<unsigned char>(focused->text.text[erase_at])&0xC0u)==0x80u)--erase_at;focused->text.text.erase(erase_at);}for(const unsigned char c:text_input.text){if(c<0x20u&&c!='\t')continue;if(focused->text.text.size()>=focused->input.max_length)break;focused->text.text.push_back(static_cast<char>(c));}}}packet.focused_node=runtime_state->focused;if(runtime_state->hovered)packet.hovered_button=runtime_state->hovered;if(runtime_state->pressed)packet.pressed_button=runtime_state->pressed;
    }else{packet.hovered_button=hovered;if(hovered&&pointer.primary_down)packet.pressed_button=hovered;}

    for(const auto& resolved:layout.nodes){if(!resolved.enabled)continue;const UiNode* node=document.find(resolved.id);if(!node||node->type==UiNodeType::Canvas)continue;const UiRect& rect=resolved.rect;if(rect.width<=0||rect.height<=0)continue;UiRect clip_storage=viewport_clip;if(resolved.clip_enabled){if(!intersect_rect(viewport_clip,resolved.clip_rect,clip_storage))continue;}const UiRect* clip=&clip_storage;
        const bool is_focused=runtime_state&&runtime_state->focused&&*runtime_state->focused==node->id;const bool is_hovered=runtime_state?(runtime_state->hovered&&*runtime_state->hovered==node->id):(hovered&&*hovered==node->id);const bool is_pressed=runtime_state?(runtime_state->pressed&&*runtime_state->pressed==node->id):(is_hovered&&pointer.primary_down);const float opacity=std::clamp(node->surface.opacity,0.0f,1.0f);
        const auto draw_background=[&](const std::array<float,4>& color){append_shadow(packet.vertices,rect,node->surface,clip);UiRect draw=rect;float border=std::clamp(node->surface.border_width,0.0f,std::min(rect.width,rect.height)*0.5f);if(border>0.01f&&node->surface.border_color[3]>0.001f){append_rounded_rect(packet.vertices,rect,node->surface.corner_radius,pack_color(node->surface.border_color,opacity),clip);draw=inset_rect(rect,border);}const std::string key=asset_key(node->visual.image);const auto image=atlas_uvs_.find(key);if(image!=atlas_uvs_.end())append_image(packet.vertices,draw,image->second,node->surface,pack_color(color,opacity),clip);else append_rounded_rect(packet.vertices,draw,std::max(0.0f,node->surface.corner_radius-border),pack_color(color,opacity),clip);};

        if(node->type==UiNodeType::Panel||node->type==UiNodeType::ScrollView||node->type==UiNodeType::List||node->type==UiNodeType::Grid||node->type==UiNodeType::Tabs||node->type==UiNodeType::Modal||node->type==UiNodeType::Tooltip){draw_background(node->visual.color);if(!node->text.text.empty()&&node->type!=UiNodeType::List&&node->type!=UiNodeType::Grid)append_styled_text(packet.vertices,rect,node->text.text,node->text,layout.canvas_scale,font_glyphs_,clip,opacity);
        }else if(node->type==UiNodeType::Image){append_shadow(packet.vertices,rect,node->surface,clip);const std::string key=asset_key(node->visual.image);const auto found=atlas_uvs_.find(key);if(found!=atlas_uvs_.end())append_image(packet.vertices,rect,found->second,node->surface,pack_color(node->visual.color,opacity),clip);else{const std::array<float,4> fallback{0.45f,0.18f,0.52f,0.85f};append_surface_fill(packet.vertices,rect,node->surface,multiply_color(node->visual.color,fallback),clip);UiTextProperties label=node->text;label.text="IMG";label.horizontal_alignment=UiHorizontalAlignment::Center;label.vertical_alignment=UiVerticalAlignment::Middle;append_styled_text(packet.vertices,rect,"IMG",label,layout.canvas_scale,font_glyphs_,clip,opacity);}
        }else if(node->type==UiNodeType::Text){append_styled_text(packet.vertices,rect,node->text.text,node->text,layout.canvas_scale,font_glyphs_,clip,opacity);
        }else if(node->type==UiNodeType::Button){const auto& color=!node->button.interactable?node->button.disabled_color:is_pressed?node->button.pressed_color:is_hovered?node->button.hovered_color:node->button.normal_color;draw_background(color);if(is_focused&&node->button.interactable){UiRect focus=inset_rect(rect,std::max(2.0f,node->surface.border_width+2.0f));const std::array<float,4> focus_color{0.42f,0.67f,1.0f,0.42f};append_rounded_rect(packet.vertices,focus,std::max(0.0f,node->surface.corner_radius-2.0f),pack_color(focus_color,opacity),clip);}append_styled_text(packet.vertices,rect,node->text.text,node->text,layout.canvas_scale,font_glyphs_,clip,opacity);
        }else if(node->type==UiNodeType::ProgressBar){append_shadow(packet.vertices,rect,node->surface,clip);append_rounded_rect(packet.vertices,rect,node->surface.corner_radius,pack_color(node->progress.background_color,opacity),clip);const float range=std::max(0.0001f,node->progress.maximum-node->progress.minimum),t=std::clamp((node->progress.value-node->progress.minimum)/range,0.0f,1.0f);UiRect fill=rect;fill.width*=t;if(fill.width>0)append_rounded_rect(packet.vertices,fill,std::min(node->surface.corner_radius,fill.width*0.5f),pack_color(node->progress.fill_color,opacity),clip);if(node->surface.border_width>0&&node->surface.border_color[3]>0) { const float bw=node->surface.border_width; append_rounded_rect(packet.vertices,rect,node->surface.corner_radius,pack_color(node->surface.border_color,opacity),clip); UiRect inner=inset_rect(rect,bw); append_rounded_rect(packet.vertices,inner,std::max(0.0f,node->surface.corner_radius-bw),pack_color(node->progress.background_color,opacity),clip); UiRect innerfill=inner; innerfill.width*=t; if(innerfill.width>0) append_rounded_rect(packet.vertices,innerfill,std::min(std::max(0.0f,node->surface.corner_radius-bw),innerfill.width*0.5f),pack_color(node->progress.fill_color,opacity),clip); }if(!node->text.text.empty())append_styled_text(packet.vertices,rect,node->text.text,node->text,layout.canvas_scale,font_glyphs_,clip,opacity);
        }else if(node->type==UiNodeType::TextInput){auto bg=node->visual.color;if(is_focused&&!node->input.read_only){bg[0]=std::min(1.0f,bg[0]+0.05f);bg[1]=std::min(1.0f,bg[1]+0.07f);bg[2]=std::min(1.0f,bg[2]+0.10f);}draw_background(bg);const bool placeholder=node->text.text.empty();UiTextProperties tp=node->text;tp.vertical_alignment=UiVerticalAlignment::Middle;if(placeholder)tp.color[3]*=0.55f;append_styled_text(packet.vertices,rect,placeholder?std::string_view(node->input.placeholder):std::string_view(node->text.text),tp,layout.canvas_scale,font_glyphs_,clip,opacity);
        }
    }
    return packet;
}

} // namespace vespera
