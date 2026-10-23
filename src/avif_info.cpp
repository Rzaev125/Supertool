#include "avif_info.h"
#include <fstream>
#include <stdexcept>

namespace shiftwheel {
// avifdec 1.4.2 has no JSON output. Check complete, pinned diagnostic lines,
// never substrings of the input filename or metadata. Fail closed if changed.
bool validate_decoded_avif(const std::filesystem::path& log) {
    std::ifstream stream(log);
    bool single = false, srgb_transfer = false, srgb_primaries = false, profile = false, hdr = false;
    bool transforms = false, transformed = false, invalid_transform = false, crop = false, valid_crop = false;
    unsigned transform_headers = 0, transform_properties = 0;
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
    }
    if (!stream.eof()) throw std::runtime_error("Не удалось прочитать сведения AVIF.");
    if (!single) throw std::runtime_error("Анимированные и многокадровые AVIF пока не поддерживаются.");
    if (transform_headers != 1 || invalid_transform || (crop && !valid_crop) || (transformed && !transform_properties))
        throw std::runtime_error("Преобразование AVIF не поддерживается или не применено. Сначала сохраните фото с обычными пропорциями пикселей и применённым кадрированием в редакторе.");
    if (hdr || (!profile && !(srgb_transfer && srgb_primaries)))
        throw std::runtime_error("Этот AVIF использует HDR или цветовое пространство без поддерживаемого профиля. Сначала преобразуйте его в sRGB.");
    return profile;
}
}
