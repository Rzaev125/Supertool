#include "animation_converter.h"
#include "module_paths.h"
#include "converter.h"
#include "diagnostics.h"
#include "media_converter.h"
#include "engines.h"
#include "image_converter.h"
#include "pdf_converter.h"
#include "avif_converter.h"
#include "webp_converter.h"
#include "presentation_converter.h"
#include "heic_converter.h"
#include "file_policy.h"
#include "progress.h"
#include <fstream>
#include <stdexcept>
#include <shlwapi.h>

namespace shiftwheel {
namespace fs = std::filesystem;
namespace {
bool same(const fs::path& a, const fs::path& b) { return _wcsicmp(a.c_str(), b.c_str()) == 0; }
std::wstring file_url(const fs::path& path) {
    std::wstring result(32768, L'\0'); DWORD size = static_cast<DWORD>(result.size());
    if (FAILED(UrlCreateFromPathW(path.c_str(), result.data(), &size, 0))) throw std::runtime_error("Не удалось создать профиль документов.");
    result.resize(size); return result;
}
void office(const fs::path& input, const fs::path& output, Format format, const fs::path& work) {
    report_progress(-1, L"Преобразование документа");
    std::ifstream signature(input, std::ios::binary);
    unsigned char magic[8]{}; signature.read(reinterpret_cast<char*>(magic), sizeof(magic));
    auto ext = input.extension().wstring();
    for (auto& c : ext) c = static_cast<wchar_t>(towlower(c));
    bool valid = ext == L".rtf" ? memcmp(magic, "{\\rtf", 5) == 0 :
        (ext == L".doc" || ext == L".xls") ? memcmp(magic, "\xD0\xCF\x11\xE0\xA1\xB1\x1A\xE1", 8) == 0 : memcmp(magic, "PK\x03\x04", 4) == 0;
    if (!valid) throw std::runtime_error("Содержимое не соответствует формату документа.");
    auto profile = work / L"profile", out = work / L"office-output";
    fs::create_directories(profile / L"user"); fs::create_directory(out);
    // Use a new profile; never attach to the user's running office or trust its macros.
    std::ofstream config(profile / L"user" / L"registrymodifications.xcu", std::ios::binary);
    config << R"(<?xml version="1.0" encoding="UTF-8"?><oor:items xmlns:oor="http://openoffice.org/2001/registry"><item oor:path="/org.openoffice.Office.Common/Security/Scripting"><prop oor:name="MacroSecurityLevel" oor:op="fuse"><value>3</value></prop><prop oor:name="DisableMacrosExecution" oor:op="fuse"><value>true</value></prop></item><item oor:path="/org.openoffice.Office.Calc/Content/Update"><prop oor:name="Link" oor:op="fuse"><value>0</value></prop></item><item oor:path="/org.openoffice.Office.Writer/Content/Update"><prop oor:name="Link" oor:op="fuse"><value>0</value></prop></item><item oor:path="/org.openoffice.Office.Common/Misc"><prop oor:name="UseOpenCL" oor:op="fuse"><value>false</value></prop></item></oor:items>)";
    config.close(); if (!config) throw std::runtime_error("Не удалось записать профиль документов.");
    auto source = work / (L"source" + input.extension().wstring());
    if (!CopyFileW(input.c_str(), source.c_str(), TRUE)) throw std::runtime_error("Не удалось подготовить документ.");
    std::wstring filter;
    switch (format) {
    case Format::Pdf: filter = category(input) == Category::Spreadsheet ? L"pdf:calc_pdf_Export" : L"pdf:writer_pdf_Export"; break;
    case Format::Docx: filter = L"docx:Office Open XML Text"; break;
    case Format::Odt: filter = L"odt:writer8"; break;
    case Format::Rtf: filter = L"rtf:Rich Text Format"; break;
    case Format::Txt: filter = L"txt:Text (encoded):UTF8"; break;
    case Format::Xlsx: filter = L"xlsx:Calc Office Open XML"; break;
    case Format::Ods: filter = L"ods:calc8"; break;
    case Format::Csv: filter = L"csv:Text - txt - csv (StarCalc):44,34,76,1,,0,false,true,true,false,false,-1"; break;
    default: throw std::runtime_error("Неподдерживаемый формат документа.");
    }
    run_engine(module_path(category(input)), {L"-env:UserInstallation=" + file_url(profile), L"--headless", L"--nologo", L"--nodefault", L"--norestore", L"--convert-to", filter, L"--outdir", out.wstring(), source.wstring()}, work / L"engine.log", 120000);
    if (format == Format::Csv) {
        fs::create_directory(output);
        bool any = false;
        for (const auto& item : fs::directory_iterator(out)) {
            auto name = item.path().filename().wstring();
            // LibreOffice leaves a workbook lock marker after exporting all sheets.
            // Publish only CSVs, never its locks or profile files.
            if (name.starts_with(L".~lock.") && name.ends_with(L"#") && item.is_regular_file()) continue;
            if (!item.is_regular_file() || item.path().extension() != L".csv") throw std::runtime_error("Некорректный результат экспорта листов.");
            if (!MoveFileExW(item.path().c_str(), (output / item.path().filename()).c_str(), MOVEFILE_WRITE_THROUGH))
                throw std::runtime_error("Не удалось сохранить CSV листа.");
            any = true;
        }
        if (!any) throw std::runtime_error("Не удалось экспортировать листы таблицы.");
    } else {
        auto result = out / (L"source" + std::wstring(extension(format)));
        if (!fs::is_regular_file(result) || !MoveFileExW(result.c_str(), output.c_str(), MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Документ не преобразован: он повреждён, защищён паролем или не поддерживается.");
    }
}
}
void convert_file(const fs::path& input_path, const fs::path& output_path, const fs::path& temp_path, Format format, const ConversionOptions& options) {
    ModuleLease module_lease(application_directory(),false);
    validate_options(options);
    auto input = fs::absolute(input_path).lexically_normal(), output = fs::absolute(output_path).lexically_normal(), work = fs::absolute(temp_path).lexically_normal();
    if (!supports_conversion(input, format)) throw std::runtime_error("Это преобразование не поддерживается.");
    if (!folder_output(input, format) && parse_format(output.extension().wstring()) != format) throw std::runtime_error("Расширение результата не соответствует выбранному формату.");
    const bool avif = category(input) == Category::Image &&
        (format == Format::Avif || parse_format(input.extension().wstring()) == Format::Avif);
    const bool webp = category(input) == Category::Image &&
        (format == Format::Webp || parse_format(input.extension().wstring()) == Format::Webp);
    const bool heic = category(input) == Category::Image &&
        (format == Format::Heic || parse_format(input.extension().wstring()) == Format::Heic);
    const bool animation=category(input)==Category::Image&&(format==Format::Gif||animation_input(input));
    if (category(input) == Category::Image && !avif && !webp && !heic && !animation) {
        report_progress(-1, L"Преобразование изображения");
        const bool tiff = format == Format::Tiff || parse_format(input.extension().wstring()) == Format::Tiff;
        convert_image(input, output, work, tiff ? ImageColor::ToSrgb : ImageColor::Unmanaged, options); return;
    }
    validate_local_path(input, true); validate_local_path(output, false); validate_local_path(work, false);
    if (same(input, output) || same(input, work) || same(output, work) || fs::exists(output)) throw std::runtime_error("Результат уже существует или совпадает с оригиналом.");
    // Permit CLI staging beside output, or the worker's private sibling directory.
    if (!same(work.parent_path(), output.parent_path()) &&
        !(same(work.parent_path().parent_path(), output.parent_path()) && work.parent_path().filename().wstring().starts_with(L".shiftwheel-")))
        throw std::runtime_error("Временная папка должна находиться рядом с результатом.");
    require_conversion_engines(input, format);
    WinHandle source(CreateFileW(input.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    BY_HANDLE_FILE_INFORMATION info{};
    if (source.value == INVALID_HANDLE_VALUE) throw std::runtime_error(file_error(GetLastError(),"Не удалось открыть исходный файл."));
    if (!GetFileInformationByHandle(source.value, &info) || (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)))
        throw std::runtime_error("Исходный файл недоступен или занят для записи.");
    if (!CreateDirectoryW(work.c_str(), nullptr)) throw std::runtime_error(file_error(GetLastError(),"Не удалось создать временную папку."));
    struct Cleanup { fs::path path; ~Cleanup() { remove_owned_tree(path); } } cleanup{work};
    auto result = work / (L"result" + std::wstring(extension(format)));
    switch (category(input)) {
    case Category::Image:
        report_progress(-1, L"Преобразование изображения");
        if (animation) convert_animation(input,result,format,work,options);
        else if (heic) convert_heic(input, result, format, work, options);
        else if (webp) convert_webp(input, result, format, work, options);
        else convert_avif(input, result, format, work, options);
        break;
    case Category::Video: case Category::Audio: convert_media(input, result, format, work, options); break;
    case Category::Document: case Category::Spreadsheet: office(input, result, format, work); break;
    case Category::Presentation: convert_presentation(input, result, format, work, options); break;
    case Category::Pdf: convert_pdf(input, result, format, options); break;
    default: throw std::runtime_error("Неподдерживаемый тип файла.");
    }
    if (folder_output(input, format)) {
        if (!fs::is_directory(result) || fs::is_empty(result)) throw std::runtime_error("Конвертер не создал страницы или листы.");
    } else if (!fs::is_regular_file(result) || (format != Format::Txt && fs::file_size(result) == 0)) throw std::runtime_error("Конвертер не создал готовый файл.");
    validate_local_path(output, false);
    report_progress(99, L"Сохранение результата");
    if (!MoveFileExW(result.c_str(), output.c_str(), MOVEFILE_WRITE_THROUGH)) throw std::runtime_error(file_error(GetLastError(),"Не удалось сохранить результат."));
}
}
