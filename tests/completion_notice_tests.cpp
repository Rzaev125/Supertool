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
struct Messages { int timers = 0, paints = 0; };
Messages pump(HWND window, DWORD milliseconds) {
    Messages events;
    const auto until = GetTickCount64() + milliseconds;
    const auto drain = [&] {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.hwnd == window) {
                if (message.message == WM_TIMER) ++events.timers;
                if (message.message == WM_PAINT) ++events.paints;
            }
            TranslateMessage(&message); DispatchMessageW(&message);
        }
    };
    do {
        drain(); Sleep(1);
    } while (GetTickCount64() < until);
    // Sleep can overrun the deadline under load. Dispatch the due timer before
    // observing the animation, rather than leaving the final tick queued.
    drain();
    return events;
}
BYTE opacity(HWND window) {
    BYTE alpha = 255; DWORD flags = 0;
    check(GetLayeredWindowAttributes(window, nullptr, &alpha, &flags) && (flags & LWA_ALPHA), "notification must expose fade opacity");
    return alpha;
}
void check_layout(HWND window) {
    RECT client{}; GetClientRect(window, &client);
    auto bounds = [&](int id) {
        RECT rect{}; const auto control = GetDlgItem(window, id); check(control != nullptr, "notification control missing");
        GetWindowRect(control, &rect); MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&rect), 2);
        check(rect.left >= 0 && rect.top >= 0 && rect.right <= client.right && rect.bottom <= client.bottom,
            "notification controls must stay inside card at every DPI");
        return rect;
    };
    for (int id : {100,101,102,103,104,105,106}) bounds(id);
    check(bounds(101).bottom <= bounds(102).top && bounds(102).bottom <= bounds(106).top &&
        bounds(106).bottom <= bounds(105).top && bounds(105).bottom <= bounds(103).top,
        "title, summary, destination and action must not overlap");
    const auto button = GetDlgItem(window, 103); const auto dc = GetDC(button);
    const auto old = SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(button, WM_GETFONT, 0, 0)));
    const auto caption = text(button); SIZE size{}; GetTextExtentPoint32W(dc, caption.c_str(), static_cast<int>(caption.size()), &size);
    SelectObject(dc, old); ReleaseDC(button, dc);
    const auto button_rect = bounds(103);
    check(size.cx < button_rect.right-button_rect.left && size.cy < button_rect.bottom-button_rect.top,
        "open-folder caption must fit at every DPI");
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
        BOOL animations = TRUE; SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0);
        check(notice.show(first) && notice.visible(), "completed output must show a notification with an action button");
        check(text(GetDlgItem(notice.handle(), 100)) == L"Supertool", "completion notification uses Supertool branding");
        check(GetForegroundWindow() == foreground, "notification must not steal focus");
        const auto initial = opacity(notice.handle());
        if (animations) check(initial < 255, "opening starts translucent");
        pump(notice.handle(), 60);
        if (animations) check(opacity(notice.handle()) > initial, "opacity must increase during opening");
        pump(notice.handle(), 250);
        check(opacity(notice.handle()) == 255, "opening settles fully opaque");
        const auto idle = pump(notice.handle(), 100);
        check(idle.timers == 0 && idle.paints == 0, "settled notification must not poll or repaint");
        auto button = GetDlgItem(notice.handle(), 103);
        check(button && text(button) == L"Открыть папку" && IsWindowEnabled(button), "native open-folder button must be visible and accessible");
        const auto folder_label = GetDlgItem(notice.handle(), 105);
        check(folder_label && text(folder_label) == first.folder.wstring(), "notification must show its actual destination folder as accessible text");
        check(text(GetDlgItem(notice.handle(), 104)) == L"Закрыть уведомление", "close button must have a meaningful accessible name");
        const auto original_dpi = GetDpiForWindow(notice.handle());
        for (UINT dpi : {96U,144U,192U}) {
            SendMessageW(notice.handle(), WM_DPICHANGED, MAKELONG(dpi,dpi), 0);
            check_layout(notice.handle());
        }
        SendMessageW(notice.handle(), WM_DPICHANGED, MAKELONG(original_dpi,original_dpi), 0);
        if (argc == 2) render(notice.handle(), argv[1]);
        RECT work{}, outer{}; MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfoW(MonitorFromWindow(notice.handle(), MONITOR_DEFAULTTOPRIMARY), &monitor); work = monitor.rcWork;
        GetWindowRect(notice.handle(), &outer);
        check(outer.left >= work.left && outer.top >= work.top && outer.right <= work.right && outer.bottom <= work.bottom, "notification must stay inside monitor work area");
        TrayNotification second{L"Вторая группа", L"готово", NIIF_INFO, L"D:\\second\\c.jpg", L"D:\\second"};
        check(!notice.show(second), "a later completion must not replace the active notification target");
        first.folder = L"D:\\changed";
        SendMessageW(button, BM_CLICK, 0, 0);
        if (animations) {
            check(notice.visible() && IsWindowVisible(notice.handle()) && actions.empty(), "closing must finish its fade before advancing notification queue");
            check((GetWindowLongPtrW(notice.handle(), GWL_EXSTYLE) & WS_EX_TRANSPARENT) != 0 && !IsWindowEnabled(button), "closing notification must release input immediately");
            check(!notice.show(second), "next notification must wait for closing fade");
            pump(notice.handle(), 40);
            check(opacity(notice.handle()) < 255, "opacity must decrease during closing");
        }
        pump(notice.handle(), 250);
        check(!notice.visible() && actions == std::vector<std::filesystem::path>{L"D:\\first"}, "button must open exactly its own original folder once");
        check(!IsWindowVisible(notice.handle()), "faded notification window must be hidden");
        const auto hidden = pump(notice.handle(), 100);
        check(hidden.timers == 0 && hidden.paints == 0, "hidden notification must have no timer or paint work");
        SendMessageW(button, BM_CLICK, 0, 0);
        check(actions.size() == 1, "late duplicate button click must not open a folder twice");
        check(notice.show(second), "next notification can be shown after the previous one finishes");
        check(text(folder_label) == second.folder.wstring(), "next notification must refresh displayed folder");
        pump(notice.handle(), 250);
        SendMessageW(GetDlgItem(notice.handle(), 104), BM_CLICK, 0, 0);
        pump(notice.handle(), 250);
        check(!notice.visible() && actions.size() == 2 && actions.back().empty(), "close button dismisses without opening Explorer");
        check(notice.show(second), "show again before timeout test");
        pump(notice.handle(), 250);
        SetWindowPos(notice.handle(), nullptr, -30000, -30000, 0, 0, SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOSIZE);
        SendMessageW(notice.handle(), WM_TIMER, 1, 0);
        pump(notice.handle(), 250);
        check(!notice.visible() && actions.size() == 3 && actions.back().empty(), "timeout must dismiss without opening a folder");
        check(notice.show(second), "show before silent shutdown");
        notice.hide();
        check(!notice.visible() && actions.size() == 3, "silent hide must not consume the queued notification");
        check(notice.show(second), "show before interrupted close"); pump(notice.handle(), 250);
        SendMessageW(notice.handle(), WM_CLOSE, 0, 0); notice.hide();
        check(notice.show(second), "reopen after silent animation cancellation"); pump(notice.handle(), 300);
        check(notice.visible() && opacity(notice.handle()) == 255 && actions.size() == (animations ? 3 : 4), "cancelled animation must not hide or activate next notification");
        notice.hide();
        check(!notice.show({L"Ошибка", L"нет результата", NIIF_ERROR, {}, {}}), "no open-folder notification without a ready output");
        notice.shutdown();
        check(!notice.handle(), "notification window released");
        for (int repeat = 0; repeat < 3; ++repeat) {
            check(notice.initialize(GetModuleHandleW(nullptr), owner, {}), "reinitialize notification instance");
            check(notice.show(second), "recreated layered window must show again");
            opacity(notice.handle()); // Every new HWND needs its own initial alpha attributes.
            notice.shutdown();
        }
        if (argc == 2) {
            check(notice.initialize(GetModuleHandleW(nullptr), owner, {}), "initialize preview");
            TrayNotification sample{L"Конвертация завершена", L"Готово: 8 из 8", NIIF_INFO,
                L"D:\\Фото\\Конвертация JPG\\DSC_0148.jpg", L"D:\\Фото\\Конвертация JPG"};
            check(notice.show(sample), "show success preview"); pump(notice.handle(), 250); render(notice.handle(), argv[1]);
            SendMessageW(notice.handle(), WM_DPICHANGED, MAKELONG(192,192), 0); check_layout(notice.handle());
            render(notice.handle(), (std::string(argv[1])+".192.bmp").c_str()); notice.hide();
            sample.title=L"Конвертация: есть ошибки"; sample.message=L"Готово: 7 из 8, ошибок: 1"; sample.type=NIIF_ERROR;
            sample.folder=L"D:\\Фото\\Съёмка в Баку\\Обработанные фотографии\\Конвертация JPG";
            check(notice.show(sample), "show partial result preview"); pump(notice.handle(), 250);
            check(text(GetDlgItem(notice.handle(),105)) == sample.folder.wstring(), "long destination remains complete for accessibility");
            render(notice.handle(), (std::string(argv[1])+".warning.bmp").c_str()); notice.shutdown();
        }
        std::cout << "PASS notification layout at 96/144/192 DPI, destination, action, animation and lifecycle\n";
    } catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; result = 1; }
    DestroyWindow(owner); return result;
}
