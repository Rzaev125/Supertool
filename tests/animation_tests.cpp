#include "wheel.h"
#include <iostream>
#include <stdexcept>

using namespace shiftwheel;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct Messages { int timers = 0, paints = 0; };
Messages pump(HWND window, DWORD duration) {
    Messages result;
    const auto end = GetTickCount64() + duration;
    do {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.hwnd == window) {
                if (message.message == WM_TIMER) ++result.timers;
                if (message.message == WM_PAINT) ++result.paints;
            }
            TranslateMessage(&message); DispatchMessageW(&message);
        }
        Sleep(1);
    } while (GetTickCount64() < end);
    return result;
}
BYTE opacity(HWND window) {
    BYTE alpha = 255; DWORD flags = 0;
    check(GetLayeredWindowAttributes(window, nullptr, &alpha, &flags) && (flags & LWA_ALPHA), "window must expose animated opacity");
    return alpha;
}
int main() {
    if (FAILED(OleInitialize(nullptr))) return 1;
    Microsoft::WRL::ComPtr<Wheel> wheel;
    wheel.Attach(new Wheel([] { return false; }, [](std::filesystem::path, Format) {}));
    int failed = 0;
    BOOL animations = TRUE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0);
    if (FAILED(wheel->initialize(GetModuleHandleW(nullptr)))) return 1;
    const HWND window = wheel->handle();
    auto quiet = [&] {
        const auto messages = pump(window, 100);
        check(messages.timers == 0 && messages.paints == 0, "settled wheel must not poll or repaint");
    };
    auto run = [&](const char* name, auto test) {
        try { test(); std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& error) { ++failed; std::cerr << "FAIL " << name << ": " << error.what() << '\n'; }
        wheel->hide(); pump(window, 250);
    };
    run("opening settles and visible idle is quiet", [&] {
        wheel->show({600, 400}, true);
        check(wheel->visible(), "opening is immediately interactive");
        if (animations) check(opacity(window) < 255, "opening must start translucent");
        pump(window, 300);
        check(opacity(window) == 255, "opening must finish fully opaque");
        quiet();
    });
    run("preview hover animates and stops without mouse polling", [&] {
        wheel->show({600, 400}, true); pump(window, 300);
        RECT rect{}; GetClientRect(window, &rect);
        SendMessageW(window, WM_MOUSEMOVE, 0, MAKELPARAM(rect.right / 2, rect.bottom / 6));
        const auto changes = pump(window, 250);
        check(changes.paints > 0, "preview hover must redraw");
        if (animations) check(changes.timers > 0, "hover must transition over time");
        quiet();
        SendMessageW(window, WM_MOUSELEAVE, 0, 0); pump(window, 250); quiet();
    });
    run("closing releases input immediately and then disappears", [&] {
        wheel->show({600, 400}, true); pump(window, 300);
        wheel->hide();
        check(!wheel->visible(), "closing wheel is not interactive");
        if (animations) check(IsWindowVisible(window), "closing must fade before hiding");
        check((GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TRANSPARENT) != 0, "closing window must pass input through");
        pump(window, 250);
        check(!IsWindowVisible(window), "closing must finish");
        quiet();
    });
    run("DPI notification preserves opening and hit geometry", [&] {
        wheel->show({600, 400}, true);
        RECT rect{}; GetWindowRect(window, &rect);
        const UINT dpi = GetDpiForWindow(window);
        SendMessageW(window, WM_DPICHANGED, MAKELONG(dpi, dpi), reinterpret_cast<LPARAM>(&rect));
        pump(window, 300);
        check(wheel->visible() && IsWindowVisible(window), "DPI notification must not cancel opening");
        GetClientRect(window, &rect);
        check(rect.right == MulDiv(320, dpi, 96), "wheel retains its size in DIPs");
        quiet();
    });
    run("reopening interrupts closing without delayed disappearance", [&] {
        wheel->show({600, 400}, true); pump(window, 300);
        wheel->hide(); pump(window, 30);
        wheel->show({600, 400}, true); pump(window, 300);
        check(wheel->visible() && IsWindowVisible(window) && opacity(window) == 255, "reopened wheel must remain visible");
        check(!(GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TRANSPARENT), "reopened wheel receives input");
        quiet();
    });
    wheel->shutdown(); wheel.Reset(); OleUninitialize();
    return failed ? 1 : 0;
}
