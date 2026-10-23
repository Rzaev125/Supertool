#include "avif_info.h"
#include <fstream>
#include <stdexcept>
#include <charconv>

namespace shiftwheel {
// avifdec 1.4.2 has no JSON output. Check complete, pinned diagnostic lines,
// never substrings of the input filename or metadata. Fail closed if changed.
AvifImageInfo inspect_decoded_avif(const std::filesystem::path& log) {
    std::ifstream stream(log);
    bool single = false, srgb_transfer = false, srgb_primaries = false, profile = false, hdr = false;
    bool transforms = false, transformed = false, invalid_transform = false, crop = false, valid_crop = false;
    unsigned transform_headers = 0, transform_properties = 0;
    AvifImageInfo info;
    unsigned depths = 0, primaries = 0, transfers = 0, matrices = 0, profiles = 0, gainmaps = 0, alphas = 0;
    bool no_gainmap = false, straight_alpha = false, invalid_numbers = false, no_profile = false;
    for (std::string line; std::getline(stream, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        single |= line == " * Frame:";
        if (line == " * Transformations: None" || line == " * Transformations:") {
            ++transform_headers;
            transforms = line == " * Transformations:";
            transformed |= transforms;
        } else if (transforms) {
            if (line.starts_with("    * ")) {
                ++transform_properties;
                const bool rotation = line == "    * irot (Rotation)      : 0" || line == "    * irot (Rotation)      : 1" ||
                    line == "    * irot (Rotation)      : 2" || line == "    * irot (Rotation)      : 3";
                const bool mirror = line == "    * imir (Mirror)        : 0 (top-to-bottom)" ||
                    line == "    * imir (Mirror)        : 1 (left-to-right)";
                const bool aperture = line.starts_with("    * clap (Clean Aperture): ");
                crop |= aperture;
                invalid_transform |= !rotation && !mirror && !aperture;
            } else if (line.starts_with("      * Valid, derived crop rect: ")) valid_crop = true;
            else if (line.starts_with("      * Invalid: ")) invalid_transform = true;
            else transforms = false;
        }
        // The pinned decoder can exit successfully after ignoring invalid transforms.
        // Never publish that fallback PNG as if the requested display was applied.
        invalid_transform |= line == "Warning, ignoring invalid transforms (clap/irot/imir)" ||
            line == "Warning: Invalid Clean Aperture values" || line == "Invalid clean aperture box";
        srgb_transfer |= line == " * Transfer Char. : 13" || line == " * Transfer Char. : 2";
        srgb_primaries |= line == " * Color Primaries: 1" || line == " * Color Primaries: 2";
        hdr |= line == " * Transfer Char. : 16" || line == " * Transfer Char. : 18";
        profile |= line.starts_with(" * ICC Profile    : Present (") && line.ends_with(" bytes)");
        auto number = [&](const char* prefix, int& value, unsigned& count) {
            if (!line.starts_with(prefix)) return;
            ++count;
            const auto start = line.data() + std::char_traits<char>::length(prefix);
            const auto parsed = std::from_chars(start, line.data() + line.size(), value);
            invalid_numbers |= parsed.ec != std::errc{} || parsed.ptr != line.data() + line.size();
        };
        number(" * Bit Depth      : ", info.depth, depths);
        number(" * Color Primaries: ", info.primaries, primaries);
        number(" * Transfer Char. : ", info.transfer, transfers);
        number(" * Matrix Coeffs. : ", info.matrix, matrices);
        if (line.starts_with(" * ICC Profile    : ")) { ++profiles; no_profile = line == " * ICC Profile    : Absent"; }
        if (line.starts_with(" * Gain map       : ")) { ++gainmaps; no_gainmap = line == " * Gain map       : Absent"; }
        if (line.starts_with(" * Alpha          : ")) { ++alphas; straight_alpha = line == " * Alpha          : Not premultiplied" || line == " * Alpha          : Absent"; }
    }
    if (!stream.eof()) throw std::runtime_error("Не удалось прочитать сведения AVIF.");
    if (!single) throw std::runtime_error("Анимированные и многокадровые AVIF пока не поддерживаются.");
    if (transform_headers != 1 || invalid_transform || (crop && !valid_crop) || (transformed && !transform_properties))
        throw std::runtime_error("Преобразование AVIF не поддерживается или не применено. Сначала сохраните фото с обычными пропорциями пикселей и применённым кадрированием в редакторе.");
    if (gainmaps && (gainmaps != 1 || !no_gainmap))
        throw std::runtime_error("AVIF с HDR gain map пока не поддерживается. Сначала преобразуйте его в SDR sRGB.");
    info.profile = profile;
    if (hdr) {
        if (invalid_numbers || depths != 1 || primaries != 1 || transfers != 1 || matrices != 1 || profiles != 1 ||
            gainmaps != 1 || !no_gainmap || alphas != 1 || !straight_alpha || !no_profile || profile || info.primaries != 9 ||
            (info.transfer != 16 && info.transfer != 18) ||
            (info.depth != 10 && info.depth != 12) || (info.matrix != 0 && info.matrix != 9))
            throw std::runtime_error("Неподдерживаемый HDR AVIF: нужны 10/12 бит, BT.2020 PQ/HLG без ICC и gain map.");
        info.hdr = info.transfer == 16 ? HdrTransfer::Pq : HdrTransfer::Hlg;
    } else if (!profile && !(srgb_transfer && srgb_primaries))
        throw std::runtime_error("Этот AVIF использует HDR или цветовое пространство без поддерживаемого профиля. Сначала преобразуйте его в sRGB.");
    return info;
}
bool validate_decoded_avif(const std::filesystem::path& log) {
    const auto info = inspect_decoded_avif(log);
    if (info.hdr != HdrTransfer::None) throw std::runtime_error("Для HDR AVIF нужно тональное преобразование в SDR.");
    return info.profile;
}
}
