#include "wheel.h"
#include <shlobj.h>
#include <winioctl.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <cmath>
#include <numbers>
using Microsoft::WRL::ComPtr;
using namespace shiftwheel;
namespace fs = std::filesystem;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void junction(const fs::path& link, const fs::path& target) {
    check(CreateDirectoryW(link.c_str(), nullptr) != FALSE, "create junction directory");
    HANDLE handle = CreateFileW(link.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    check(handle != INVALID_HANDLE_VALUE, "open junction directory");
    std::wstring substitute = L"\\??\\" + target.wstring(), print = target.wstring();
    struct Header { DWORD tag; WORD bytes, reserved, sub_offset, sub_bytes, print_offset, print_bytes; };
    const auto text_bytes = (substitute.size() + print.size() + 2) * sizeof(wchar_t);
    std::vector<BYTE> buffer(sizeof(Header) + text_bytes);
    auto header = reinterpret_cast<Header*>(buffer.data());
    *header = {IO_REPARSE_TAG_MOUNT_POINT, static_cast<WORD>(buffer.size() - 8), 0, 0,
        static_cast<WORD>(substitute.size() * 2), static_cast<WORD>((substitute.size() + 1) * 2), static_cast<WORD>(print.size() * 2)};
    memcpy(buffer.data() + sizeof(Header), substitute.c_str(), (substitute.size() + 1) * 2);
    memcpy(buffer.data() + sizeof(Header) + header->print_offset, print.c_str(), (print.size() + 1) * 2);
    DWORD returned = 0;
    BOOL okay = DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, buffer.data(), static_cast<DWORD>(buffer.size()), nullptr, 0, &returned, nullptr);
    DWORD error = okay ? ERROR_SUCCESS : GetLastError();
    CloseHandle(handle);
    if (!okay) RemoveDirectoryW(link.c_str());
    if (!okay) throw std::runtime_error("set junction reparse point: " + std::to_string(error));
}
ComPtr<IDataObject> file_data(const std::vector<fs::path>& paths) {
    ComPtr<IDataObject> data;
    check(SUCCEEDED(SHCreateDataObject(nullptr, 0, nullptr, nullptr, IID_PPV_ARGS(&data))), "create data object");
    std::wstring list;
    for (const auto& path : paths) { list += path.wstring(); list += L'\0'; }
    list += L'\0';
    SIZE_T size = sizeof(DROPFILES) + list.size() * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, size);
    check(memory != nullptr, "allocate CF_HDROP");
    auto header = static_cast<DROPFILES*>(GlobalLock(memory));
    header->pFiles = sizeof(DROPFILES); header->fWide = TRUE;
    memcpy(reinterpret_cast<BYTE*>(header) + sizeof(DROPFILES), list.data(), list.size() * sizeof(wchar_t));
    GlobalUnlock(memory);
    FORMATETC format{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM medium{}; medium.tymed = TYMED_HGLOBAL; medium.hGlobal = memory;
    HRESULT hr = data->SetData(&format, &medium, TRUE);
    if (FAILED(hr)) GlobalFree(memory);
    check(SUCCEEDED(hr), "set CF_HDROP");
    return data;
}
int main() {
    if (FAILED(OleInitialize(nullptr))) return 1;
    int failed = 0, submitted = 0; bool busy = false; Format chosen = Format::Bmp;
    auto root = fs::current_path() / "test-output" / (L"ole-" + std::to_wstring(GetCurrentProcessId()));
    fs::create_directories(root);
    const auto input = root / L"Фото.png"; { std::ofstream f(input); f << "path fixture; decoding tested separately"; }
    ComPtr<Wheel> wheel;
    fs::path expected_input = input;
    wheel.Attach(new Wheel([&] { return busy; }, [&](fs::path file, Format format) { check(file == expected_input, "original path preserved"); ++submitted; chosen = format; }));
    if (FAILED(wheel->initialize(GetModuleHandleW(nullptr)))) { wheel->shutdown(); wheel.Reset(); OleUninitialize(); return 1; }
    auto data = file_data({input});
    auto point = [&](bool center) {
        RECT client{}; GetClientRect(wheel->handle(), &client);
        POINT p{client.right / 2, center ? client.bottom / 2 : client.bottom / 6};
        ClientToScreen(wheel->handle(), &p); return POINTL{p.x, p.y};
    };
    auto run = [&](const char* name, auto test) {
        try { wheel->show(POINT{500, 400}); test(); std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& e) { ++failed; std::cerr << "FAIL " << name << ": " << e.what() << '\n'; }
        wheel->hide();
    };
    run("real CF_HDROP selects COPY then submits once", [&] {
        DWORD effect = DROPEFFECT_COPY | DROPEFFECT_MOVE;
        wheel->DragEnter(data.Get(), MK_SHIFT | MK_LBUTTON, point(true), &effect);
        check(wheel->has_ole_drag(), "OLE lifecycle begins before mouse-up cleanup");
        check(effect == DROPEFFECT_NONE, "neutral center");
        effect = DROPEFFECT_COPY | DROPEFFECT_MOVE;
        wheel->DragOver(MK_SHIFT | MK_LBUTTON, point(false), &effect);
        check(effect == DROPEFFECT_COPY, "COPY offered despite Shift");
        effect = DROPEFFECT_COPY | DROPEFFECT_MOVE;
        wheel->Drop(data.Get(), MK_SHIFT, point(false), &effect);
        check(effect == DROPEFFECT_COPY && submitted == 1 && chosen == Format::Jpeg, "one JPEG job");
        check(!wheel->has_ole_drag(), "Drop ends OLE lifecycle");
    });
    run("MOVE-only source is rejected", [&] {
        DWORD effect = DROPEFFECT_MOVE;
        wheel->DragEnter(data.Get(), MK_SHIFT, point(false), &effect);
        check(effect == DROPEFFECT_NONE, "no MOVE");
        effect = DROPEFFECT_MOVE;
        wheel->Drop(data.Get(), MK_SHIFT, point(false), &effect);
        check(effect == DROPEFFECT_NONE && submitted == 1, "no job for MOVE");
    });
    run("Shift release cancels drop", [&] {
        DWORD effect = DROPEFFECT_COPY;
        wheel->DragEnter(data.Get(), MK_SHIFT, point(false), &effect);
        effect = DROPEFFECT_COPY;
        wheel->Drop(data.Get(), 0, point(false), &effect);
        check(effect == DROPEFFECT_NONE && submitted == 1, "Shift required at drop time");
    });
    run("center and batch never convert", [&] {
        DWORD effect = DROPEFFECT_COPY;
        wheel->Drop(data.Get(), MK_SHIFT, point(true), &effect);
        check(effect == DROPEFFECT_NONE && submitted == 1, "center drop");
        auto batch = file_data({input, input}); effect = DROPEFFECT_COPY;
        wheel->DragEnter(batch.Get(), MK_SHIFT, point(false), &effect);
        check(effect == DROPEFFECT_NONE, "batch rejected");
    });
    run("busy and preview reject conversion", [&] {
        busy = true; DWORD effect = DROPEFFECT_COPY;
        wheel->Drop(data.Get(), MK_SHIFT, point(false), &effect);
        check(effect == DROPEFFECT_NONE && submitted == 1, "busy rejected");
        busy = false; wheel->show(POINT{500, 400}, true); effect = DROPEFFECT_COPY;
        wheel->Drop(data.Get(), MK_SHIFT, point(false), &effect);
        check(effect == DROPEFFECT_NONE && submitted == 1, "preview rejected");
    });
    run("ancestor junction is rejected before COPY", [&] {
        auto alias = root / "alias";
        junction(alias, root);
        struct Cleanup { fs::path path; ~Cleanup() { RemoveDirectoryW(path.c_str()); } } cleanup{alias};
        auto linked = file_data({alias / input.filename()});
        DWORD effect = DROPEFFECT_COPY;
        wheel->DragEnter(linked.Get(), MK_SHIFT, point(false), &effect);
        check(effect == DROPEFFECT_NONE, "ancestor junction must not be admitted");
    });
    run("dynamic video and spreadsheet sectors", [&] {
        for (auto pair : {std::pair{L"video.mp4", Format::Mp4}, std::pair{L"sheets.xlsx", Format::Pdf}}) {
            expected_input = root / pair.first; { std::ofstream f(expected_input); f << "fixture"; }
            auto dynamic = file_data({expected_input}); DWORD effect = DROPEFFECT_COPY;
            wheel->show(POINT{500, 400});
            wheel->DragEnter(dynamic.Get(), MK_SHIFT, point(false), &effect);
            check(effect == DROPEFFECT_COPY, "category admitted");
            effect = DROPEFFECT_COPY; wheel->Drop(dynamic.Get(), MK_SHIFT, point(false), &effect);
            check(effect == DROPEFFECT_COPY && chosen == pair.second, "route from category, not enum index");
        }
        expected_input = input;
    });
    run("all image audio and video sectors submit their displayed format", [&] {
        for (auto sample : {L"six.png", L"seven.wav", L"six.mp4"}) {
            expected_input = root / sample; { std::ofstream f(expected_input); f << "fixture"; }
            auto dynamic = file_data({expected_input});
            auto formats = formats_for(expected_input);
            for (size_t i = 0; i < formats.size(); ++i) {
                wheel->show(POINT{500, 400});
                RECT rect{}; GetClientRect(wheel->handle(), &rect);
                const double angle = i * 2 * std::numbers::pi / formats.size();
                POINT p{static_cast<LONG>(rect.right * (.5 + std::sin(angle) / 3)),
                    static_cast<LONG>(rect.bottom * (.5 - std::cos(angle) / 3))};
                ClientToScreen(wheel->handle(), &p); POINTL sector{p.x, p.y};
                int before = submitted; DWORD effect = DROPEFFECT_COPY;
                wheel->DragEnter(dynamic.Get(), MK_SHIFT, sector, &effect);
                check(effect == DROPEFFECT_COPY, "sector accepts COPY");
                effect = DROPEFFECT_COPY;
                wheel->Drop(dynamic.Get(), MK_SHIFT, sector, &effect);
                check(effect == DROPEFFECT_COPY && submitted == before + 1 && chosen == formats[i], "sector route matches label");
            }
        }
        expected_input = input;
    });
    run("changed data object cannot change selected route", [&] {
        auto different = file_data({root / L"sheets.xlsx"}); DWORD effect = DROPEFFECT_COPY; int before = submitted;
        wheel->DragEnter(data.Get(), MK_SHIFT, point(false), &effect);
        effect = DROPEFFECT_COPY; wheel->Drop(different.Get(), MK_SHIFT, point(false), &effect);
        check(effect == DROPEFFECT_NONE && submitted == before, "different file rejected at drop");
    });
    run("cancelled wheel rejects reentry and late drop", [&] {
        int before = submitted;
        DWORD effect = DROPEFFECT_COPY;
        wheel->DragEnter(data.Get(), MK_SHIFT, point(false), &effect);
        wheel->hide();
        effect = DROPEFFECT_COPY;
        wheel->DragEnter(data.Get(), MK_SHIFT, point(false), &effect);
        check(effect == DROPEFFECT_NONE && !wheel->has_ole_drag(), "closing wheel must reject reentry");
        effect = DROPEFFECT_COPY;
        wheel->Drop(data.Get(), MK_SHIFT, point(false), &effect);
        check(effect == DROPEFFECT_NONE && submitted == before, "cancelled wheel cannot submit");
    });
    run("configured order and single visible format select their labels", [&] {
        wheel->set_format_provider([](const fs::path&) { return std::vector<Format>{Format::Avif, Format::Webp}; });
        DWORD effect = DROPEFFECT_COPY;
        wheel->DragEnter(data.Get(), MK_SHIFT, point(false), &effect);
        effect = DROPEFFECT_COPY; wheel->Drop(data.Get(), MK_SHIFT, point(false), &effect);
        check(effect == DROPEFFECT_COPY && chosen == Format::Avif, "configured first sector chosen");
        wheel->set_format_provider([](const fs::path&) { return std::vector<Format>{Format::Webp}; });
        wheel->show(POINT{500, 400}); effect = DROPEFFECT_COPY;
        wheel->DragEnter(data.Get(), MK_SHIFT, point(false), &effect);
        UpdateWindow(wheel->handle());
        effect = DROPEFFECT_COPY; wheel->Drop(data.Get(), MK_SHIFT, point(false), &effect);
        check(effect == DROPEFFECT_COPY && chosen == Format::Webp, "single visible sector works");
        wheel->set_format_provider({});
    });
    run("mixed batch uses shared formats and submits once", [&] {
        wheel->set_format_provider({});
        const auto pdf = root / L"pages.pdf"; { std::ofstream f(pdf); f << "fixture"; }
        const std::vector<fs::path> inputs{input, pdf};
        int batches = 0;
        wheel->set_batch_handler([&](std::vector<fs::path> files, Format format) {
            check(files == inputs, "ordered batch preserved"); check(format == Format::Jpeg, "shared JPEG route"); ++batches;
        });
        auto batch = file_data(inputs); DWORD effect = DROPEFFECT_COPY | DROPEFFECT_MOVE;
        wheel->DragEnter(batch.Get(), MK_SHIFT, point(false), &effect);
        check(effect == DROPEFFECT_COPY, "compatible batch accepted");
        effect = DROPEFFECT_COPY; wheel->Drop(batch.Get(), MK_SHIFT, point(false), &effect);
        check(effect == DROPEFFECT_COPY && batches == 1, "one atomic batch callback");
        wheel->show(POINT{500, 400}); effect = DROPEFFECT_COPY;
        wheel->DragEnter(batch.Get(), MK_SHIFT, point(false), &effect);
        effect = DROPEFFECT_COPY; wheel->Drop(data.Get(), MK_SHIFT, point(false), &effect);
        check(effect == DROPEFFECT_NONE && batches == 1, "changed batch rejected");
        wheel->set_batch_handler({});
    });
    run("batch refuses incompatible, oversized and failing admissions", [&] {
        int batches = 0;
        wheel->set_batch_handler([&](std::vector<fs::path>, Format) { ++batches; throw std::runtime_error("queue full"); });
        const auto audio = root / L"sound.wav"; { std::ofstream f(audio); f << "fixture"; }
        for (const auto& files : {std::vector<fs::path>{input, audio}, std::vector<fs::path>(257, input)}) {
            wheel->show(POINT{500, 400}); auto batch = file_data(files); DWORD effect = DROPEFFECT_COPY;
            wheel->DragEnter(batch.Get(), MK_SHIFT, point(false), &effect);
            check(effect == DROPEFFECT_NONE, "invalid batch not advertised as copy");
            effect = DROPEFFECT_COPY; wheel->Drop(batch.Get(), MK_SHIFT, point(false), &effect);
            check(effect == DROPEFFECT_NONE && batches == 0, "invalid batch never submitted");
        }
        wheel->show(POINT{500, 400}); DWORD effect = DROPEFFECT_COPY;
        wheel->DragEnter(data.Get(), MK_SHIFT, point(false), &effect);
        effect = DROPEFFECT_COPY;
        check(FAILED(wheel->Drop(data.Get(), MK_SHIFT, point(false), &effect)) && effect == DROPEFFECT_NONE && batches == 1, "failed admission never claims copy");
        wheel->set_batch_handler({});
    });
    run("input is revalidated at drop", [&] {
        int before = submitted;
        DWORD effect = DROPEFFECT_COPY;
        wheel->DragEnter(data.Get(), MK_SHIFT, point(false), &effect);
        fs::rename(input, root / "removed.png"); effect = DROPEFFECT_COPY;
        wheel->Drop(data.Get(), MK_SHIFT, point(false), &effect);
        check(effect == DROPEFFECT_NONE && submitted == before, "removed source rejected");
    });
    data.Reset(); wheel->shutdown(); wheel.Reset(); OleUninitialize();
    return failed ? 1 : 0;
}
