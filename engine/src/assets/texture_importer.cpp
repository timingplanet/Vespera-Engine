#include <vespera/assets/texture_importer.hpp>
#include <utility>

#if defined(_WIN32)
#include <objbase.h>
#include <wincodec.h>
#endif

#include <array>
#include <cstdint>
#include <fstream>
#include <limits>
#include <vector>

namespace vespera {
namespace {

std::uint16_t u16le(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8u);
}

std::uint32_t u32le(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0])
        | (static_cast<std::uint32_t>(p[1]) << 8u)
        | (static_cast<std::uint32_t>(p[2]) << 16u)
        | (static_cast<std::uint32_t>(p[3]) << 24u);
}

std::int32_t i32le(const std::uint8_t* p) {
    return static_cast<std::int32_t>(u32le(p));
}

TextureImportResult failure(std::string message) {
    return {false, {}, std::move(message)};
}

} // namespace

TextureData make_missing_texture_placeholder(std::string display_name) {
    TextureData texture;
    texture.name = display_name.empty() ? "Missing Texture" : std::move(display_name);
    texture.width = 2;
    texture.height = 2;
    // High-contrast magenta/charcoal checkerboard: obvious in-world, tiny in memory.
    texture.rgba8 = {
        255, 0, 255, 255, 36, 36, 42, 255,
        36, 36, 42, 255, 255, 0, 255, 255,
    };
    return texture;
}

TextureImportResult import_bmp_texture(const std::filesystem::path& path, std::string display_name) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return failure("could not open BMP texture: " + path.string());

    std::array<std::uint8_t, 54> header{};
    input.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
    if (input.gcount() != static_cast<std::streamsize>(header.size())) {
        return failure("BMP header is truncated: " + path.string());
    }
    if (header[0] != 'B' || header[1] != 'M') return failure("not a BMP file: " + path.string());

    const std::uint32_t pixel_offset = u32le(header.data() + 10);
    const std::uint32_t dib_size = u32le(header.data() + 14);
    const std::int32_t width_signed = i32le(header.data() + 18);
    const std::int32_t height_signed = i32le(header.data() + 22);
    const std::uint16_t planes = u16le(header.data() + 26);
    const std::uint16_t bits_per_pixel = u16le(header.data() + 28);
    const std::uint32_t compression = u32le(header.data() + 30);

    if (dib_size < 40u) return failure("BMP DIB header is unsupported (need BITMAPINFOHEADER or newer)");
    if (planes != 1u) return failure("BMP has an unsupported plane count");
    if (width_signed <= 0 || height_signed == 0) return failure("BMP has invalid dimensions");
    if (bits_per_pixel != 24u && bits_per_pixel != 32u) return failure("BMP must be uncompressed 24-bit or 32-bit");
    if (compression != 0u) return failure("compressed BMP textures are not supported");

    const bool top_down = height_signed < 0;
    const std::uint32_t width = static_cast<std::uint32_t>(width_signed);
    const std::uint32_t height = static_cast<std::uint32_t>(top_down ? -static_cast<std::int64_t>(height_signed) : height_signed);
    if (width > 16384u || height > 16384u) return failure("BMP dimensions exceed Vespera's 16384 pixel safety limit");

    const std::uint64_t source_row_bytes = (static_cast<std::uint64_t>(width) * bits_per_pixel + 7u) / 8u;
    const std::uint64_t padded_row_bytes = (source_row_bytes + 3u) & ~std::uint64_t{3u};
    const std::uint64_t pixel_bytes = padded_row_bytes * height;
    if (pixel_bytes > static_cast<std::uint64_t>((std::numeric_limits<std::size_t>::max)())) {
        return failure("BMP pixel payload is too large");
    }

    input.seekg(static_cast<std::streamoff>(pixel_offset), std::ios::beg);
    if (!input) return failure("BMP pixel offset is invalid");
    std::vector<std::uint8_t> source(static_cast<std::size_t>(pixel_bytes));
    input.read(reinterpret_cast<char*>(source.data()), static_cast<std::streamsize>(source.size()));
    if (input.gcount() != static_cast<std::streamsize>(source.size())) return failure("BMP pixel payload is truncated");

    TextureData texture;
    texture.name = display_name.empty() ? path.stem().string() : std::move(display_name);
    texture.width = width;
    texture.height = height;
    texture.rgba8.resize(static_cast<std::size_t>(width) * height * 4u);

    const std::size_t bytes_per_pixel = bits_per_pixel / 8u;
    bool any_nonzero_alpha = bits_per_pixel != 32u;
    for (std::uint32_t y = 0; y < height; ++y) {
        const std::uint32_t source_y = top_down ? y : (height - 1u - y);
        const auto* row = source.data() + static_cast<std::size_t>(source_y) * static_cast<std::size_t>(padded_row_bytes);
        for (std::uint32_t x = 0; x < width; ++x) {
            const auto* pixel = row + static_cast<std::size_t>(x) * bytes_per_pixel;
            const std::size_t out = (static_cast<std::size_t>(y) * width + x) * 4u;
            texture.rgba8[out + 0] = pixel[2];
            texture.rgba8[out + 1] = pixel[1];
            texture.rgba8[out + 2] = pixel[0];
            texture.rgba8[out + 3] = bits_per_pixel == 32u ? pixel[3] : 255u;
            if (bits_per_pixel == 32u && pixel[3] != 0u) any_nonzero_alpha = true;
        }
    }

    // A lot of ordinary 32-bit BI_RGB BMP writers leave the reserved alpha byte
    // at zero even though the image is intended to be opaque. Preserve real
    // mixed alpha, but interpret an all-zero alpha plane as fully opaque.
    if (bits_per_pixel == 32u && !any_nonzero_alpha) {
        for (std::size_t i = 3; i < texture.rgba8.size(); i += 4u) texture.rgba8[i] = 255u;
    }

    return {true, std::move(texture), "imported BMP texture: " + path.string()};
}


