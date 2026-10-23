#include "image_hdr.h"
#include <windows.h>
#include <wincodec.h>
#include <shlwapi.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace shiftwheel {
namespace {
using Microsoft::WRL::ComPtr;
void check(HRESULT result) {
    if (FAILED(result)) throw std::runtime_error("Не удалось преобразовать HDR-фото в SDR PNG.");
}
struct File {
    HANDLE handle;
    ~File() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
};
double hable(double x) {
    return (x * (.15 * x + .05) + .004) / (x * (.15 * x + .5) + .06) - 1.0 / 15;
}
}
void hdr_to_sdr(const std::filesystem::path& input, const std::filesystem::path& output, HdrTransfer transfer) {
    if (transfer != HdrTransfer::Pq && transfer != HdrTransfer::Hlg)
        throw std::runtime_error("Неизвестная передаточная функция HDR.");
    File source{CreateFileW(input.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
    BY_HANDLE_FILE_INFORMATION info{};
    if (source.handle == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(source.handle, &info) ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
        throw std::runtime_error("Недоступен промежуточный HDR PNG.");
    ComPtr<IWICImagingFactory> factory;
    check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)));
    ComPtr<IWICBitmapDecoder> decoder;
    check(factory->CreateDecoderFromFileHandle(reinterpret_cast<ULONG_PTR>(source.handle), nullptr, WICDecodeMetadataCacheOnDemand, &decoder));
    GUID container{}; UINT frames = 0;
    check(decoder->GetContainerFormat(&container)); check(decoder->GetFrameCount(&frames));
    if (container != GUID_ContainerFormatPng || frames != 1) throw std::runtime_error("Для HDR нужен однокадровый 16-битный PNG.");
    ComPtr<IWICBitmapFrameDecode> frame; check(decoder->GetFrame(0, &frame));
    UINT width = 0, height = 0; GUID format{};
    check(frame->GetSize(&width, &height)); check(frame->GetPixelFormat(&format));
    if (!width || !height || width > 32768 || height > 32768 || uint64_t(width) * height > 40000000)
        throw std::runtime_error("Лимит HDR: 40 млн пикселей и 32768 пикселей по каждой стороне.");
    if (format != GUID_WICPixelFormat48bppRGB && format != GUID_WICPixelFormat64bppRGBA)
        throw std::runtime_error("Декодер HDR не сохранил 16-битную точность RGB.");
    ComPtr<IWICFormatConverter> rgba; check(factory->CreateFormatConverter(&rgba));
    check(rgba->Initialize(frame.Get(), GUID_WICPixelFormat64bppRGBA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom));
    // LUTs and two row buffers bound application memory independently of height.
    std::vector<double> linear(65536);
    std::vector<BYTE> srgb(65536);
    constexpr double a = .17883277, b = 1 - 4 * a;
    const double c = .5 - a * std::log(4 * a);
    for (unsigned i = 0; i < 65536; ++i) {
        const double v = i / 65535.0;
        if (transfer == HdrTransfer::Pq) {
            const double p = std::pow(v, 32.0 / 2523);
            linear[i] = 10000 * std::pow(std::max(p - 3424.0 / 4096, 0.0) / (2413.0 / 128 - 2392.0 / 128 * p), 16384.0 / 2610);
        } else linear[i] = v <= .5 ? v * v / 3 : (std::exp((v - c) / a) + b) / 12;
        srgb[i] = static_cast<BYTE>(std::lround(255 * (v <= .0031308 ? 12.92 * v : 1.055 * std::pow(v, 1 / 2.4) - .055)));
    }
    auto rgb709 = [&](const uint16_t* p) {
        double r = linear[p[0]], g = linear[p[1]], blue = linear[p[2]];
        if (transfer == HdrTransfer::Hlg) {
            const double ootf = 1000 * std::pow(.2627 * r + .6780 * g + .0593 * blue, .2);
            r *= ootf; g *= ootf; blue *= ootf;
        }
        return std::array<double, 3>{
            std::max(0.0, 1.660491002108434 * r - .58764113878855 * g - .072849863319884 * blue) / 100,
            std::max(0.0, -.124550474521591 * r + 1.13289989712596 * g - .008349422604369 * blue) / 100,
            std::max(0.0, -.018150763354905 * r - .100578898008007 * g + 1.118729661362912 * blue) / 100};
    };
    std::vector<uint16_t> row(size_t(width) * 4);
    auto read_row = [&](UINT y) {
        WICRect rect{0, static_cast<INT>(y), static_cast<INT>(width), 1};
        check(rgba->CopyPixels(&rect, width * 8, width * 8, reinterpret_cast<BYTE*>(row.data())));
    };
    double peak = 1;
    for (UINT y = 0; y < height; ++y) {
        read_row(y);
        for (UINT x = 0; x < width; ++x) if (row[x * 4 + 3]) {
            const auto rgb = rgb709(&row[x * 4]);
            peak = std::max(peak, *std::max_element(rgb.begin(), rgb.end()));
        }
    }
    ComPtr<IStream> stream;
    // FAILIFTHERE + fCreate=TRUE is CREATE_NEW: never truncate a foreign file.
    check(SHCreateStreamOnFileEx(output.c_str(), STGM_READWRITE | STGM_SHARE_EXCLUSIVE | STGM_FAILIFTHERE,
        FILE_ATTRIBUTE_TEMPORARY, TRUE, nullptr, &stream));
    ComPtr<IWICBitmapEncoder> encoder; check(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder));
    check(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache));
    ComPtr<IWICBitmapFrameEncode> encoded; ComPtr<IPropertyBag2> properties;
    check(encoder->CreateNewFrame(&encoded, &properties)); check(encoded->Initialize(properties.Get()));
    check(encoded->SetSize(width, height));
    format = GUID_WICPixelFormat32bppBGRA; check(encoded->SetPixelFormat(&format));
    if (format != GUID_WICPixelFormat32bppBGRA) throw std::runtime_error("PNG не сохраняет прозрачность HDR.");
    std::vector<BYTE> result(size_t(width) * 4);
    const double normalization = hable(peak);
    for (UINT y = 0; y < height; ++y) {
        read_row(y);
        for (UINT x = 0; x < width; ++x) {
            const auto rgb = rgb709(&row[x * 4]);
            const double signal = *std::max_element(rgb.begin(), rgb.end());
            const double scale = signal > 0 ? hable(signal) / normalization / signal : 0;
            for (unsigned channel = 0; channel < 3; ++channel)
                result[x * 4 + (2 - channel)] = srgb[static_cast<size_t>(std::lround(std::clamp(rgb[channel] * scale, 0.0, 1.0) * 65535))];
            result[x * 4 + 3] = static_cast<BYTE>((unsigned(row[x * 4 + 3]) + 128) / 257);
        }
        check(encoded->WritePixels(1, width * 4, width * 4, result.data()));
    }
    check(encoded->Commit()); check(encoder->Commit()); check(stream->Commit(STGC_DEFAULT));
}
}
