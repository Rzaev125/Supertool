#pragma once
#include <windows.h>
#include <cstdint>
#include <span>
#include <string>
#include <vector>
namespace shiftwheel {
// Inspect the original IFD before WIC coalesces duplicate tag IDs. No pixel
// decoding, opaque metadata copy or unbounded allocation. Restores file offset 0.
void validate_image_exif(HANDLE input, uint64_t length);
unsigned inspect_tiff_orientation(std::span<const BYTE> bytes);
struct PhotoExif {
    unsigned orientation = 1;
    std::string capture_date, artist, copyright;
    std::vector<BYTE> xp_author;
};
PhotoExif inspect_tiff_exif(std::span<const BYTE> bytes, bool details);
PhotoExif inspect_image_exif(HANDLE input, uint64_t length);
}
