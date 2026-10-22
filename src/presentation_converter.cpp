#include "presentation_converter.h"
#include "engines.h"
#include "pdf_converter.h"
#include "progress.h"
#include <shlwapi.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <set>
#include <stdexcept>

namespace shiftwheel {
namespace fs = std::filesystem;
namespace {
[[noreturn]] void bad_container() {
    throw std::runtime_error("Презентация повреждена, защищена паролем или её содержимое не соответствует расширению.");
}
uint16_t u16(const unsigned char* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t u32(const unsigned char* p) { return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24); }
std::string lower(std::string value) {
    for (auto& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}
std::vector<unsigned char> read_at(std::ifstream& input, uint64_t offset, size_t count) {
    std::vector<unsigned char> bytes(count);
    input.clear(); input.seekg(static_cast<std::streamoff>(offset));
    if (count) input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(count));
    if (!input || static_cast<size_t>(input.gcount()) != count) bad_container();
    return bytes;
}
void validate_zip(const fs::path& path, bool pptx) {
    const auto size = fs::file_size(path);
    if (size < 22 || size > 1024ULL * 1024 * 1024) bad_container();
    std::ifstream input(path, std::ios::binary);
    const auto tail_size = static_cast<size_t>(std::min<uint64_t>(size, 65557));
    const auto tail = read_at(input, size - tail_size, tail_size);
    size_t end = tail.size();
    for (size_t i = tail.size() - 22;; --i) {
        if (u32(tail.data() + i) == 0x06054b50 && i + 22 + u16(tail.data() + i + 20) == tail.size()) { end = i; break; }
        if (!i) break;
    }
    if (end == tail.size()) bad_container();
    const auto* eocd = tail.data() + end;
    const uint16_t count = u16(eocd + 10);
    const uint64_t directory_size = u32(eocd + 12), directory_offset = u32(eocd + 16);
    if (u16(eocd + 4) || u16(eocd + 6) || !count || count == 0xffff || u16(eocd + 8) != count ||
        directory_size > 16 * 1024 * 1024 || directory_offset + directory_size != size - tail_size + end) bad_container();
    const auto directory = read_at(input, directory_offset, static_cast<size_t>(directory_size));
    size_t offset = 0; uint64_t expanded = 0;
    bool presentation = false, types = false, content = false, mime = false;
    std::set<std::string> names;
    for (unsigned i = 0; i < count; ++i) {
        if (offset + 46 > directory.size() || u32(directory.data() + offset) != 0x02014b50) bad_container();
        const auto* item = directory.data() + offset;
        const uint16_t flags = u16(item + 8), method = u16(item + 10), name_size = u16(item + 28), extra_size = u16(item + 30), comment_size = u16(item + 32);
        const uint32_t compressed = u32(item + 20), uncompressed = u32(item + 24), local_offset = u32(item + 42);
        const size_t next = offset + 46 + name_size + extra_size + comment_size;
        if (!name_size || next > directory.size() || (flags & 1) || (method != 0 && method != 8) || u16(item + 34)) bad_container();
        expanded += uncompressed;
        if (expanded > 1024ULL * 1024 * 1024 || static_cast<uint64_t>(local_offset) + 30 > directory_offset) bad_container();
        std::string name(reinterpret_cast<const char*>(item + 46), name_size);
        if (!names.insert(name).second || name.starts_with('/') || name.find('\\') != std::string::npos ||
            name.find('\0') != std::string::npos || name.starts_with("../") || name.find("/../") != std::string::npos) bad_container();
        const auto normalized = lower(name);
        if (normalized.find("vbaproject") != std::string::npos || normalized.starts_with("basic/") || normalized.starts_with("scripts/"))
            throw std::runtime_error("Презентации с макросами не поддерживаются. Сохраните копию без макросов.");
        const auto local = read_at(input, local_offset, 30);
        const uint16_t local_name = u16(local.data() + 26), local_extra = u16(local.data() + 28);
        if (u32(local.data()) != 0x04034b50 || u16(local.data() + 6) != flags || u16(local.data() + 8) != method || local_name != name_size) bad_container();
        const uint64_t data_offset = static_cast<uint64_t>(local_offset) + 30 + local_name + local_extra;
        if (data_offset + compressed > directory_offset) bad_container();
        const auto local_name_bytes = read_at(input, static_cast<uint64_t>(local_offset) + 30, local_name);
        if (!std::equal(local_name_bytes.begin(), local_name_bytes.end(), reinterpret_cast<const unsigned char*>(name.data()))) bad_container();
        presentation = presentation || name == "ppt/presentation.xml";
        types = types || name == "[Content_Types].xml";
        content = content || name == "content.xml";
        if (name == "mimetype") {
            constexpr std::string_view expected = "application/vnd.oasis.opendocument.presentation";
            if (method != 0 || compressed != expected.size() || uncompressed != expected.size()) bad_container();
            const auto value = read_at(input, data_offset, compressed);
            mime = std::equal(value.begin(), value.end(), reinterpret_cast<const unsigned char*>(expected.data()));
        }
        offset = next;
    }
    if (offset != directory.size() || (pptx ? !(presentation && types) : !(mime && content))) bad_container();
}
void reject_storage_macros(IStorage* storage, unsigned depth = 0) {
    if (depth > 8) bad_container();
    Microsoft::WRL::ComPtr<IEnumSTATSTG> entries;
    if (FAILED(storage->EnumElements(0, nullptr, 0, &entries))) bad_container();
    STATSTG entry{}; ULONG fetched = 0;
    while (entries->Next(1, &entry, &fetched) == S_OK) {
        const std::wstring name(entry.pwcsName ? entry.pwcsName : L""); CoTaskMemFree(entry.pwcsName);
        auto normalized = name; for (auto& c : normalized) c = static_cast<wchar_t>(towlower(c));
        if (normalized.find(L"vba") != std::wstring::npos)
            throw std::runtime_error("Презентации с макросами не поддерживаются. Сохраните копию без макросов.");
        if (entry.type == STGTY_STORAGE) {
            Microsoft::WRL::ComPtr<IStorage> child;
            if (FAILED(storage->OpenStorage(name.c_str(), nullptr, STGM_READ | STGM_SHARE_EXCLUSIVE, nullptr, 0, &child))) bad_container();
            reject_storage_macros(child.Get(), depth + 1);
        }
    }
}
void validate_presentation(const fs::path& input) {
    auto ext = input.extension().wstring(); for (auto& c : ext) c = static_cast<wchar_t>(towlower(c));
    if (ext == L".pptx" || ext == L".odp") { validate_zip(input, ext == L".pptx"); return; }
    if (ext != L".ppt" || fs::file_size(input) > 1024ULL * 1024 * 1024) bad_container();
    Microsoft::WRL::ComPtr<IStorage> storage;
    if (FAILED(StgOpenStorage(input.c_str(), nullptr, STGM_READ | STGM_SHARE_DENY_WRITE, nullptr, 0, &storage))) bad_container();
    Microsoft::WRL::ComPtr<IStream> slides;
    if (FAILED(storage->OpenStream(L"PowerPoint Document", nullptr, STGM_READ | STGM_SHARE_EXCLUSIVE, 0, &slides))) bad_container();
    reject_storage_macros(storage.Get());
}
std::wstring file_url(const fs::path& path) {
    std::wstring result(32768, L'\0'); DWORD size = static_cast<DWORD>(result.size());
    if (FAILED(UrlCreateFromPathW(path.c_str(), result.data(), &size, 0))) throw std::runtime_error("Не удалось создать профиль презентаций.");
    result.resize(size); return result;
}
}
void convert_presentation(const fs::path& input, const fs::path& output, Format format, const fs::path& work, const ConversionOptions& options) {
    validate_options(options);
    const bool images = format == Format::Png || format == Format::Jpeg;
    if (!images && format != Format::Pdf && format != Format::Pptx && format != Format::Odp)
        throw std::runtime_error("Неподдерживаемый формат презентации.");
    if (fs::exists(output)) throw std::runtime_error("Результат презентации уже существует.");
    report_progress(-1, L"Чтение презентации");
    validate_presentation(input);
    const auto profile = work / L"presentation-profile", out = work / L"presentation-output";
    if (!CreateDirectoryW(profile.c_str(), nullptr) || !CreateDirectoryW((profile / L"user").c_str(), nullptr) || !CreateDirectoryW(out.c_str(), nullptr))
        throw std::runtime_error("Не удалось подготовить приватную папку презентации.");
    std::ofstream config(profile / L"user" / L"registrymodifications.xcu", std::ios::binary);
    config << R"(<?xml version="1.0" encoding="UTF-8"?><oor:items xmlns:oor="http://openoffice.org/2001/registry"><item oor:path="/org.openoffice.Office.Common/Security/Scripting"><prop oor:name="MacroSecurityLevel" oor:op="fuse"><value>3</value></prop><prop oor:name="DisableMacrosExecution" oor:op="fuse"><value>true</value></prop><prop oor:name="BlockUntrustedRefererLinks" oor:op="fuse"><value>true</value></prop></item><item oor:path="/org.openoffice.Office.Calc/Content/Update"><prop oor:name="Link" oor:op="fuse"><value>0</value></prop></item><item oor:path="/org.openoffice.Office.Writer/Content/Update"><prop oor:name="Link" oor:op="fuse"><value>0</value></prop></item><item oor:path="/org.openoffice.Office.Jobs/Jobs/UpdateCheck/Arguments"><prop oor:name="AutoCheckEnabled" oor:op="fuse"><value>false</value></prop></item></oor:items>)";
    config.close(); if (!config) throw std::runtime_error("Не удалось записать профиль презентаций.");
    auto ext = input.extension().wstring(); for (auto& c : ext) c = static_cast<wchar_t>(towlower(c));
    const auto source = work / (L"slides" + ext);
    if (!CopyFileW(input.c_str(), source.c_str(), TRUE)) throw std::runtime_error("Не удалось подготовить презентацию.");
    const std::wstring filter = images || format == Format::Pdf
        ? LR"(pdf:impress_pdf_Export:{"ExportHiddenSlides":{"type":"boolean","value":"true"},"ExportNotesPages":{"type":"boolean","value":"false"}})"
        : format == Format::Pptx ? L"pptx:Impress MS PowerPoint 2007 XML" : L"odp:impress8";
    const std::wstring import_filter = ext == L".pptx" ? L"Impress MS PowerPoint 2007 XML" : ext == L".odp" ? L"impress8" : L"MS PowerPoint 97";
    report_progress(-1, L"Преобразование презентации");
    run_engine(module_path(Category::Presentation), {L"-env:UserInstallation=" + file_url(profile), L"--headless", L"--nologo", L"--nodefault", L"--norestore",
        L"--impress", L"--infilter=" + import_filter, L"--convert-to", filter, L"--outdir", out.wstring(), source.wstring()}, work / L"presentation.log", 120000);
    const auto result = out / (L"slides" + std::wstring(images ? L".pdf" : extension(format)));
    if (!fs::is_regular_file(result) || !fs::file_size(result))
        throw std::runtime_error("Презентация не преобразована: она повреждена, защищена паролем или не поддерживается.");
    if (images) {
        report_progress(-1, L"Сохранение слайдов в изображения");
        convert_pdf(result, output, format, options);
    } else if (!MoveFileExW(result.c_str(), output.c_str(), MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Не удалось сохранить преобразованную презентацию.");
}
}
