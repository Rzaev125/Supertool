#include "pdf_converter.h"
#include "engines.h"
#include "progress.h"
#include "ocr.h"
#include <wincodec.h>
#include <wrl/client.h>
#include <cmath>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace shiftwheel {
namespace fs = std::filesystem;
namespace {
// Stable PDFium C ABI, resolved only in the short-lived conversion process.
struct Pdfium {
    HMODULE dll = nullptr;
    template<class T> T load(const char* name) {
        auto result = reinterpret_cast<T>(GetProcAddress(dll, name));
        if (!result) throw std::runtime_error("Несовместимая версия PDF-модуля.");
        return result;
    }
    void (*init)() = nullptr, (*destroy)() = nullptr;
    void* (*open)(const void*, size_t, const char*) = nullptr;
    void (*close)(void*) = nullptr;
    int (*pages)(void*) = nullptr, (*security)(void*) = nullptr, (*form_type)(void*) = nullptr;
    void* (*page)(void*, int) = nullptr;
    void (*close_page)(void*) = nullptr;
    float (*width)(void*) = nullptr, (*height)(void*) = nullptr;
    void* (*bitmap)(int, int, int, void*, int) = nullptr;
    void (*close_bitmap)(void*) = nullptr;
    void (*render)(void*, void*, int, int, int, int, int, int) = nullptr;
    void* (*text_page)(void*) = nullptr;
    void (*close_text)(void*) = nullptr;
    int (*characters)(void*) = nullptr, (*text)(void*, int, int, unsigned short*) = nullptr;
    Pdfium() {
        dll = LoadLibraryExW(module_path(Category::Pdf).c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!dll) throw std::runtime_error("Не удалось загрузить PDF-модуль.");
        try {
#define API(member, name) member = load<decltype(member)>(name)
            API(init, "FPDF_InitLibrary"); API(destroy, "FPDF_DestroyLibrary");
            API(open, "FPDF_LoadMemDocument64"); API(close, "FPDF_CloseDocument");
            API(pages, "FPDF_GetPageCount"); API(security, "FPDF_GetSecurityHandlerRevision");
            API(form_type, "FPDF_GetFormType");
            API(page, "FPDF_LoadPage"); API(close_page, "FPDF_ClosePage");
            API(width, "FPDF_GetPageWidthF"); API(height, "FPDF_GetPageHeightF");
            API(bitmap, "FPDFBitmap_CreateEx"); API(close_bitmap, "FPDFBitmap_Destroy");
            API(render, "FPDF_RenderPageBitmap"); API(text_page, "FPDFText_LoadPage");
            API(close_text, "FPDFText_ClosePage"); API(characters, "FPDFText_CountChars"); API(text, "FPDFText_GetText");
#undef API
            init();
        } catch (...) { FreeLibrary(dll); dll = nullptr; throw; }
    }
    ~Pdfium() { if (dll) { destroy(); FreeLibrary(dll); } }
};
void hr(HRESULT value) { if (FAILED(value)) throw std::runtime_error("Не удалось сохранить страницу PDF."); }
bool meaningful_text(std::wstring_view text) {
    for (const wchar_t character : text)
        if (character > 0x20 && character != 0x7f && character != 0xa0 && character != 0xfeff && character < 0xfffd &&
            !(character >= 0x2000 && character <= 0x200b) && character != 0x2028 && character != 0x2029 &&
            character != 0x202f && character != 0x205f && character != 0x3000) return true;
    return false;
}
void encode(const fs::path& path, Format format, UINT width, UINT height, std::vector<BYTE>& pixels, const ConversionOptions& options) {
    using Microsoft::WRL::ComPtr;
    ComPtr<IWICImagingFactory> factory; ComPtr<IWICBitmap> bitmap; ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder; ComPtr<IWICBitmapFrameEncode> frame; ComPtr<IWICFormatConverter> converter;
    hr(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)));
    hr(factory->CreateBitmapFromMemory(width, height, GUID_WICPixelFormat32bppBGRA, width * 4, static_cast<UINT>(pixels.size()), pixels.data(), &bitmap));
    hr(factory->CreateStream(&stream)); hr(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE));
    hr(factory->CreateEncoder(format == Format::Png ? GUID_ContainerFormatPng : GUID_ContainerFormatJpeg, nullptr, &encoder));
    hr(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache));
    ComPtr<IPropertyBag2> properties;
    hr(encoder->CreateNewFrame(&frame, &properties));
    if (format == Format::Jpeg) {
        PROPBAG2 option{}; option.pstrName = const_cast<wchar_t*>(L"ImageQuality");
        VARIANT value{}; value.vt = VT_R4;
        value.fltVal = options.quality == QualityPreset::Compact ? .75f : options.quality == QualityPreset::High ? .97f : .9f;
        hr(properties->Write(1, &option, &value));
    }
    hr(frame->Initialize(properties.Get())); hr(frame->SetSize(width, height)); hr(frame->SetResolution(options.pdf_dpi, options.pdf_dpi));
    auto pixel_format = format == Format::Png ? GUID_WICPixelFormat32bppBGRA : GUID_WICPixelFormat24bppBGR;
    hr(frame->SetPixelFormat(&pixel_format)); hr(factory->CreateFormatConverter(&converter));
    hr(converter->Initialize(bitmap.Get(), pixel_format, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom));
    hr(frame->WriteSource(converter.Get(), nullptr)); hr(frame->Commit()); hr(encoder->Commit());
}
}
void convert_pdf(const fs::path& input, const fs::path& output, Format format, const ConversionOptions& options) {
    validate_options(options);
    WinHandle file(CreateFileW(input.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    LARGE_INTEGER size{};
    if (file.value == INVALID_HANDLE_VALUE || !GetFileSizeEx(file.value, &size) || size.QuadPart <= 0 || size.QuadPart > 1024LL * 1024 * 1024)
        throw std::runtime_error("PDF пуст, недоступен или больше 1 ГБ.");
    WinHandle mapping(CreateFileMappingW(file.value, nullptr, PAGE_READONLY, 0, 0, nullptr));
    void* bytes = mapping.value ? MapViewOfFile(mapping.value, FILE_MAP_READ, 0, 0, 0) : nullptr;
    if (!bytes) throw std::runtime_error("Не удалось прочитать PDF.");
    struct View { void* value; ~View() { UnmapViewOfFile(value); } } view{bytes};
    Pdfium pdf;
    using Object = std::unique_ptr<void, void(*)(void*)>;
    Object document(pdf.open(bytes, static_cast<size_t>(size.QuadPart), nullptr), pdf.close);
    if (!document || pdf.security(document.get()) >= 0) throw std::runtime_error("PDF повреждён или защищён паролем. Защищённые PDF пока не поддерживаются.");
    if (pdf.form_type(document.get()) != 0)
        throw std::runtime_error("PDF содержит интерактивную форму. Сначала сохраните её как обычный PDF без полей, чтобы значения не потерялись.");
    int count = pdf.pages(document.get());
    if (count < 1 || count > 1000) throw std::runtime_error("Поддерживается PDF от 1 до 1000 страниц.");
    const bool images = format == Format::Png || format == Format::Jpeg;
    std::ofstream text;
    if (images) fs::create_directory(output);
    else { text.open(output, std::ios::binary); if (!text) throw std::runtime_error("Не удалось создать текстовый файл."); }
    bool any_text = false, scanned_without_ocr = false;
    const auto started = GetTickCount64();
    for (int i = 0; i < count; ++i) {
        Object page(pdf.page(document.get(), i), pdf.close_page);
        if (!page) throw std::runtime_error("Не удалось прочитать страницу PDF.");
        if (images) {
            double w = std::ceil(pdf.width(page.get()) * (options.pdf_dpi / 72.0)), h = std::ceil(pdf.height(page.get()) * (options.pdf_dpi / 72.0));
            if (!std::isfinite(w) || !std::isfinite(h) || w < 1 || h < 1 || w > 32768 || h > 32768 || w * h > 40000000)
                throw std::runtime_error("Страница PDF превышает лимит 40 млн пикселей при выбранном DPI.");
            int width = static_cast<int>(w), height = static_cast<int>(h);
            std::vector<BYTE> pixels(static_cast<size_t>(width) * height * 4, 255);
            // Format 4 is BGRA. White background, annotations, no JavaScript/forms execution.
            Object bitmap(pdf.bitmap(width, height, 4, pixels.data(), width * 4), pdf.close_bitmap);
            if (!bitmap) throw std::runtime_error("Недостаточно памяти для страницы PDF.");
            pdf.render(bitmap.get(), page.get(), 0, 0, width, height, 0, 1);
            wchar_t name[40]{}; swprintf_s(name, L"page-%04d%s", i + 1, extension(format));
            encode(output / name, format, width, height, pixels, options);
        } else {
            Object page_text(pdf.text_page(page.get()), pdf.close_text);
            if (!page_text) throw std::runtime_error("Не удалось извлечь текст страницы.");
            int chars = pdf.characters(page_text.get());
            if (chars < 0 || chars > 10000000) throw std::runtime_error("Слишком много текста на странице PDF.");
            std::wstring buffer(static_cast<size_t>(chars) + 1, L'\0');
            int written = pdf.text(page_text.get(), 0, chars, reinterpret_cast<unsigned short*>(buffer.data()));
            if (written < 0 || written > chars + 1) throw std::runtime_error("Ошибка извлечения текста PDF.");
            buffer.resize(written > 0 ? written - 1 : 0);
            if (!meaningful_text(buffer)) {
                if (options.pdf_ocr) {
                    if (GetTickCount64() - started > 60ULL * 60 * 1000) throw std::runtime_error("Превышено время обработки PDF с OCR (1 час).");
                    report_progress(i * 99 / count, L"Распознавание PDF " + std::to_wstring(i + 1) + L" из " + std::to_wstring(count));
                    const double w = std::ceil(pdf.width(page.get()) * (300.0 / 72.0)), h = std::ceil(pdf.height(page.get()) * (300.0 / 72.0));
                    if (!std::isfinite(w) || !std::isfinite(h) || w < 1 || h < 1 || w > 32768 || h > 32768 || w * h > 40000000)
                        throw std::runtime_error("Страница PDF превышает лимит OCR: 40 млн пикселей при 300 DPI.");
                    const int width = static_cast<int>(w), height = static_cast<int>(h);
                    std::vector<BYTE> pixels(static_cast<size_t>(width) * height * 4, 255);
                    Object bitmap(pdf.bitmap(width, height, 4, pixels.data(), width * 4), pdf.close_bitmap);
                    if (!bitmap) throw std::runtime_error("Недостаточно памяти для распознавания страницы PDF.");
                    pdf.render(bitmap.get(), page.get(), 0, 0, width, height, 0, 1);
                    bool marks = false;
                    for (size_t pixel = 0; pixel < pixels.size() && !marks; pixel += 4)
                        marks = pixels[pixel] != 255 || pixels[pixel + 1] != 255 || pixels[pixel + 2] != 255;
                    if (marks) {
                        require_ocr(options.ocr_languages);
                        const auto work = output.parent_path() / (L"ocr-page-" + std::to_wstring(i + 1));
                        if (!CreateDirectoryW(work.c_str(), nullptr)) throw std::runtime_error("Не удалось создать временную папку страницы OCR.");
                        struct Cleanup { fs::path path; ~Cleanup() { remove_owned_tree(path); } } cleanup{work};
                        ConversionOptions raster_options = options; raster_options.pdf_dpi = 300;
                        const auto png = work / L"page.png";
                        encode(png, Format::Png, width, height, pixels, raster_options);
                        // Release the 300-DPI bitmap before starting the OCR process.
                        bitmap.reset(); std::vector<BYTE>().swap(pixels);
                        buffer = recognize_page(png, work, options.ocr_languages);
                    }
                } else scanned_without_ocr = true;
            }
            any_text = any_text || meaningful_text(buffer);
            if (i) text << "\r\n\f\r\n";
            text << utf8(buffer);
            if (!text) throw std::runtime_error("Недостаточно места для текста PDF.");
        }
        report_progress((i + 1) * 99 / count, L"Страница PDF " + std::to_wstring(i + 1) + L" из " + std::to_wstring(count));
    }
    if (!images) {
        text.close();
        if (!text) throw std::runtime_error("Не удалось записать текст PDF.");
        if (!any_text) throw std::runtime_error(scanned_without_ocr ? "В PDF нет извлекаемого текста. Включите OCR для распознавания сканов." : "В PDF не найден читаемый текст, в том числе после OCR. Страницы могут быть пустыми или скан недостаточно чёткий.");
    }
}
}