TextureImportResult import_tga_texture(const std::filesystem::path& path, std::string display_name) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return failure("could not open TGA texture: " + path.string());

    std::array<std::uint8_t, 18> header{};
    input.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
    if (input.gcount() != static_cast<std::streamsize>(header.size())) {
        return failure("TGA header is truncated: " + path.string());
    }

    const std::uint8_t id_length = header[0];
    const std::uint8_t color_map_type = header[1];
    const std::uint8_t image_type = header[2];
    const std::uint16_t width16 = u16le(header.data() + 12);
    const std::uint16_t height16 = u16le(header.data() + 14);
    const std::uint8_t bits_per_pixel = header[16];
    const std::uint8_t descriptor = header[17];

    if (color_map_type != 0u) return failure("color-mapped TGA textures are not supported");
    if (image_type != 2u && image_type != 10u) {
        return failure("TGA must be uncompressed or RLE true-color (type 2 or 10)");
    }
    if (width16 == 0u || height16 == 0u) return failure("TGA has invalid dimensions");
    if (bits_per_pixel != 24u && bits_per_pixel != 32u) return failure("TGA must be 24-bit or 32-bit true-color");

    const std::uint32_t width = width16;
    const std::uint32_t height = height16;
    const std::size_t pixel_count = static_cast<std::size_t>(width) * height;
    if (pixel_count > ((std::numeric_limits<std::size_t>::max)() / 4u)) {
        return failure("TGA pixel payload is too large");
    }

    if (id_length != 0u) {
        input.seekg(static_cast<std::streamoff>(id_length), std::ios::cur);
        if (!input) return failure("TGA image ID field is truncated");
    }

    TextureData texture;
    texture.name = display_name.empty() ? path.stem().string() : std::move(display_name);
    texture.width = width;
    texture.height = height;
    texture.rgba8.resize(pixel_count * 4u);

    const std::size_t bytes_per_pixel = bits_per_pixel / 8u;
    const bool top_origin = (descriptor & 0x20u) != 0u;
    const bool right_origin = (descriptor & 0x10u) != 0u;

    const auto write_pixel = [&](std::size_t file_index, const std::uint8_t* pixel) {
        const std::uint32_t file_x = static_cast<std::uint32_t>(file_index % width);
        const std::uint32_t file_y = static_cast<std::uint32_t>(file_index / width);
        const std::uint32_t x = right_origin ? (width - 1u - file_x) : file_x;
        const std::uint32_t y = top_origin ? file_y : (height - 1u - file_y);
        const std::size_t out = (static_cast<std::size_t>(y) * width + x) * 4u;
        texture.rgba8[out + 0] = pixel[2];
        texture.rgba8[out + 1] = pixel[1];
        texture.rgba8[out + 2] = pixel[0];
        texture.rgba8[out + 3] = bytes_per_pixel == 4u ? pixel[3] : 255u;
    };

    std::array<std::uint8_t, 4> pixel{};
    std::size_t written = 0;
    if (image_type == 2u) {
        while (written < pixel_count) {
            input.read(reinterpret_cast<char*>(pixel.data()), static_cast<std::streamsize>(bytes_per_pixel));
            if (input.gcount() != static_cast<std::streamsize>(bytes_per_pixel)) {
                return failure("TGA pixel payload is truncated");
            }
            write_pixel(written++, pixel.data());
        }
    } else {
        while (written < pixel_count) {
            std::uint8_t packet = 0;
            input.read(reinterpret_cast<char*>(&packet), 1);
            if (input.gcount() != 1) return failure("TGA RLE packet stream is truncated");
            const std::size_t count = static_cast<std::size_t>(packet & 0x7fu) + 1u;
            if (count > pixel_count - written) return failure("TGA RLE packet overruns the image");
            if ((packet & 0x80u) != 0u) {
                input.read(reinterpret_cast<char*>(pixel.data()), static_cast<std::streamsize>(bytes_per_pixel));
                if (input.gcount() != static_cast<std::streamsize>(bytes_per_pixel)) {
                    return failure("TGA RLE pixel is truncated");
                }
                for (std::size_t i = 0; i < count; ++i) write_pixel(written++, pixel.data());
            } else {
                for (std::size_t i = 0; i < count; ++i) {
                    input.read(reinterpret_cast<char*>(pixel.data()), static_cast<std::streamsize>(bytes_per_pixel));
                    if (input.gcount() != static_cast<std::streamsize>(bytes_per_pixel)) {
                        return failure("TGA raw packet is truncated");
                    }
                    write_pixel(written++, pixel.data());
                }
            }
        }
    }

    return {true, std::move(texture), "imported TGA texture: " + path.string()};
}


