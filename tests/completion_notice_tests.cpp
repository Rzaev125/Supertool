#include "completion_notice.h"
#include <iostream>
#include <stdexcept>
#include <vector>
#include <fstream>

using namespace shiftwheel;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
std::wstring text(HWND control) {
    std::wstring value(static_cast<size_t>(GetWindowTextLengthW(control)) + 1, L'\0');
    value.resize(static_cast<size_t>(GetWindowTextW(control, value.data(), static_cast<int>(value.size())))); return value;
}
void render(HWND window, const char* path) {
    RECT rect{}; GetClientRect(window, &rect);
    BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth = rect.right;
    info.bmiHeader.biHeight = -rect.bottom; info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
    const auto dc = CreateCompatibleDC(nullptr); void* pixels = nullptr;
    const auto bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0); check(bitmap && pixels, "render bitmap");
    const auto previous = SelectObject(dc, bitmap);
    SendMessageW(window, WM_PRINT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT | PRF_CHILDREN | PRF_ERASEBKGND); GdiFlush();
    const DWORD bytes = static_cast<DWORD>(rect.right * rect.bottom * 4);
    BITMAPFILEHEADER file{}; file.bfType = 0x4d42; file.bfOffBits = sizeof(file) + sizeof(BITMAPINFOHEADER); file.bfSize = file.bfOffBits + bytes;
    std::ofstream output(path, std::ios::binary); output.write(reinterpret_cast<const char*>(&file), sizeof(file));
    output.write(reinterpret_cast<const char*>(&info.bmiHeader), sizeof(info.bmiHeader)); output.write(static_cast<const char*>(pixels), bytes);
    SelectObject(dc, previous); DeleteObject(bitmap); DeleteDC(dc); check(static_cast<bool>(output), "render saved");
}
int main(int argc, char** argv) {
    HWND owner = CreateWindowExW(0, L"STATIC", L"notice-test", WS_OVERLAPPED, 0, 0, 20, 20, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    int result = 0;
    try {
        std::vector<std::filesystem::path> actions;
        CompletionNotice notice;
        check(notice.initialize(GetModuleHandleW(nullptr), owner, [&](auto folder) { actions.push_back(std::move(folder)); }), "initialize completion notification");
        TrayNotification first{L"Конвертация завершена", L"2 файла готовы", NIIF_INFO, L"D:\\first\\b.jpg", L"D:\\first"};
        const auto foreground = GetForegroundWindow();
        check(notice.show(first) && notice.visible(), "completed output must show a notification with an action button");
        check(GetForegroundWindow() == foreground, "notification must not steal focus");
        auto button = GetDlgItem(notice.handle(), 103);
        check(button && text(button) == L"Открыть папку" && IsWindowEnabled(button), "native open-folder button must be visible and accessible");
        if (argc == 2) render(notice.handle(), argv[1]);
        RECT work{}, outer{}; MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfoW(MonitorFromWindow(notice.handle(), MONITOR_DEFAULTTOPRIMARY), &monitor); work = monitor.rcWork;
        GetWindowRect(notice.handle(), &outer);
        check(outer.left >= work.left && outer.top >= work.top && outer.right <= work.right && outer.bottom <= work.bottom, "notification must stay inside monitor work area");
        TrayNotification second{L"Вторая группа", L"готово", NIIF_INFO, L"D:\\second\\c.jpg", L"D:\\second"};
        check(!notice.show(second), "a later completion must not replace the active notification target");
        first.folder = L"D:\\changed";
        SendMessageW(button, BM_CLICK, 0, 0);
        check(!notice.visible() && actions == std::vector<std::filesystem::path>{L"D:\\first"}, "button must open exactly its own original folder once");
        SendMessageW(button, BM_CLICK, 0, 0);
        check(actions.size() == 1, "late duplicate button click must not open a folder twice");
        check(notice.show(second), "next notification can be shown after the previous one finishes");
        SendMessageW(GetDlgItem(notice.handle(), 104), BM_CLICK, 0, 0);
        check(!notice.visible() && actions.size() == 2 && actions.back().empty(), "close button dismisses without opening Explorer");
        check(notice.show(second), "show again before timeout test");
        SetWindowPos(notice.handle(), nullptr, -30000, -30000, 0, 0, SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOSIZE);
        SendMessageW(notice.handle(), WM_TIMER, 1, 0);
        check(!notice.visible() && actions.size() == 3 && actions.back().empty(), "timeout must dismiss without opening a folder");
        check(notice.show(second), "show before silent shutdown");
        notice.hide();
        check(!notice.visible() && actions.size() == 3, "silent hide must not consume the queued notification");
        check(!notice.show({L"Ошибка", L"нет результата", NIIF_ERROR, {}, {}}), "no open-folder notification without a ready output");
        notice.shutdown();
        check(!notice.handle(), "notification window released");
        std::cout << "PASS native notification button, stable folder, focus, placement, close and timeout\n";
    } catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; result = 1; }
    DestroyWindow(owner); return result;
}
