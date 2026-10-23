#include "completion_notice.h"
#include "panel_theme.h"
#include "resource.h"
#include <dwmapi.h>
#include <shellscalingapi.h>
#include <windowsx.h>
#include <algorithm>
#include <cmath>

namespace shiftwheel {
namespace {
constexpr int brand_id = 100, title_id = 101, message_id = 102, open_id = 103, close_id = 104;
constexpr UINT_PTR expiry_timer = 1;
constexpr UINT_PTR frame_timer = 2;
constexpr double appear_ms = 180, disappear_ms = 140;
double now_ms() {
    static const double frequency = [] { LARGE_INTEGER value; QueryPerformanceFrequency(&value); return static_cast<double>(value.QuadPart); }();
    LARGE_INTEGER value; QueryPerformanceCounter(&value);
    return static_cast<double>(value.QuadPart) * 1000.0 / frequency;
}
}
CompletionNotice::~CompletionNotice() { shutdown(); }
bool CompletionNotice::initialize(HINSTANCE instance, HWND owner, std::function<void(std::filesystem::path)> closed) {
    if (window_) return true;
    closed_ = std::move(closed);
    WNDCLASSW cls{}; cls.lpfnWndProc = window_proc; cls.hInstance = instance;
    cls.lpszClassName = L"ShiftWheel.CompletionNotice"; cls.hCursor = LoadCursorW(nullptr, IDC_ARROW); cls.style = CS_DROPSHADOW;
    if (!RegisterClassW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    window_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED, cls.lpszClassName,
        L"ShiftWheel — конвертация завершена", WS_POPUP, 0, 0, 400, 188, owner, nullptr, instance, this);
    if (!window_) return false;
    auto control = [&](int id, const wchar_t* type, const wchar_t* text, DWORD style) {
        return CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 1, 1, window_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr) != nullptr;
    };
    if (!control(brand_id, L"STATIC", L"ShiftWheel", SS_NOPREFIX) ||
        !control(title_id, L"STATIC", L"", SS_NOPREFIX | SS_ENDELLIPSIS) ||
        !control(message_id, L"STATIC", L"", SS_NOPREFIX | SS_ENDELLIPSIS) ||
        !control(open_id, L"BUTTON", L"Открыть папку", WS_TABSTOP | BS_PUSHBUTTON) ||
        !control(close_id, L"BUTTON", L"×", WS_TABSTOP | BS_PUSHBUTTON)) { shutdown(); return false; }
    const DWORD corner = 2; DwmSetWindowAttribute(window_, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
    return true;
}
void CompletionNotice::layout(UINT dpi, const RECT& work) {
    dpi_ = dpi;
    auto px = [dpi](int value) { return MulDiv(value, static_cast<int>(dpi), 96); };
    const int width = std::min(px(400), static_cast<int>(work.right - work.left) - px(24)), height = px(188);
    HFONT font = CreateFontW(-px(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HFONT title = CreateFontW(-px(16), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    auto place = [&](int id, int x, int y, int w, int h) {
        const auto child = GetDlgItem(window_, id);
        SetWindowPos(child, nullptr, px(x), px(y), w, px(h), SWP_NOACTIVATE | SWP_NOZORDER);
        SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(id == title_id ? title : font), FALSE);
    };
    place(brand_id, 56, 18, width - px(108), 24);
    place(title_id, 20, 54, width - px(40), 25);
    place(message_id, 20, 83, width - px(40), 40);
    place(open_id, 20, 134, std::min(px(180), width - px(40)), 34);
    place(close_id, MulDiv(width, 96, static_cast<int>(dpi)) - 44, 12, px(30), 30);
    if (font_) DeleteObject(font_); if (title_font_) DeleteObject(title_font_);
    font_ = font; title_font_ = title;
    SetWindowPos(window_, nullptr, work.right - width - px(12), work.bottom - height - px(12), width, height,
        SWP_NOACTIVATE | SWP_NOZORDER);
}
bool CompletionNotice::show(const TrayNotification& notification) {
    if (!window_ || visible_ || notification.folder.empty()) return false;
    try {
        MONITORINFO monitor{sizeof(monitor)};
        const auto screen = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
        if (!GetMonitorInfoW(screen, &monitor)) return false;
        UINT x = 96, y = 96; GetDpiForMonitor(screen, MDT_EFFECTIVE_DPI, &x, &y);
        if (monitor.rcWork.right - monitor.rcWork.left < MulDiv(260, static_cast<int>(x), 96) ||
            monitor.rcWork.bottom - monitor.rcWork.top < MulDiv(212, static_cast<int>(x), 96)) return false;
        folder_ = notification.folder;
        SetWindowTextW(GetDlgItem(window_, title_id), notification.title.c_str());
        SetWindowTextW(GetDlgItem(window_, message_id), notification.message.c_str());
        layout(x, monitor.rcWork);
        SendMessageW(window_, WM_CHANGEUISTATE, MAKEWPARAM(UIS_SET, UISF_HIDEFOCUS), 0);
        DWORD seconds = 10; SystemParametersInfoW(SPI_GETMESSAGEDURATION, 0, &seconds, 0);
        if (!SetTimer(window_, expiry_timer, std::clamp<DWORD>(seconds, 10, 300) * 1000, nullptr)) return false;
        SetWindowLongPtrW(window_, GWL_EXSTYLE, GetWindowLongPtrW(window_, GWL_EXSTYLE) & ~WS_EX_TRANSPARENT);
        EnableWindow(GetDlgItem(window_, open_id), TRUE); EnableWindow(GetDlgItem(window_, close_id), TRUE);
        closing_ = false; open_on_close_ = false;
        update_animation_setting();
        opacity_.snap(0); opacity_.aim(1, now_ms(), animations_ ? appear_ms : 0);
        visible_ = true;
        // Apply the first alpha before showing to avoid a stale opaque frame.
        animate();
        SetWindowPos(window_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        RedrawWindow(window_, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        return true;
    } catch (...) { hide(); return false; }
}
void CompletionNotice::hide() {
    visible_ = false; closing_ = false; open_on_close_ = false;
    if (window_) {
        KillTimer(window_, expiry_timer);
        if (animation_timer_) KillTimer(window_, animation_timer_);
        ShowWindow(window_, SW_HIDE);
    }
    animation_timer_ = 0; opacity_.snap(0);
    folder_.clear();
}
void CompletionNotice::dismiss(bool open) {
    if (!visible_ || closing_) return;
    closing_ = true; open_on_close_ = open;
    KillTimer(window_, expiry_timer);
    EnableWindow(GetDlgItem(window_, open_id), FALSE); EnableWindow(GetDlgItem(window_, close_id), FALSE);
    SetWindowLongPtrW(window_, GWL_EXSTYLE, GetWindowLongPtrW(window_, GWL_EXSTYLE) | WS_EX_TRANSPARENT);
    opacity_.aim(0, now_ms(), animations_ ? disappear_ms : 0);
    animate();
}
void CompletionNotice::finish_dismiss() {
    auto folder = open_on_close_ ? folder_ : std::filesystem::path{};
    hide();
    if (closed_) closed_(std::move(folder));
}
void CompletionNotice::update_animation_setting() {
    BOOL enabled = TRUE; SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0);
    animations_ = enabled != FALSE;
    if (!animations_) opacity_.snap(opacity_.to);
}
void CompletionNotice::animate() {
    if (!window_ || !visible_) return;
    const auto now = now_ms();
    const auto alpha = static_cast<int>(std::lround(opacity_.value(now) * 255));
    if (alpha != last_alpha_) {
        SetLayeredWindowAttributes(window_, 0, static_cast<BYTE>(alpha), LWA_ALPHA); last_alpha_ = alpha;
    }
    if (opacity_.active(now)) {
        if (!animation_timer_) {
            animation_timer_ = SetTimer(window_, frame_timer, 16, nullptr);
            if (!animation_timer_) { opacity_.snap(opacity_.to); animate(); }
        }
    } else {
        if (animation_timer_) { KillTimer(window_, animation_timer_); animation_timer_ = 0; }
        if (closing_) finish_dismiss();
    }
}
void CompletionNotice::shutdown() {
    hide();
    if (window_) { DestroyWindow(window_); window_ = nullptr; }
    last_alpha_ = -1;
    if (font_) { DeleteObject(font_); font_ = nullptr; }
    if (title_font_) { DeleteObject(title_font_); title_font_ = nullptr; }
    closed_ = {};
}
void CompletionNotice::paint(HDC dc) {
    const auto palette = panel_theme::colors();
    RECT rect{}; GetClientRect(window_, &rect);
    panel_theme::box(dc, rect, palette.surface, palette.border, MulDiv(12, static_cast<int>(dpi_), 96));
    const auto icon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_SHIFTWHEEL));
    DrawIconEx(dc, MulDiv(20, dpi_, 96), MulDiv(16, dpi_, 96), icon,
        MulDiv(24, dpi_, 96), MulDiv(24, dpi_, 96), 0, nullptr, DI_NORMAL);
}
LRESULT CALLBACK CompletionNotice::window_proc(HWND window, UINT message, WPARAM w, LPARAM l) {
    auto self = reinterpret_cast<CompletionNotice*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<CompletionNotice*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(window, message, w, l);
    const auto palette = panel_theme::colors();
    switch (message) {
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{}; const auto dc = BeginPaint(window, &paint);
        self->paint(dc); EndPaint(window, &paint); return 0;
    }
    case WM_PRINTCLIENT: self->paint(reinterpret_cast<HDC>(w)); return 0;
    case WM_CTLCOLORSTATIC:
        SetTextColor(reinterpret_cast<HDC>(w), GetDlgCtrlID(reinterpret_cast<HWND>(l)) == brand_id ? palette.accent : palette.text);
        SetBkColor(reinterpret_cast<HDC>(w), palette.surface); SetDCBrushColor(reinterpret_cast<HDC>(w), palette.surface);
        return reinterpret_cast<LRESULT>(GetStockObject(DC_BRUSH));
    case WM_NOTIFY: {
        const auto item = reinterpret_cast<NMHDR*>(l);
        if (item->code == NM_CUSTOMDRAW && (item->idFrom == open_id || item->idFrom == close_id)) {
            const auto draw = reinterpret_cast<NMCUSTOMDRAW*>(l);
            if (draw->dwDrawStage == CDDS_PREPAINT) {
                panel_theme::button(*draw, palette, self->font_, self->dpi_, item->idFrom == open_id); return CDRF_SKIPDEFAULT;
            }
        }
        break;
    }
    case WM_COMMAND:
        if (HIWORD(w) == BN_CLICKED && (LOWORD(w) == open_id || LOWORD(w) == close_id)) {
            self->dismiss(LOWORD(w) == open_id); return 0;
        }
        break;
    case WM_TIMER:
        if (self->animation_timer_ && w == self->animation_timer_) { self->animate(); return 0; }
        if (w == expiry_timer && self->visible_ && !self->closing_) {
            RECT rect{}; POINT pointer{}; GetWindowRect(window, &rect); GetCursorPos(&pointer);
            const auto focus = GetFocus();
            const bool keyboard = GetForegroundWindow() == window && (focus == window || IsChild(window, focus));
            if (!PtInRect(&rect, pointer) && !keyboard) self->dismiss(false);
        }
        return 0;
    case WM_KEYDOWN: if (w == VK_ESCAPE) { self->dismiss(false); return 0; } break;
    case WM_CLOSE: self->dismiss(false); return 0;
    case WM_SETTINGCHANGE: case WM_SYSCOLORCHANGE:
        self->update_animation_setting(); self->animate();
        RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN); return 0;
    case WM_DPICHANGED: {
        MONITORINFO monitor{sizeof(monitor)};
        if (GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTOPRIMARY), &monitor)) self->layout(HIWORD(w), monitor.rcWork);
        return 0;
    }
    }
    return DefWindowProcW(window, message, w, l);
}
}
