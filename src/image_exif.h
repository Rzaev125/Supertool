#pragma once
#include <windows.h>
#include <cstdint>
namespace shiftwheel {
// Inspect the original IFD before WIC coalesces duplicate tag IDs. No pixel
// decoding, opaque metadata copy or unbounded allocation. Restores file offset 0.
void validate_image_exif(HANDLE input, uint64_t length);
}
