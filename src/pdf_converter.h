#pragma once
#include "options.h"
namespace shiftwheel {
// Output is exclusively owned staging, not a user destination.
// TXT preserves embedded text and recognizes textless pages when pdf_ocr is enabled.
void convert_pdf(const std::filesystem::path& input, const std::filesystem::path& output, Format format, const ConversionOptions& options = {});
}
