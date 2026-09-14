#include <vespera/assets/font_asset.hpp>

#include <array>
#include <fstream>

namespace vespera {
namespace {
std::uint16_t be16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(p[0]) << 8u) | p[1]);
}
}

const char* font_container_name(FontContainerKind kind) {
    return kind == FontContainerKind::OpenTypeCff ? "OpenType/CFF" : "TrueType";
}

FontAssetInspectResult inspect_font_asset(const std::filesystem::path& path) {
    FontAssetInspectResult result;
    std::ifstream input(path, std::ios::binary);
    if (!input) { result.message = "could not open font source: " + path.string(); return result; }
    std::array<std::uint8_t, 12> header{};
    input.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
    if (input.gcount() != static_cast<std::streamsize>(header.size())) {
        result.message = "font source is too small to contain an SFNT header";
        return result;
    }
    const bool true_type = header[0] == 0x00 && header[1] == 0x01 && header[2] == 0x00 && header[3] == 0x00;
    const bool open_type = header[0] == 'O' && header[1] == 'T' && header[2] == 'T' && header[3] == 'O';
    if (!true_type && !open_type) {
        result.message = "font is not a supported TrueType/OpenType SFNT source";
        return result;
    }
    const auto table_count = be16(header.data() + 4);
    if (table_count == 0) { result.message = "font SFNT table directory is empty"; return result; }
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) { result.message = "could not query font source size: " + ec.message(); return result; }
    const std::uintmax_t minimum = 12u + static_cast<std::uintmax_t>(table_count) * 16u;
    if (size < minimum) { result.message = "font SFNT table directory is truncated"; return result; }
    result.ok = true;
    result.font.source_path = path;
    result.font.container = open_type ? FontContainerKind::OpenTypeCff : FontContainerKind::TrueType;
    result.font.table_count = table_count;
    result.font.source_size = size;
    result.message = "font SFNT source validated";
    return result;
}

} // namespace vespera
