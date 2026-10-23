#pragma once
#include <filesystem>
namespace shiftwheel {
enum class HdrTransfer { None, Pq, Hlg };
// Raw, full-range 16-bit RGB(A) PNG from the pinned codec. No color profiles
// are applied here; the caller must first validate BT.2020 PQ/HLG metadata.
void hdr_to_sdr(const std::filesystem::path& input, const std::filesystem::path& output, HdrTransfer transfer);
}
