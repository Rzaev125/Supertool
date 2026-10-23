#include "policy.h"
#include <windows.h>
#include <shellapi.h>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace shiftwheel;
void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
int main() {
    int failed = 0;
    auto run = [&](const char* name, auto test) {
        try { test(); std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& e) { ++failed; std::cerr << "FAIL " << name << ": " << e.what() << '\n'; }
    };
    run("wheel selection and neutral zone", [] {
        check(sector_at(0, 0, 60, 150, 3) == -1, "center is neutral");
        check(sector_at(0, -170, 60, 150, 3) == -1, "outside is neutral");
        check(sector_at(0, -100, 60, 150, 3) == 0, "top is JPEG");
        check(sector_at(87, 50, 60, 150, 3) == 1, "lower right is PNG");
        check(sector_at(-87, 50, 60, 150, 3) == 2, "lower left is BMP");
    });
    run("copy-only drop admission", [] {
        check(can_accept(true, true, 1, true, 0, false), "valid COPY admitted");
        check(!can_accept(true, false, 1, true, 0, false), "MOVE-only source rejected");
        check(!can_accept(false, true, 1, true, 0, false), "released Shift rejected");
        check(can_accept(true, true, 2, true, 0, false), "batch admitted after route validation");
        check(!can_accept(true, true, 257, true, 0, false), "oversized batch rejected");
        check(!can_accept(true, true, 0, true, 0, false), "empty batch rejected");
        check(!can_accept(true, true, 1, false, 0, false), "unsupported rejected");
        check(!can_accept(true, true, 1, true, -1, false), "center rejected");
        check(!can_accept(true, true, 1, true, 3, false), "invalid sector rejected");
        check(!can_accept(true, true, 1, true, 0, true), "busy rejected");
    });
    run("supported extensions", [] {
        check(std::wstring(caption(Format::Png,true))==L"Первый кадр","static animation target is explicit");
        check(std::wstring(caption(Format::Gif,true))!=L"Первый кадр","GIF keeps animation");
        check(supported_input(L"Photo.PNG"), "case insensitive PNG");
        check(supported_input(L"Photo.jpeg"), "JPEG");
        check(supported_input(L"animated.gif"), "GIF input admitted");
        for (auto path : {L"report.docx", L"table.XLSX", L"report.pdf", L"clip.MP4", L"song.flac"})
            check(supported_input(path), "new category must be admitted");
    });
    run("result naming preserves source", [] {
        check(output_candidate(L"C:\\docs\\two.pdf", Format::Png) == L"C:\\docs\\two-png", "PDF pages are one folder");
        check(output_candidate(L"C:\\docs\\two.xlsx", Format::Csv, 1) == L"C:\\docs\\two-csv (1)", "all sheets folder collision");
        check(output_candidate(L"C:\\photos\\photo.png", Format::Jpeg) == L"C:\\photos\\photo.jpg", "different format");
        check(output_candidate(L"C:\\photos\\photo.PNG", Format::Png) == L"C:\\photos\\photo-converted.png", "same format never source");
        check(output_candidate(L"C:\\photos\\photo.png", Format::Jpeg, 2) == L"C:\\photos\\photo (2).jpg", "collision suffix");
    });
    run("category routes and dynamic sectors", [] {
        check(formats_for(L"v.mp4").size() == 6 && formats_for(L"s.xlsx").size() == 4, "dynamic sectors");
        check(can_accept(true, true, 1, true, 4, false, 5), "fifth sector admitted");
        check(!can_accept(true, true, 1, true, 4, false, 4), "sector out of range rejected");
        check(supports_conversion(L"v.mp4", Format::Mp3), "video audio extraction");
        check(!supports_conversion(L"p.pdf", Format::Docx), "no false OCR capability");
        check(!supports_conversion(L"s.xlsx", Format::Mp4), "incompatible route rejected");
        check(parse_format(L"WEBM") == Format::Webm, "case insensitive target IDs");
    });
    run("AVIF input and output routes", [] {
        check(category(L"photo.AVIF") == Category::Image, "AVIF is an image");
        const auto avif = parse_format(L"AVIF");
        check(std::wstring(label(avif)) == L"AVIF", "AVIF label");
        check(formats_for(L"photo.png").size() == 8, "image wheel includes HEIC");
        check(supports_conversion(L"photo.png", avif), "PNG to AVIF");
        check(supports_conversion(L"photo.avif", Format::Png), "AVIF to PNG");
        check(!supports_conversion(L"clip.mp4", avif), "video not routed to still AVIF");
        check(output_candidate(L"C:\\photos\\photo.AVIF", avif) == L"C:\\photos\\photo-converted.avif", "same format preserves source");
    });
    run("popular image and media routes", [] {
        for (auto name : {L"photo.WEBP", L"photo.TIF", L"photo.TIFF"}) {
            check(category(name) == Category::Image, "new image input");
            check(supports_conversion(name, parse_format(L"avif")), "new image to AVIF");
        }
        for (auto id : {L"webp", L"tiff", L"tif"})
            check(supports_conversion(L"photo.avif", parse_format(id)), "AVIF to new image");
        check(parse_format(L"TIFF") == parse_format(L"tif"), "TIFF alias");
        check(output_candidate(L"C:\\photos\\photo.TIFF", parse_format(L"tif")) == L"C:\\photos\\photo-converted.tiff", "TIFF alias naming");
        for (auto id : {L"opus", L"aac"}) {
            check(supports_conversion(L"audio.wav", parse_format(id)), "new audio output");
            check(!supports_conversion(L"photo.png", parse_format(id)), "incompatible image route");
        }
        check(formats_for(L"song.opus").size() == 7, "seven audio sectors");
        check(supports_conversion(L"video.mp4", parse_format(L"mov")), "MOV output");
        check(!supports_conversion(L"song.mp3", parse_format(L"mov")), "no fabricated video");
    });
    run("HEIC aliases and presentation targets", [] {
        check(category(L"photo.HEIC") == Category::Image && category(L"photo.heif") == Category::Image, "HEIC aliases accepted");
        const auto heic = parse_format(L"heic");
        check(parse_format(L"HEIF") == heic && supports_conversion(L"image.png", heic), "HEIC encode and alias");
        check(output_candidate(L"C:\\photos\\image.HEIF", heic) == L"C:\\photos\\image-converted.heic", "HEIF naming preserves source");
        for (auto source : {L"slides.PPTX", L"slides.ppt", L"slides.odp"}) {
            check(supported_input(source) && formats_for(source).size() == 5, "presentation supported");
            for (auto target : {L"pdf", L"pptx", L"odp", L"png", L"jpg"}) check(supports_conversion(source, parse_format(target)), "presentation target");
            check(folder_output(source, Format::Png) && folder_output(source, Format::Jpeg), "all-slide images are folders");
            check(!supports_conversion(source, Format::Docx), "presentation not routed to document");
        }
        check(output_candidate(L"C:\\slides\\deck.pptx", Format::Png) == L"C:\\slides\\deck-png", "slide folder naming");
    });
    run("Windows argument quoting roundtrip", [] {
        for (const auto& argument : std::vector<std::wstring>{L"", L"plain", L"C:\\a b\\", L"a\"b", L"C:\\Фото\\1.png", L"a\\\\\"b"}) {
            auto command = std::wstring(L"worker.exe ") + quote_argument(argument);
            int argc = 0;
            auto argv = CommandLineToArgvW(command.c_str(), &argc);
            bool okay = argv && argc == 2 && argument == argv[1];
            if (argv) LocalFree(argv);
            check(okay, "argument must survive CreateProcess parsing");
        }
    });
    run("negative screen coordinates, DPI thresholds and repeated gesture cancellation", [] {
        for(int threshold : {4,6,8}) {
            Gesture g;g.begin(-1900,-300,true);
            check(!g.move(-1900+threshold,-300+threshold,true,threshold,threshold),"threshold boundary stays neutral");
            check(g.move(-1900+threshold+1,-300,true,threshold,threshold),"Shift held before drag triggers");
            g.end();check(!g.active(),"mouse release ends tracking");
            for(int i=0;i<500;++i){g.begin(-100,100,true);check(!g.move(-80,120,false,threshold,threshold),"late Shift waits");check(g.move(-80,120,true,threshold,threshold),"late Shift triggers");g.shift_released();g.end();}
        }
    });
    run("gesture threshold and rearm", [] {
        Gesture g;
        g.begin(100, 100, true);
        check(!g.move(102, 102, true, 4, 4), "click not drag");
        check(!g.move(120, 120, false, 4, 4), "Shift required");
        check(g.move(120, 120, true, 4, 4), "Shift during drag triggers");
        check(!g.move(125, 125, true, 4, 4), "only once per hold");
        g.shift_released();
        check(g.move(125, 125, true, 4, 4), "new Shift can reopen");
        g.end();
        check(!g.move(125, 125, true, 4, 4), "release ends gesture");
        g.begin(0, 0, false);
        check(!g.move(20, 20, true, 4, 4), "unsupported source ignored");
    });
    return failed == 0 ? 0 : 1;
}