#if defined(_WIN32)
namespace {
template <typename T>
struct ComPtrLite {
    T* ptr = nullptr;
    ~ComPtrLite() { if (ptr) ptr->Release(); }
    T** out() { return &ptr; }
    T* operator->() const { return ptr; }
    explicit operator bool() const { return ptr != nullptr; }
};

struct ComInitGuard {
    HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool owns_initialization = SUCCEEDED(result);
    ~ComInitGuard() { if (owns_initialization) CoUninitialize(); }
};
}

TextureImportResult import_wic_texture(const std::filesystem::path& path, std::string display_name) {
    ComInitGuard com;
    if (FAILED(com.result) && com.result != RPC_E_CHANGED_MODE) {
        return failure("Windows Imaging Component COM initialization failed");
    }

    ComPtrLite<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(factory.out()));
    if (FAILED(hr) || !factory) return failure("could not create Windows Imaging Component factory");

    ComPtrLite<IWICBitmapDecoder> decoder;
    hr = factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
        WICDecodeMetadataCacheOnLoad, decoder.out());
    if (FAILED(hr) || !decoder) return failure("WIC could not decode texture: " + path.string());

    ComPtrLite<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, frame.out());
    if (FAILED(hr) || !frame) return failure("WIC image has no decodable first frame");

    UINT width = 0, height = 0;
    hr = frame->GetSize(&width, &height);
    if (FAILED(hr) || width == 0u || height == 0u || width > 16384u || height > 16384u) {
        return failure("WIC texture dimensions are invalid or exceed 16384 pixels");
    }

    ComPtrLite<IWICFormatConverter> converter;
    hr = factory->CreateFormatConverter(converter.out());
    if (FAILED(hr) || !converter) return failure("could not create WIC format converter");
    hr = converter->Initialize(frame.ptr, GUID_WICPixelFormat32bppRGBA,
        WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) return failure("WIC could not convert texture to RGBA8");

    const std::uint64_t stride64 = static_cast<std::uint64_t>(width) * 4u;
    const std::uint64_t bytes64 = stride64 * height;
    if (bytes64 > static_cast<std::uint64_t>((std::numeric_limits<std::size_t>::max)())
        || stride64 > static_cast<std::uint64_t>((std::numeric_limits<UINT>::max)())
        || bytes64 > static_cast<std::uint64_t>((std::numeric_limits<UINT>::max)())) {
        return failure("WIC texture payload is too large");
    }

    TextureData texture;
    texture.name = display_name.empty() ? path.stem().string() : std::move(display_name);
    texture.width = width;
    texture.height = height;
    texture.rgba8.resize(static_cast<std::size_t>(bytes64));
    hr = converter->CopyPixels(nullptr, static_cast<UINT>(stride64), static_cast<UINT>(bytes64), texture.rgba8.data());
    if (FAILED(hr)) return failure("WIC failed while copying decoded pixels");
    return {true, std::move(texture), "imported WIC texture: " + path.string()};
}
#endif

TextureImportResult import_texture(const std::filesystem::path& path, std::string display_name) {
    auto extension = path.extension().string();
    for (char& c : extension) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    if (extension == ".bmp") return import_bmp_texture(path, std::move(display_name));
    if (extension == ".tga") return import_tga_texture(path, std::move(display_name));
#if defined(_WIN32)
    if (extension == ".png" || extension == ".jpg" || extension == ".jpeg") {
        return import_wic_texture(path, std::move(display_name));
    }
#endif
    return failure("no decoded texture importer is available for '" + extension + "' yet");
}

} // namespace vespera
