#include "image_converter.h"
#include "diagnostics.h"
#include "policy.h"
#include "file_policy.h"
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cwctype>
#include <vector>
#include <stdexcept>
namespace shiftwheel {
namespace {
namespace fs = std::filesystem;
using Microsoft::WRL::ComPtr;
void require_hr(HRESULT value, const char* message) {
    if (FAILED(value)) {
        const auto code = HRESULT_FACILITY(value)==FACILITY_WIN32 ? HRESULT_CODE(value) : value==STG_E_MEDIUMFULL ? ERROR_DISK_FULL : value==STG_E_ACCESSDENIED ? ERROR_ACCESS_DENIED : ERROR_GEN_FAILURE;
        throw std::runtime_error(file_error(code,message));
    }
}
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
// WIC writes through an exclusively created handle: it can never truncate an
// existing temporary file, unlike opening an output stream by filename.
class FileStream final : public IStream {
public:
    explicit FileStream(HANDLE file) : file_(file) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (id == IID_IUnknown || id == IID_ISequentialStream || id == IID_IStream) {
            *out = static_cast<IStream*>(this); AddRef(); return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&refs_)); }
    ULONG STDMETHODCALLTYPE Release() override {
        auto count = InterlockedDecrement(&refs_); if (!count) delete this; return static_cast<ULONG>(count);
    }
    HRESULT STDMETHODCALLTYPE Read(void* data, ULONG size, ULONG* read) override {
        DWORD count = 0;
        if (!ReadFile(file_, data, size, &count, nullptr)) return HRESULT_FROM_WIN32(GetLastError());
        if (read) *read = count;
        return count == size ? S_OK : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Write(const void* data, ULONG size, ULONG* written) override {
        DWORD count = 0;
        if (!WriteFile(file_, data, size, &count, nullptr)) return HRESULT_FROM_WIN32(GetLastError());
        if (written) *written = count;
        return count == size ? S_OK : STG_E_WRITEFAULT;
    }
    HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER move, DWORD origin, ULARGE_INTEGER* position) override {
        if (origin > STREAM_SEEK_END) return STG_E_INVALIDFUNCTION;
        LARGE_INTEGER actual{};
        if (!SetFilePointerEx(file_, move, &actual, origin)) return HRESULT_FROM_WIN32(GetLastError());
        if (position) position->QuadPart = static_cast<ULONGLONG>(actual.QuadPart);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER size) override {
        LARGE_INTEGER zero{}, saved{}, end{}; end.QuadPart = static_cast<LONGLONG>(size.QuadPart);
        if (!SetFilePointerEx(file_, zero, &saved, FILE_CURRENT) || !SetFilePointerEx(file_, end, nullptr, FILE_BEGIN) || !SetEndOfFile(file_)) return HRESULT_FROM_WIN32(GetLastError());
        return SetFilePointerEx(file_, saved, nullptr, FILE_BEGIN) ? S_OK : HRESULT_FROM_WIN32(GetLastError());
    }
    HRESULT STDMETHODCALLTYPE CopyTo(IStream*, ULARGE_INTEGER, ULARGE_INTEGER*, ULARGE_INTEGER*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Commit(DWORD) override { return FlushFileBuffers(file_) ? S_OK : HRESULT_FROM_WIN32(GetLastError()); }
    HRESULT STDMETHODCALLTYPE Revert() override { return STG_E_INVALIDFUNCTION; }
    HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return STG_E_INVALIDFUNCTION; }
    HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return STG_E_INVALIDFUNCTION; }
    HRESULT STDMETHODCALLTYPE Stat(STATSTG* stat, DWORD) override {
        if (!stat) return E_POINTER;
        *stat = {}; stat->type = STGTY_STREAM;
        LARGE_INTEGER size{}; if (!GetFileSizeEx(file_, &size)) return HRESULT_FROM_WIN32(GetLastError());
        stat->cbSize.QuadPart = static_cast<ULONGLONG>(size.QuadPart); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Clone(IStream**) override { return E_NOTIMPL; }
private:
    ~FileStream() { CloseHandle(file_); }
    HANDLE file_;
    LONG refs_ = 1;
};
fs::path normalized(const fs::path& value) { return fs::absolute(value).lexically_normal(); }
bool same_path(const fs::path& a, const fs::path& b) { return _wcsicmp(a.c_str(), b.c_str()) == 0; }
}
void convert_image(const fs::path& input_arg, const fs::path& output_arg, const fs::path& temporary_arg, ImageColor color, const ConversionOptions& options, unsigned orientation_override) {
    validate_options(options);
    const auto input = normalized(input_arg), output = normalized(output_arg), temporary = normalized(temporary_arg);
    if (same_path(input, output) || same_path(input, temporary) || same_path(output, temporary))
        throw std::runtime_error("Исходный, конечный и временный пути должны различаться.");
    validate_local_path(input, true); validate_local_path(output, false); validate_local_path(temporary, false);
    // Staging may be in the destination folder or its dedicated private subfolder.
    if (!same_path(output.parent_path(), temporary.parent_path()) && !same_path(output.parent_path(), temporary.parent_path().parent_path()))
        throw std::runtime_error("Временный файл должен находиться рядом с результатом.");
    if (GetFileAttributesW(output.c_str()) != INVALID_FILE_ATTRIBUTES) throw std::runtime_error("Файл результата уже существует; перезапись запрещена.");
    if (category(input) != Category::Image || category(output) != Category::Image ||
        parse_format(input.extension().wstring()) == Format::Avif || parse_format(output.extension().wstring()) == Format::Avif ||
        parse_format(input.extension().wstring()) == Format::Webp || parse_format(output.extension().wstring()) == Format::Webp)
        throw std::runtime_error("Встроенный модуль изображений поддерживает PNG, JPEG, BMP и TIFF; AVIF/WebP требуют отдельного модуля.");
    auto ext = output.extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    const bool png = ext == L".png", jpeg = ext == L".jpg" || ext == L".jpeg", tiff = ext == L".tif" || ext == L".tiff";
    const bool keep_alpha = png || tiff;
    const GUID container = png ? GUID_ContainerFormatPng : jpeg ? GUID_ContainerFormatJpeg : tiff ? GUID_ContainerFormatTiff : GUID_ContainerFormatBmp;
    bool owned_temporary = false;
    try {
        {
            Handle source{CreateFileW(input.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
            if (source.value == INVALID_HANDLE_VALUE) throw std::runtime_error(file_error(GetLastError(),"Не удалось открыть исходный файл для чтения."));
            BY_HANDLE_FILE_INFORMATION info{};
            if (!GetFileInformationByHandle(source.value, &info) || (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
                throw std::runtime_error("Нужен обычный локальный файл.");
            ComPtr<IWICImagingFactory> factory;
            require_hr(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)), "Не удалось создать движок изображений");
            ComPtr<IWICBitmapDecoder> decoder;
            require_hr(factory->CreateDecoderFromFileHandle(reinterpret_cast<ULONG_PTR>(source.value), nullptr, WICDecodeMetadataCacheOnDemand, &decoder), "Не удалось прочитать изображение");
            GUID source_container{}; require_hr(decoder->GetContainerFormat(&source_container), "Не удалось определить формат");
            if (source_container != GUID_ContainerFormatPng && source_container != GUID_ContainerFormatJpeg && source_container != GUID_ContainerFormatBmp && source_container != GUID_ContainerFormatTiff)
                throw std::runtime_error("Содержимое файла не является PNG, JPEG, BMP или TIFF.");
            if (parse_format(input.extension().wstring()) == Format::Tiff && source_container != GUID_ContainerFormatTiff)
                throw std::runtime_error("Содержимое файла не соответствует TIFF.");
            UINT frames = 0; require_hr(decoder->GetFrameCount(&frames), "Не удалось прочитать кадры");
            if (frames != 1) throw std::runtime_error("Многокадровые изображения в прототипе не поддерживаются.");
            ComPtr<IWICBitmapFrameDecode> decoded; require_hr(decoder->GetFrame(0, &decoded), "Не удалось прочитать кадр");
            UINT width = 0, height = 0; require_hr(decoded->GetSize(&width, &height), "Не удалось определить размер");
            if (!width || !height || width > 32768 || height > 32768 || static_cast<ULONGLONG>(width) * height > 40000000)
                throw std::runtime_error("Лимит прототипа: 40 млн пикселей и 32768 пикселей по каждой стороне.");
            unsigned orientation = orientation_override ? orientation_override : 1;
            if (orientation > 8) throw std::runtime_error("Повреждена ориентация EXIF.");
            ComPtr<IWICMetadataQueryReader> metadata;
            if (!orientation_override && SUCCEEDED(decoded->GetMetadataQueryReader(&metadata))) {
                bool found = false;
                for (auto query : {L"/app1/ifd/{ushort=274}", L"/ifd/{ushort=274}"}) {
                    PROPVARIANT value{};
                    const HRESULT status = metadata->GetMetadataByName(query, &value);
                    const bool valid = value.vt == VT_UI2 && value.uiVal >= 1 && value.uiVal <= 8;
                    const unsigned candidate = value.uiVal;
                    PropVariantClear(&value);
                    if (SUCCEEDED(status)) {
                        if (found || !valid) throw std::runtime_error("Повреждена или неоднозначна ориентация EXIF.");
                        orientation = candidate; found = true;
                    } else if (status != WINCODEC_ERR_PROPERTYNOTFOUND && status != WINCODEC_ERR_PROPERTYNOTSUPPORTED)
                        require_hr(status, "Не удалось прочитать ориентацию EXIF");
                }
            }
            ComPtr<IWICBitmapSource> color_source = decoded;
            if (color != ImageColor::Unmanaged) {
                UINT count = 0;
                const HRESULT status = decoded->GetColorContexts(0, nullptr, &count);
                if (FAILED(status) && status != WINCODEC_ERR_UNSUPPORTEDOPERATION) require_hr(status, "Не удалось прочитать цветовой профиль");
                if (count > 16) throw std::runtime_error("Слишком много цветовых профилей изображения.");
                std::vector<ComPtr<IWICColorContext>> contexts(count);
                std::vector<IWICColorContext*> pointers(count);
                for (UINT i = 0; i < count; ++i) {
                    require_hr(factory->CreateColorContext(&contexts[i]), "Не удалось создать цветовой контекст");
                    pointers[i] = contexts[i].Get();
                }
                if (count) require_hr(decoded->GetColorContexts(count, pointers.data(), &count), "Не удалось получить цветовой профиль");
                ComPtr<IWICColorContext> source_context;
                bool has_profile = false;
                for (auto& context : contexts) {
                    WICColorContextType type{};
                    require_hr(context->GetType(&type), "Не удалось определить тип цветового профиля");
                    if (type == WICColorContextProfile) { source_context = context; has_profile = true; break; }
                    if (type == WICColorContextExifColorSpace) {
                        UINT space = 0; require_hr(context->GetExifColorSpace(&space), "Не удалось прочитать цветовое пространство");
                        if (space == 1 || space == 2) source_context = context;
                    }
                }
                if (color == ImageColor::RequireProfile && !has_profile) throw std::runtime_error("ICC-профиль изображения не удалось прочитать. Преобразование цвета отменено.");
                if (source_context) {
                    ComPtr<IWICColorContext> srgb;
                    require_hr(factory->CreateColorContext(&srgb), "Не удалось создать профиль sRGB");
                    require_hr(srgb->InitializeFromExifColorSpace(1), "Не удалось настроить sRGB");
                    ComPtr<IWICColorTransform> transform;
                    require_hr(factory->CreateColorTransformer(&transform), "Не удалось создать преобразование цвета");
                    require_hr(transform->Initialize(decoded.Get(), source_context.Get(), srgb.Get(), GUID_WICPixelFormat32bppBGRA), "Не удалось преобразовать цвет в sRGB");
                    color_source = transform;
                }
            }
            if (orientation != 1) {
                // WIC flips the source axes before rotating. EXIF 5/7 are
                // transpose/transverse, not simple quarter-turns.
                constexpr WICBitmapTransformOptions transforms[]{WICBitmapTransformRotate0,
                    WICBitmapTransformRotate0, WICBitmapTransformFlipHorizontal, WICBitmapTransformRotate180,
                    WICBitmapTransformFlipVertical,
                    static_cast<WICBitmapTransformOptions>(WICBitmapTransformRotate270 | WICBitmapTransformFlipHorizontal),
                    WICBitmapTransformRotate90,
                    static_cast<WICBitmapTransformOptions>(WICBitmapTransformRotate90 | WICBitmapTransformFlipHorizontal),
                    WICBitmapTransformRotate270};
                ComPtr<IWICBitmapFlipRotator> rotated;
                require_hr(factory->CreateBitmapFlipRotator(&rotated), "Не удалось создать поворот изображения");
                require_hr(rotated->Initialize(color_source.Get(), transforms[orientation]), "Не удалось применить ориентацию EXIF");
                color_source = rotated;
                require_hr(color_source->GetSize(&width, &height), "Не удалось определить размер после поворота");
            }
            if (options.image_max_dimension && std::max(width, height) > options.image_max_dimension) {
                const double scale = static_cast<double>(options.image_max_dimension) / std::max(width, height);
                width = std::max(1U, static_cast<UINT>(std::lround(width * scale)));
                height = std::max(1U, static_cast<UINT>(std::lround(height * scale)));
                // Interpolate premultiplied pixels so transparent colors cannot bleed
                // into visible edges; convert back to straight alpha for the encoder.
                ComPtr<IWICFormatConverter> premultiplied;
                require_hr(factory->CreateFormatConverter(&premultiplied), "Не удалось подготовить масштабирование");
                require_hr(premultiplied->Initialize(color_source.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom), "Не удалось подготовить прозрачность");
                ComPtr<IWICBitmapScaler> scaler; require_hr(factory->CreateBitmapScaler(&scaler), "Не удалось создать масштабирование");
                require_hr(scaler->Initialize(premultiplied.Get(), width, height, WICBitmapInterpolationModeFant), "Не удалось изменить размер изображения");
                color_source = scaler;
            }
            ComPtr<IWICFormatConverter> pixels; require_hr(factory->CreateFormatConverter(&pixels), "Не удалось создать конвертер пикселей");
            require_hr(pixels->Initialize(color_source.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom), "Не удалось преобразовать пиксели");
            Handle temp_handle{CreateFileW(temporary.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY, nullptr)};
            if (temp_handle.value == INVALID_HANDLE_VALUE) throw std::runtime_error("Не удалось создать новый временный файл.");
            owned_temporary = true;
            ComPtr<IStream> stream; stream.Attach(new FileStream(temp_handle.value)); temp_handle.value = INVALID_HANDLE_VALUE;
            ComPtr<IWICBitmapEncoder> encoder; require_hr(factory->CreateEncoder(container, nullptr, &encoder), "Не удалось создать энкодер");
            require_hr(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache), "Не удалось открыть выходной поток");
            ComPtr<IWICBitmapFrameEncode> frame; ComPtr<IPropertyBag2> properties;
            require_hr(encoder->CreateNewFrame(&frame, &properties), "Не удалось создать выходной кадр");
            if (jpeg) {
                PROPBAG2 option{}; option.pstrName = const_cast<wchar_t*>(L"ImageQuality");
                VARIANT quality{}; quality.vt = VT_R4;
                quality.fltVal = options.quality == QualityPreset::Compact ? .75f : options.quality == QualityPreset::High ? .97f : .90f;
                require_hr(properties->Write(1, &option, &quality), "Не удалось задать качество JPEG");
            }
            if (tiff) {
                PROPBAG2 option{}; option.pstrName = const_cast<wchar_t*>(L"TiffCompressionMethod");
                VARIANT compression{}; compression.vt = VT_UI1; compression.bVal = WICTiffCompressionZIP;
                require_hr(properties->Write(1, &option, &compression), "Не удалось задать сжатие TIFF");
            }
            require_hr(frame->Initialize(properties.Get()), "Не удалось настроить энкодер");
            require_hr(frame->SetSize(width, height), "Не удалось задать размер результата");
            double dpi_x = 96, dpi_y = 96;
            decoded->GetResolution(&dpi_x, &dpi_y);
            if (!std::isfinite(dpi_x) || !std::isfinite(dpi_y) || dpi_x <= 0 || dpi_y <= 0 || dpi_x > 10000 || dpi_y > 10000) dpi_x = dpi_y = 96;
            if (orientation >= 5) std::swap(dpi_x, dpi_y);
            require_hr(frame->SetResolution(dpi_x, dpi_y), "Не удалось задать разрешение");
            auto format = keep_alpha ? GUID_WICPixelFormat32bppBGRA : GUID_WICPixelFormat24bppBGR;
            const auto requested = format;
            require_hr(frame->SetPixelFormat(&format), "Не удалось задать формат пикселей");
            if (format != requested) throw std::runtime_error("Энкодер не поддерживает нужный формат пикселей.");
            std::vector<BYTE> row(static_cast<std::size_t>(width) * 4), flattened(static_cast<std::size_t>(width) * 3);
            for (UINT y = 0; y < height; ++y) {
                WICRect rect{0, static_cast<INT>(y), static_cast<INT>(width), 1};
                require_hr(pixels->CopyPixels(&rect, width * 4, width * 4, row.data()), "Ошибка декодирования пикселей");
                if (keep_alpha) require_hr(frame->WritePixels(1, width * 4, width * 4, row.data()), "Ошибка записи изображения с прозрачностью");
                else {
                    for (UINT x = 0; x < width; ++x) {
                        const unsigned alpha = row[x * 4 + 3];
                        for (UINT channel = 0; channel < 3; ++channel)
                            flattened[x * 3 + channel] = static_cast<BYTE>((row[x * 4 + channel] * alpha + 255 * (255 - alpha) + 127) / 255);
                    }
                    require_hr(frame->WritePixels(1, width * 3, width * 3, flattened.data()), "Ошибка записи изображения");
                }
            }
            require_hr(frame->Commit(), "Ошибка завершения кадра");
            require_hr(encoder->Commit(), "Ошибка завершения файла");
            require_hr(stream->Commit(STGC_DEFAULT), "Ошибка сохранения на диск");
        }
        if (!MoveFileExW(temporary.c_str(), output.c_str(), MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error(file_error(GetLastError(),"Не удалось сохранить результат."));
        owned_temporary = false;
    } catch (...) {
        if (owned_temporary) DeleteFileW(temporary.c_str());
        throw;
    }
}
}
