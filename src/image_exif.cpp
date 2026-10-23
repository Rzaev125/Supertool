#include "image_exif.h"
#include <array>
#include <cstring>
#include <stdexcept>

namespace shiftwheel {
namespace {
[[noreturn]] void invalid() { throw std::runtime_error("Повреждена или неоднозначна ориентация EXIF изображения."); }
struct Reader {
    HANDLE file;
    uint64_t length;
    const BYTE* memory = nullptr;
    std::array<BYTE, 12> read(uint64_t offset, DWORD size) const {
        if (size > 12 || offset > length || size > length - offset) invalid();
        std::array<BYTE, 12> data{}; DWORD count = 0;
        if (memory) { std::memcpy(data.data(), memory + offset, size); return data; }
        LARGE_INTEGER position{}; position.QuadPart = static_cast<LONGLONG>(offset);
        if (!SetFilePointerEx(file, position, nullptr, FILE_BEGIN) ||
            !ReadFile(file, data.data(), size, &count, nullptr) || count != size) invalid();
        return data;
    }
};
uint32_t number(const std::array<BYTE, 12>& data, unsigned offset, unsigned size, bool little) {
    uint32_t value = 0;
    for (unsigned i = 0; i < size; ++i) value = (value << 8) | data[offset + (little ? size - i - 1 : i)];
    return value;
}
bool valid_date(const std::string& text) {
    if (text.size() != 19) return false;
    for (unsigned i = 0; i < 19; ++i) {
        const char separator = i == 4 || i == 7 || i == 13 || i == 16 ? ':' : i == 10 ? ' ' : 0;
        if (separator ? text[i] != separator : text[i] < '0' || text[i] > '9') return false;
    }
    auto pair = [&](unsigned i) { return unsigned(text[i] - '0') * 10 + unsigned(text[i + 1] - '0'); };
    const unsigned year = pair(0) * 100 + pair(2), month = pair(5), day = pair(8);
    constexpr unsigned days[]{31,28,31,30,31,30,31,31,30,31,30,31};
    return year && month >= 1 && month <= 12 && day >= 1 &&
        day <= days[month - 1] + (month == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0) ? 1 : 0) &&
        pair(11) <= 23 && pair(14) <= 59 && pair(17) <= 59;
}
// Only selected bounded values are dereferenced. Unknown IFDs and pixel offsets
// are never walked. Optional invalid values are omitted, structural bounds fail.
std::vector<BYTE> value_bytes(const Reader& reader, uint64_t base, uint64_t size,
    const std::array<BYTE,12>& entry, bool little, unsigned type, unsigned maximum) {
    const auto count = number(entry, 4, 4, little);
    if (number(entry, 2, 2, little) != type || !count || count > maximum) return {};
    std::vector<BYTE> result(count);
    if (count <= 4) std::memcpy(result.data(), entry.data() + 8, count);
    else {
        const uint64_t offset = number(entry, 8, 4, little);
        if (offset < 8 || offset > size || count > size - offset) invalid();
        for (unsigned i = 0; i < count; ++i) result[i] = reader.read(base + offset + i, 1)[0];
    }
    return result;
}
std::string text_value(const std::vector<BYTE>& bytes) {
    if (bytes.empty() || bytes.back() != 0) return {};
    for (size_t i = 0; i + 1 < bytes.size(); ++i) if (bytes[i] < 32 || bytes[i] == 127) return {};
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size() - 1};
}
bool valid_xp(const std::vector<BYTE>& bytes) {
    if (bytes.size() < 2 || bytes.size() % 2 || bytes[bytes.size()-1] || bytes[bytes.size()-2]) return false;
    for (size_t i = 0; i + 2 < bytes.size(); i += 2) {
        unsigned ch = unsigned(bytes[i]) | unsigned(bytes[i+1]) << 8;
        if (ch < 32 || (ch >= 127 && ch <= 159) || (ch >= 0xdc00 && ch <= 0xdfff)) return false;
        if (ch >= 0xd800 && ch <= 0xdbff) {
            if (i + 4 >= bytes.size()) return false;
            ch = unsigned(bytes[i+2]) | unsigned(bytes[i+3]) << 8;
            if (ch < 0xdc00 || ch > 0xdfff) return false;
            i += 2;
        }
    }
    return true;
}
unsigned directory(const Reader& reader, uint64_t base, uint64_t size, PhotoExif* details = nullptr) {
    if (size < 8) invalid();
    const auto header = reader.read(base, 8);
    const bool little = header[0] == 'I' && header[1] == 'I';
    if (!little && !(header[0] == 'M' && header[1] == 'M')) invalid();
    if (number(header, 2, 2, little) != 42) invalid();
    const uint64_t offset = number(header, 4, 4, little);
    if (offset < 8 || offset > size || size - offset < 2) invalid();
    const auto count = number(reader.read(base + offset, 2), 0, 2, little);
    // The selected root IFD is bounded independently of pixel data/file size.
    if (count > 4096 || uint64_t(count) * 12 + 6 > size - offset) invalid();
    bool found = false;
    unsigned result = 1;
    unsigned artist_count = 0, copyright_count = 0, xp_count = 0, exif_count = 0;
    uint64_t exif_offset = 0;
    for (uint32_t i = 0; i < count; ++i) {
        const auto entry = reader.read(base + offset + 2 + uint64_t(i) * 12, 12);
        const auto tag = number(entry, 0, 2, little);
        if (details) {
            if (tag == 315) {
                if (++artist_count == 1) details->artist = text_value(value_bytes(reader, base, size, entry, little, 2, 256));
                else details->artist.clear();
            } else if (tag == 33432) {
                if (++copyright_count == 1) details->copyright = text_value(value_bytes(reader, base, size, entry, little, 2, 512));
                else details->copyright.clear();
            } else if (tag == 40093) {
                if (++xp_count == 1) {
                    auto bytes = value_bytes(reader, base, size, entry, little, 1, 1024);
                    if (valid_xp(bytes)) details->xp_author = std::move(bytes);
                } else details->xp_author.clear();
            } else if (tag == 34665) {
                if (++exif_count > 1 || number(entry, 2, 2, little) != 4 || number(entry, 4, 4, little) != 1) invalid();
                exif_offset = number(entry, 8, 4, little);
                if (exif_offset < 8) invalid();
            }
        }
        if (tag != 274) continue;
        if (found || number(entry, 2, 2, little) != 3 || number(entry, 4, 4, little) != 1) invalid();
        const auto orientation = number(entry, 8, 2, little);
        if (orientation < 1 || orientation > 8) invalid();
        found = true;
        result = orientation;
    }
    if (details && exif_offset) {
        if (exif_offset == offset || exif_offset > size || size - exif_offset < 6) invalid();
        const auto entries = number(reader.read(base + exif_offset, 2), 0, 2, little);
        if (entries > 4096 || uint64_t(entries)*12 + 6 > size - exif_offset) invalid();
        unsigned dates = 0;
        for (unsigned i = 0; i < entries; ++i) {
            const auto entry = reader.read(base + exif_offset + 2 + uint64_t(i)*12, 12);
            if (number(entry, 0, 2, little) == 36867) {
                if (++dates == 1) {
                    const auto text = text_value(value_bytes(reader, base, size, entry, little, 2, 20));
                    if (valid_date(text)) details->capture_date = text;
                } else details->capture_date.clear();
            }
        }
    }
    return result;
}
void jpeg(const Reader& reader, PhotoExif* details = nullptr) {
    uint64_t offset = 2; unsigned steps = 0, exif_blocks = 0;
    while (offset < reader.length) {
        if (++steps > 65536 || reader.read(offset++, 1)[0] != 0xff) invalid();
        BYTE marker = 0xff;
        while (marker == 0xff) {
            if (++steps > 65536) invalid();
            marker = reader.read(offset++, 1)[0];
        }
        if (marker == 0xda || marker == 0xd9) return; // Never scan entropy-coded pixels.
        if (marker == 0x01 || (marker >= 0xd0 && marker <= 0xd7)) continue;
        const uint64_t length = number(reader.read(offset, 2), 0, 2, false);
        if (length < 2 || length > reader.length - offset) invalid();
        if (marker == 0xe1 && length >= 8) {
            const auto prefix = reader.read(offset + 2, 6);
            if (!std::memcmp(prefix.data(), "Exif\0\0", 6)) {
                if (++exif_blocks > 1) invalid();
                const auto orientation = directory(reader, offset + 8, length - 8, details);
                if (details) details->orientation = orientation;
            }
        }
        offset += length;
    }
}
}
unsigned inspect_tiff_orientation(std::span<const BYTE> bytes) {
    return directory({nullptr, bytes.size(), bytes.data()}, 0, bytes.size());
}
PhotoExif inspect_tiff_exif(std::span<const BYTE> bytes, bool details) {
    PhotoExif result;
    result.orientation = directory({nullptr, bytes.size(), bytes.data()}, 0, bytes.size(), details ? &result : nullptr);
    return result;
}
PhotoExif inspect_image_exif(HANDLE input, uint64_t length) {
    PhotoExif result;
    if (length >= 8) {
        const Reader reader{input, length}; const auto header = reader.read(0, 8);
        if ((header[0] == 'I' && header[1] == 'I') || (header[0] == 'M' && header[1] == 'M'))
            result.orientation = directory(reader, 0, length, &result);
        else if (header[0] == 0xff && header[1] == 0xd8) jpeg(reader, &result);
    }
    LARGE_INTEGER zero{};
    if (!SetFilePointerEx(input, zero, nullptr, FILE_BEGIN)) invalid();
    return result;
}
void validate_image_exif(HANDLE input, uint64_t length) {
    if (length < 8) return; // WIC supplies the ordinary invalid-image error.
    const Reader reader{input, length}; const auto header = reader.read(0, 8);
    if ((header[0] == 'I' && header[1] == 'I') || (header[0] == 'M' && header[1] == 'M')) directory(reader, 0, length);
    else if (header[0] == 0xff && header[1] == 0xd8) jpeg(reader);
    LARGE_INTEGER zero{};
    if (!SetFilePointerEx(input, zero, nullptr, FILE_BEGIN)) invalid();
}
}
