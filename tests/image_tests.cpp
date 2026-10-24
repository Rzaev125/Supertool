#include "image_converter.h"
#include "execution.h"
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>
#include <array>
#include <stdexcept>
using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;
namespace {
fs::path telemetry_public,telemetry_owned;
bool measured_private=false;
uint64_t measured_bytes=0;
}
// Observe the producer boundary while retaining the real native WIC implementation.
namespace shiftwheel {
void report_engine(std::string_view) noexcept {}
void report_output_size(const fs::path& path) noexcept {if(telemetry_public.empty())return;try{telemetry_owned=path;measured_private=!fs::exists(telemetry_public)&&fs::is_regular_file(path);measured_bytes=fs::file_size(path);}catch(...){measured_private=false;}}
}
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void hr(HRESULT value) { if (FAILED(value)) throw std::runtime_error("WIC test fixture failed"); }
std::vector<char> bytes(const fs::path& path) { std::ifstream f(path, std::ios::binary); return {std::istreambuf_iterator<char>(f), {}}; }
void fixture(const fs::path& path, BYTE alpha) {
    ComPtr<IWICImagingFactory> f;
    hr(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f)));
    ComPtr<IWICStream> stream; hr(f->CreateStream(&stream));
    hr(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE));
    ComPtr<IWICBitmapEncoder> encoder; hr(f->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder));
    hr(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache));
    ComPtr<IWICBitmapFrameEncode> frame; hr(encoder->CreateNewFrame(&frame, nullptr));
    hr(frame->Initialize(nullptr)); hr(frame->SetSize(16, 16));
    auto format = GUID_WICPixelFormat32bppBGRA; hr(frame->SetPixelFormat(&format));
    std::vector<BYTE> pixels(16 * 16 * 4);
    for (std::size_t i = 0; i < pixels.size(); i += 4) { pixels[i + 2] = 255; pixels[i + 3] = alpha; }
    hr(frame->WritePixels(16, 64, static_cast<UINT>(pixels.size()), pixels.data()));
    hr(frame->Commit()); hr(encoder->Commit());
}
std::array<BYTE, 4> first_pixel(const fs::path& path) {
    ComPtr<IWICImagingFactory> f;
    hr(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f)));
    ComPtr<IWICBitmapDecoder> d; hr(f->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &d));
    ComPtr<IWICBitmapFrameDecode> frame; hr(d->GetFrame(0, &frame));
    ComPtr<IWICFormatConverter> c; hr(f->CreateFormatConverter(&c));
    hr(c->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom));
    std::array<BYTE, 4> pixel{}; WICRect rect{0, 0, 1, 1};
    hr(c->CopyPixels(&rect, 4, 4, pixel.data())); return pixel;
}
int main() {
    hr(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
    auto root = fs::current_path() / "test-output" / (L"images-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    fs::create_directories(root);
    int failed = 0;
    auto run = [&](const char* name, auto test) {
        try { test(); std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& e) { ++failed; std::cerr << "FAIL " << name << ": " << e.what() << '\n'; }
    };
    const auto source = root / L"Прозрачное фото.png";
    fixture(source, 0);
    const auto original = bytes(source);
    run("image size is measured from owned temporary before publication",[&]{
        telemetry_public=root/L"telemetry.png";auto temporary=root/L"telemetry.tmp";
        shiftwheel::convert_image(source,telemetry_public,temporary);
        check(measured_private&&telemetry_owned==temporary,"WIC did not measure private result before publication");
        check(measured_bytes==fs::file_size(telemetry_public),"WIC byte measurement differs from published image");telemetry_public.clear();
    });
    run("transparent PNG becomes white JPEG and preserves original", [&] {
        auto output = root / L"Результат фото.jpg"; auto temp = root / "jpeg.tmp";
        shiftwheel::convert_image(source, output, temp);
        auto pixel = first_pixel(output);
        check(pixel[0] >= 245 && pixel[1] >= 245 && pixel[2] >= 245, "transparent background must be white");
        check(bytes(source) == original, "source changed");
        check(!fs::exists(temp), "temporary not removed after publish");
    });
    run("PNG preserves alpha", [&] {
        auto output = root / "alpha.png";
        shiftwheel::convert_image(source, output, root / "png.tmp");
        check(first_pixel(output)[3] == 0, "alpha lost");
    });
    run("BMP uses white background", [&] {
        auto output = root / "white.bmp";
        shiftwheel::convert_image(source, output, root / "bmp.tmp");
        auto p = first_pixel(output); check(p[0] == 255 && p[1] == 255 && p[2] == 255, "white background");
    });
    run("opaque pixels survive conversion", [&] {
        auto input = root / "red.png"; auto output = root / "red.jpg";
        fixture(input, 255); shiftwheel::convert_image(input, output, root / "red.tmp");
        auto p = first_pixel(output); check(p[0] < 10 && p[1] < 10 && p[2] > 245, "red pixel lost");
    });
    run("existing output and source alias are rejected", [&] {
        auto output = root / "existing.jpg"; { std::ofstream f(output); f << "do not overwrite"; }
        auto before = bytes(output); bool rejected = false;
        try { shiftwheel::convert_image(source, output, root / "exists.tmp"); } catch (...) { rejected = true; }
        check(rejected && bytes(output) == before, "existing output overwritten");
        rejected = false;
        try { shiftwheel::convert_image(source, source, root / "alias.tmp"); } catch (...) { rejected = true; }
        check(rejected && bytes(source) == original, "source alias overwritten");
    });
    run("existing temporary is never deleted", [&] {
        auto temp = root / "owned-by-other.tmp"; { std::ofstream f(temp); f << "keep me"; }
        auto before = bytes(temp); bool rejected = false;
        try { shiftwheel::convert_image(source, root / "temp-collision.jpg", temp); } catch (...) { rejected = true; }
        check(rejected && bytes(temp) == before, "foreign temporary altered");
    });
    run("corrupt input does not leave partial result", [&] {
        auto input = root / "corrupt.png"; { std::ofstream f(input); f << "not an image"; }
        auto output = root / "corrupt.jpg"; auto temp = root / "corrupt.tmp"; bool rejected = false;
        try { shiftwheel::convert_image(input, output, temp); } catch (...) { rejected = true; }
        check(rejected && !fs::exists(output) && !fs::exists(temp), "partial result left");
    });
    CoUninitialize();
    return failed == 0 ? 0 : 1;
}
