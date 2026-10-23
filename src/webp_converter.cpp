#include "webp_converter.h"
#include "avif_converter.h"
#include "image_converter.h"
#include "engines.h"
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace shiftwheel {
namespace fs = std::filesystem;
namespace {
[[noreturn]] void invalid() { throw std::runtime_error("Повреждённый или неподдерживаемый контейнер WebP."); }
uint32_t le(const unsigned char* p, unsigned bytes) {
    uint32_t value = 0;
    for (unsigned i = 0; i < bytes; ++i) value |= uint32_t(p[i]) << (i * 8);
    return value;
}
void dimensions(uint32_t width, uint32_t height) {
    if (!width || !height || width > 32768 || height > 32768 || uint64_t(width) * height > 40000000)
        throw std::runtime_error("Лимит изображения: 40 млн пикселей и 32768 пикселей по каждой стороне.");
}
unsigned check_exif(std::vector<unsigned char> data) {
    // WebP EXIF may contain either the TIFF header or an Exif\0\0 prefix.
    if (data.size() >= 6 && !memcmp(data.data(), "Exif\0\0", 6)) data.erase(data.begin(), data.begin() + 6);
    if (data.size() < 8) invalid();
    const bool little = data[0] == 'I' && data[1] == 'I';
    if (!little && !(data[0] == 'M' && data[1] == 'M')) invalid();
    auto number = [&](size_t offset, unsigned bytes) {
        if (offset > data.size() || bytes > data.size() - offset) invalid();
        uint32_t value = 0;
        for (unsigned i = 0; i < bytes; ++i) value = (value << 8) | data[offset + (little ? bytes - i - 1 : i)];
        return value;
    };
    if (number(2, 2) != 42) invalid();
    size_t offset = number(4, 4);
    if (offset < 8) invalid();
    const auto count = number(offset, 2);
    if (count > 4096 || offset + 2 + uint64_t(count) * 12 + 4 > data.size()) invalid();
    unsigned orientation = 1; bool found = false;
    for (uint32_t i = 0; i < count; ++i) {
        size_t entry = offset + 2 + size_t(i) * 12;
        if (number(entry, 2) == 274) {
            if (number(entry + 2, 2) != 3 || number(entry + 4, 4) != 1) invalid();
            if (found) invalid();
            orientation = number(entry + 8, 2); found = true;
            if (orientation < 1 || orientation > 8) invalid();
        }
    }
    return orientation;
}
struct WebpInfo { bool profile = false; unsigned orientation = 1; };
WebpInfo validate_webp(const fs::path& input) {
    const uint64_t size = fs::file_size(input);
    if (size < 20 || size > 1024ULL * 1024 * 1024) invalid();
    std::ifstream file(input, std::ios::binary);
    auto read = [&](uint64_t offset, unsigned char* data, size_t count) {
        file.seekg(static_cast<std::streamoff>(offset));
        file.read(reinterpret_cast<char*>(data), static_cast<std::streamsize>(count));
        if (!file) invalid();
    };
    std::array<unsigned char, 12> riff{}; read(0, riff.data(), riff.size());
    if (memcmp(riff.data(), "RIFF", 4) || memcmp(riff.data() + 8, "WEBP", 4) || uint64_t(le(riff.data() + 4, 4)) + 8 != size) invalid();
    unsigned frames = 0, chunks = 0;
    bool extended = false, profile = false, profile_flag = false, exif = false, exif_flag = false;
    unsigned orientation = 1;
    uint32_t canvas_width = 0, canvas_height = 0, width = 0, height = 0;
    for (uint64_t offset = 12; offset < size;) {
        if (size - offset < 8 || ++chunks > 65536) invalid();
        std::array<unsigned char, 8> header{}; read(offset, header.data(), header.size());
        uint32_t length = le(header.data() + 4, 4);
        uint64_t end = offset + 8 + length + (length & 1U);
        if (end > size) invalid();
        auto is = [&](const char* tag) { return memcmp(header.data(), tag, 4) == 0; };
        if (is("ANIM") || is("ANMF")) throw std::runtime_error("Анимированные WebP пока не поддерживаются.");
        if (is("VP8X")) {
            if (length != 10 || offset != 12 || extended) invalid();
            std::array<unsigned char, 10> data{}; read(offset + 8, data.data(), data.size());
            if (data[0] & 2) throw std::runtime_error("Анимированные WebP пока не поддерживаются.");
            if ((data[0] & 0xc1) || data[1] || data[2] || data[3]) invalid();
            canvas_width = le(data.data() + 4, 3) + 1; canvas_height = le(data.data() + 7, 3) + 1;
            dimensions(canvas_width, canvas_height); extended = true; profile_flag = (data[0] & 0x20) != 0; exif_flag = (data[0] & 0x08) != 0;
        } else if (is("VP8 ") || is("VP8L")) {
            const bool lossless = is("VP8L");
            const size_t needed = lossless ? 5 : 10;
            if (++frames != 1 || length < needed) invalid();
            std::array<unsigned char, 10> data{}; read(offset + 8, data.data(), needed);
            if (lossless) {
                if (data[0] != 0x2f || (data[4] & 0xe0)) invalid();
                auto bits = le(data.data() + 1, 4); width = (bits & 0x3fff) + 1; height = ((bits >> 14) & 0x3fff) + 1;
            } else {
                if ((data[0] & 1) || memcmp(data.data() + 3, "\x9d\x01\x2a", 3)) invalid();
                width = le(data.data() + 6, 2) & 0x3fff; height = le(data.data() + 8, 2) & 0x3fff;
            }
            dimensions(width, height);
        } else if (is("ICCP")) {
            if (profile || !length || length > 4 * 1024 * 1024) invalid();
            profile = true;
        } else if (is("EXIF")) {
            if (exif || length > 4 * 1024 * 1024) invalid();
            exif = true;
            std::vector<unsigned char> data(length); read(offset + 8, data.data(), data.size()); orientation = check_exif(std::move(data));
        }
        offset = end;
    }
    if (frames != 1 || profile != profile_flag || exif != exif_flag || (extended && (width != canvas_width || height != canvas_height))) invalid();
    return {profile, orientation};
}
void image_engine(const fs::path& input, const fs::path& output, bool encode, const fs::path& log, const ConversionOptions& options) {
    const auto threads = std::to_wstring(resource_threads(options.resources));
    std::vector<std::wstring> args{L"-hide_banner", L"-nostdin", L"-loglevel", L"error", L"-xerror", L"-max_error_rate", L"0", L"-n",
        L"-threads", threads, L"-err_detect", L"explode", L"-filter_threads", threads, L"-filter_complex_threads", threads,
        L"-protocol_whitelist", L"file,pipe", L"-noautorotate", L"-i", input.wstring(), L"-map", L"0:v:0", L"-frames:v", L"1", L"-an", L"-sn", L"-dn"};
    if (encode) {
        const std::vector<std::wstring> codec{L"-c:v", L"libwebp", L"-lossless", options.quality == QualityPreset::Compact ? L"0" : L"1",
            L"-compression_level", options.quality == QualityPreset::High ? L"6" : L"4",
            L"-quality", options.quality == QualityPreset::Compact ? L"70" : options.quality == QualityPreset::High ? L"100" : L"75", L"-pix_fmt", L"bgra"};
        args.insert(args.end(), codec.begin(), codec.end());
    } else {
        args.insert(args.end(), {L"-c:v", L"png", L"-pix_fmt", L"rgba", L"-update", L"1"});
    }
    args.insert(args.end(), {L"-threads", threads, output.wstring()});
    run_engine(module_path(Category::Video), args, log, 300000);
}
}
void convert_webp(const fs::path& input, const fs::path& output, Format format, const fs::path& work, const ConversionOptions& options) {
    validate_options(options);
    fs::path pixels = input;
    unsigned orientation = 0;
    ImageColor color = ImageColor::ToSrgb;
    const auto source_format = parse_format(input.extension().wstring());
    if (source_format == Format::Webp) {
        const auto info = validate_webp(input);
        if (info.profile) color = ImageColor::RequireProfile;
        orientation = info.orientation;
        pixels = work / L"webp-decoded.png";
        image_engine(input, pixels, false, work / L"webp-decode.log", options);
    } else if (source_format == Format::Avif) {
        pixels = work / L"avif-bridge.png";
        convert_avif(input, pixels, Format::Png, work, options);
    }
    if (format == Format::Webp || format == Format::Avif) {
        const auto normalized = work / L"webp-normalized.png";
        convert_image(pixels, normalized, work / L"webp-normalize.tmp", color, options, orientation);
        if (format == Format::Avif) convert_avif(normalized, output, format, work, options);
        else image_engine(normalized, output, true, work / L"webp-encode.log", options);
    } else convert_image(pixels, output, work / L"webp-image.tmp", color, options, orientation);
}
}
