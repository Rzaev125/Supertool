#include "image_exif.h"
#include <array>
#include <cstring>
#include <stdexcept>

namespace shiftwheel {
namespace {
[[noreturn]] void invalid() { throw std::runtime_error("Повреждена или неоднозначна ориентация EXIF JPEG/TIFF."); }
struct Reader {
    HANDLE file;
    uint64_t length;
    std::array<BYTE, 12> read(uint64_t offset, DWORD size) const {
        if (size > 12 || offset > length || size > length - offset) invalid();
        LARGE_INTEGER position{}; position.QuadPart = static_cast<LONGLONG>(offset);
        std::array<BYTE, 12> data{}; DWORD count = 0;
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
void directory(const Reader& reader, uint64_t base, uint64_t size) {
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
    for (uint32_t i = 0; i < count; ++i) {
        const auto entry = reader.read(base + offset + 2 + uint64_t(i) * 12, 12);
        if (number(entry, 0, 2, little) != 274) continue;
        if (found || number(entry, 2, 2, little) != 3 || number(entry, 4, 4, little) != 1) invalid();
        const auto orientation = number(entry, 8, 2, little);
        if (orientation < 1 || orientation > 8) invalid();
        found = true;
    }
}
void jpeg(const Reader& reader) {
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
                directory(reader, offset + 8, length - 8);
            }
        }
        offset += length;
    }
}
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
