#include "options.h"
#include <windows.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace shiftwheel;
namespace fs = std::filesystem;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejected(F f) { bool threw = false; try { f(); } catch (const std::exception&) { threw = true; } check(threw, "invalid value accepted"); }
std::string bytes(const fs::path& path) { std::ifstream f(path, std::ios::binary); return {std::istreambuf_iterator<char>(f), {}}; }
void write(const fs::path& path, const std::string& text) { std::ofstream f(path, std::ios::binary); f << text; }
int main() {
    const auto root = fs::temp_directory_path() / (L"shiftwheel-options-" + std::to_wstring(GetCurrentProcessId()));
    fs::create_directory(root);
    struct Cleanup { fs::path p; ~Cleanup() { fs::remove_all(p); } } cleanup{root};
    int failed = 0;
    auto run = [&](const char* name, auto test) { try { test(); std::cout << "PASS " << name << '\n'; } catch (const std::exception& e) { ++failed; std::cerr << "FAIL " << name << ": " << e.what() << '\n'; } };
    run("quiet preferences preserve explicit legacy settings", [&] {
        write(root / "legacy-visible.ini", "version=1\nauto_show_progress=1\n");
        check(load_settings(root / "legacy-visible.ini").auto_show_progress, "explicit old preference overwritten");
        for(auto mode : {NotificationMode::All,NotificationMode::Errors,NotificationMode::None}) {
            Settings s; s.notifications=mode;save_settings(root/"notify.ini",s);
            check(load_settings(root/"notify.ini").notifications==mode,"notification preference lost");
        }
        write(root/"bad-notify.ini","notifications=maybe\n");rejected([&]{load_settings(root/"bad-notify.ini");});
    });
    run("update preferences roundtrip without worker leakage", [&] {
        Settings s; s.automatic_updates = false; s.update_channel = L"https://example.com/stable.txt";
        save_settings(root / "updates.ini", s); auto restored = load_settings(root / "updates.ini");
        check(!restored.automatic_updates && restored.update_channel == s.update_channel, "updates lost");
        s.update_channel = L"http://example.com/file"; rejected([&] { save_settings(root / "updates.ini", s); });
        save_options(root / "snapshot-updates.ini", {});
        check(bytes(root / "snapshot-updates.ini").find("update_channel") == std::string::npos, "global preference leaked into job");
    });
    run("missing settings retains legacy conversion defaults", [&] {
        const auto s = load_settings(root / "missing.ini");
        check(s.conversion.quality == QualityPreset::Balanced && s.conversion.resources == ResourceMode::Balanced && s.conversion.pdf_dpi == 144, "legacy defaults changed");
        check(s.conversion.image_max_dimension == 0 && s.conversion.video_max_height == 0 && s.conversion.audio_bitrate_kbps == 0, "default override");
        check(s.output_directory.empty() && !s.auto_show_progress && configured_formats(L"photo.png", s).size() == 8, "quiet default missing");
        check(s.conversion.pdf_ocr && s.conversion.ocr_languages == "eng+rus+aze", "OCR language defaults");
        rejected([&] { load_options(root / "missing-options.ini"); });
    });
    run("Unicode settings preserve options and partial wheel order", [&] {
        Settings s; s.conversion = {QualityPreset::High, ResourceMode::Economy, 1024, 720, 96, 300};
        s.output_directory = root / L"Результаты = 日本語"; s.auto_show_progress = false;
        s.wheels[Category::Image] = {Format::Webp, Format::Png}; s.wheels[Category::Pdf] = {Format::Txt};
        const auto file = root / "settings.ini"; save_settings(file, s); auto restored = load_settings(file);
        check(restored.output_directory == s.output_directory && !restored.auto_show_progress, "Unicode path or bool lost");
        check(restored.conversion.quality == QualityPreset::High && restored.conversion.resources == ResourceMode::Economy && restored.conversion.image_max_dimension == 1024 && restored.conversion.video_max_height == 720 && restored.conversion.audio_bitrate_kbps == 96 && restored.conversion.pdf_dpi == 300, "conversion options lost");
        check(configured_formats(L"x.tiff", restored) == std::vector<Format>({Format::Webp, Format::Png}), "partial wheel order lost");
        check(configured_formats(L"x.pdf", restored) == std::vector<Format>({Format::Txt}), "category order mixed");
        check(configured_formats(L"x.docx", restored).size() == 5, "unspecified category hidden");
        s.output_directory = root / "updated"; save_settings(file, s); check(load_settings(file).output_directory == s.output_directory, "atomic replacement failed");
    });
    run("invalid settings cannot replace saved file", [&] {
        const auto file = root / "protected.ini"; save_settings(file, {}); const auto before = bytes(file);
        Settings s; s.conversion.pdf_dpi = 0; rejected([&] { save_settings(file, s); });
        check(bytes(file) == before, "invalid save truncated settings");
        s = {}; s.wheels[Category::Image] = {Format::Png, Format::Png}; rejected([&] { save_settings(file, s); });
        s.wheels[Category::Image] = {Format::Mp3}; rejected([&] { save_settings(file, s); });
        s.wheels[Category::Image] = {}; rejected([&] { save_settings(file, s); });
        check(bytes(file) == before, "invalid wheel save truncated settings");
        s = {}; s.output_directory = std::wstring(12000, L'界'); rejected([&] { save_settings(file, s); });
        check(bytes(file) == before, "UTF-8 byte limit mismatch replaced settings with unreadable config");
        HANDLE locked = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        check(locked != INVALID_HANDLE_VALUE, "fixture cannot lock settings");
        rejected([&] { save_settings(file, {}); }); CloseHandle(locked);
        check(bytes(file) == before, "failed atomic replacement changed old settings");
        for (const auto& entry : fs::directory_iterator(root)) check(entry.path().filename().wstring().find(L"protected.ini.tmp-") != 0, "failed save left temporary settings");
    });
    run("strict bounded parser rejects ambiguous or injected fields", [&] {
        const auto file = root / "invalid.ini";
        for (const std::string text : {"quality=unknown\n", "quality=high\nquality=compact\n", "pdf_dpi=-1\n", "pdf_dpi=144junk\n", "pdf_dpi=999999999999999999999\n", "resources=9\n", "wheel.image=png,png\n", "wheel.audio=png\n", "wheel.image=\n", "wheel.image=png,\n", "wheel.unknown=png\n", "auto_show_progress=yes\n", "unknown=value\n", "version=7\n"}) {
            write(file, text); rejected([&] { load_settings(file); });
        }
        write(file, "output_directory=" + std::string("\xff", 1)); rejected([&] { load_settings(file); });
        write(file, std::string("quality=high\0", 13)); rejected([&] { load_settings(file); });
        write(file, std::string(65537, '#')); rejected([&] { load_settings(file); });
        write(file, "quality=compact\r\npdf_dpi=72\r\n"); check(load_settings(file).conversion.pdf_dpi == 72, "CRLF unsupported");
    });
    run("numeric and enum validation happens before engines", [&] {
        for (unsigned value : {32769U, 0xffffffffU}) { ConversionOptions o; o.image_max_dimension = value; rejected([&] { validate_options(o); }); }
        for (unsigned value : {1U, 8641U, 0xffffffffU}) { ConversionOptions o; o.video_max_height = value; rejected([&] { validate_options(o); }); }
        for (unsigned value : {1U, 31U, 321U}) { ConversionOptions o; o.audio_bitrate_kbps = value; rejected([&] { validate_options(o); }); }
        for (unsigned value : {0U, 35U, 601U}) { ConversionOptions o; o.pdf_dpi = value; rejected([&] { validate_options(o); }); }
        ConversionOptions o; o.quality = static_cast<QualityPreset>(99); rejected([&] { validate_options(o); });
        o = {}; o.resources = static_cast<ResourceMode>(99); rejected([&] { validate_options(o); });
        o = {}; o.video_acceleration = static_cast<VideoAcceleration>(99); rejected([&] { validate_options(o); });
    });
    run("worker snapshots are exclusive and strictly options only", [&] {
        const auto file = root / "snapshot.ini"; ConversionOptions o; o.quality = QualityPreset::Compact; o.image_max_dimension = 640;
        save_options(file, o); const auto before = bytes(file); auto restored = load_options(file);
        check(restored.quality == QualityPreset::Compact && restored.image_max_dimension == 640, "snapshot lost options");
        rejected([&] { save_options(file, {}); }); check(bytes(file) == before, "snapshot collision overwrote another job");
        write(root / "wrong-snapshot.ini", "output_directory=C:\\temp\n"); rejected([&] { load_options(root / "wrong-snapshot.ini"); });
    });
    run("OCR settings and presentation wheel survive snapshots", [&] {
        const auto legacy = root / "legacy-030.ini";
        write(legacy, "version=1\nwheel.image=jpg,png,bmp,avif,webp,tiff\n");
        check(configured_formats(L"photo.heic", load_settings(legacy)).size() == 7, "old all-formats wheel gains new HEIC");
        write(legacy, "version=1\npdf_ocr=1\nocr_languages=eng+rus+aze\nwheel.image=jpg,png,bmp,avif,webp,tiff\n");
        check(configured_formats(L"photo.heic", load_settings(legacy)).size() == 6, "explicit current hidden HEIC stays hidden");
        const auto file = root / "ocr.ini";
        write(file, "version=1\npdf_ocr=0\nocr_languages=rus+aze\nwheel.presentation=pdf,png,pptx\n");
        auto settings = load_settings(file); save_settings(root / "ocr-saved.ini", settings);
        const auto text = bytes(root / "ocr-saved.ini");
        check(text.find("pdf_ocr=0\n") != std::string::npos && text.find("ocr_languages=rus+aze\n") != std::string::npos, "OCR fields lost");
        check(configured_formats(L"deck.pptx", settings) == std::vector<Format>({Format::Pdf, Format::Png, parse_format(L"pptx")}), "presentation wheel lost");
        save_options(root / "ocr-snapshot.ini", settings.conversion);
        check(bytes(root / "ocr-snapshot.ini").find("ocr_languages=rus+aze\n") != std::string::npos, "OCR snapshot lost");
        for (const auto invalid : {"ocr_languages=\n", "ocr_languages=eng+eng\n", "ocr_languages=../eng\n", "ocr_languages=eng --psm 0\n", "ocr_languages=eng+fra\n", "ocr_languages=eng+\n", "pdf_ocr=yes\n"}) {
            write(file, invalid); rejected([&] { load_options(file); });
        }
    });
    run("video acceleration persists in settings and job snapshots", [&] {
        const auto file = root / "video-acceleration.ini";
        write(file, "version=1\nvideo_acceleration=cpu\n");
        auto settings = load_settings(file);
        save_settings(root / "video-saved.ini", settings);
        save_options(root / "video-snapshot.ini", settings.conversion);
        check(bytes(root / "video-saved.ini").find("video_acceleration=cpu\n") != std::string::npos, "CPU preference lost");
        check(bytes(root / "video-snapshot.ini").find("video_acceleration=cpu\n") != std::string::npos, "CPU snapshot lost");
        write(file, "version=1\n");
        save_options(root / "video-legacy.ini", load_options(file));
        check(bytes(root / "video-legacy.ini").find("video_acceleration=auto\n") != std::string::npos, "legacy settings do not enable auto");
        for (const auto text : {"video_acceleration=gpu\n", "video_acceleration=\n", "video_acceleration=auto --extra\n"}) {
            write(file, text); rejected([&] { load_options(file); });
        }
    });
    run("video codec settings and immutable worker snapshots", [&] {
        const auto file = root / "codec.ini";
        for (const auto codec : {"default", "h264", "hevc", "av1"}) {
            write(file, std::string("version=1\nvideo_codec=") + codec + "\n");
            auto settings = load_settings(file);
            const auto saved = root / (std::string(codec) + "-codec-saved.ini");
            const auto snapshot = root / (std::string(codec) + "-codec.options");
            save_settings(saved, settings); save_options(snapshot, settings.conversion);
            const auto field = std::string("video_codec=") + codec + "\n";
            check(bytes(saved).find(field) != std::string::npos && bytes(snapshot).find(field) != std::string::npos, "selected codec lost");
        }
        write(file, "version=1\n"); save_options(root / "legacy-codec.options", load_options(file));
        check(bytes(root / "legacy-codec.options").find("video_codec=default\n") != std::string::npos, "legacy codec default changed");
        for (const auto text : {"video_codec=vp9\n", "video_codec=\n", "video_codec=av1 --extra\n"}) {
            write(file, text); rejected([&] { load_options(file); });
        }
        auto invalid_codec=ConversionOptions{};invalid_codec.video_codec=static_cast<VideoCodec>(99);
        rejected([&] { validate_options(invalid_codec); });
    });
    run("resource limits bound CPU workers", [&] {
        check(resource_threads(ResourceMode::Economy) == 1 && resource_threads(ResourceMode::Balanced) == 2, "baseline thread limits changed");
        check(resource_threads(ResourceMode::Fast) >= 1 && resource_threads(ResourceMode::Fast) <= 8, "fast thread count unbounded");
        rejected([&] { resource_threads(static_cast<ResourceMode>(-1)); });
    });
    return failed ? 1 : 0;
}
