#include "avif_info.h"
#include <fstream>
#include <iostream>
#include <string>
#include <windows.h>

int main() {
    namespace fs = std::filesystem;
    auto log = fs::temp_directory_path() / (L"ShiftWheel-avif-info-" + std::to_wstring(GetCurrentProcessId()) + L".log");
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code ec; fs::remove(path, ec); } } cleanup{log};
    const std::string color = " * Color Primaries: 1\n * Transfer Char. : 13\n * ICC Profile    : Absent\n";
    auto check = [&](const std::string& block, bool accept, const char* name, const std::string& suffix = "", bool single = true) {
        { std::ofstream out(log); out << color << block << " * Progressive    : Unavailable\n" << (single ? " * Frame:\n" : "") << suffix; }
        bool accepted = false;
        try { (void)shiftwheel::validate_decoded_avif(log); accepted = true; } catch (const std::runtime_error&) {}
        if (accepted != accept) throw std::runtime_error(name);
    };
    try {
        check(" * Transformations: None\n", true, "plain");
        check(" * Transformations:\n    * irot (Rotation)      : 1\n    * imir (Mirror)        : 0 (top-to-bottom)\n", true, "rotation and mirror");
        const std::string crop = " * Transformations:\n    * clap (Clean Aperture): W: 64/1, H: 40/1, hOff: -8/1, vOff: -6/1\n      * Valid, derived crop rect: X: 8, Y: 6, W: 64, H: 40\n";
        check(crop, true, "crop");
        check(" * Transformations:\n", false, "empty transform block accepted");
        check("", false, "missing transform block accepted");
        check(" * Transformations: None\n * Transformations: None\n", false, "duplicate block");
        check(" * Transformations:\n    * future (Unknown)     : 1\n", false, "unknown property");
        check(" * Transformations:\n    * pasp (Aspect Ratio)  : 2/1\n", false, "pasp");
        check(" * Transformations:\n    * irot (Rotation)      : 4\n", false, "bad angle");
        check(" * Transformations:\n    * imir (Mirror)        : 2 (unknown)\n", false, "bad axis");
        check(" * Transformations:\n    * clap (Clean Aperture): W: 64/1\n", false, "unconfirmed crop");
        check(crop, false, "ignored transforms accepted", "Warning, ignoring invalid transforms (clap/irot/imir)\n");
        check(crop, false, "invalid clean aperture accepted", "Invalid clean aperture box\n");
        check(crop, false, "HDR accepted", " * Transfer Char. : 16\n");
        check(crop, false, "sequence accepted", "", false);
        std::cout << "PASS pinned AVIF diagnostics: transforms, malformed/unknown blocks, ignored warnings, HDR and sequences\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
