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
constexpr int brand_id = 100, title_id = 101, message_id = 102, open_id = 103, close_id = 104,
    folder_id = 105, folder_caption_id = 106;
constexpr int card_width = 420, card_height = 236;
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
        L"Supertool — конвертация завершена", WS_POPUP, 0, 0, card_width, card_height, owner, nullptr, instance, this);
    if (!window_) return false;
    auto control = [&](int id, const wchar_t* type, const wchar_t* text, DWORD style) {
        return CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 1, 1, window_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr) != nullptr;
    };
    if (!control(brand_id, L"STATIC", L"Supertool", SS_NOPREFIX) ||
        !control(title_id, L"STATIC", L"", SS_NOPREFIX | SS_ENDELLIPSIS) ||
        !control(message_id, L"STATIC", L"", SS_NOPREFIX | SS_ENDELLIPSIS) ||
        !control(folder_caption_id, L"STATIC", L"Сохранено в папку", SS_NOPREFIX) ||
        !control(folder_id, L"STATIC", L"", SS_NOPREFIX | SS_PATHELLIPSIS) ||
        !control(open_id, L"BUTTON", L"Открыть папку", WS_TABSTOP | BS_PUSHBUTTON) ||
        !control(close_id, L"BUTTON", L"Закрыть уведомление", WS_TABSTOP | BS_PUSHBUTTON)) { shutdown(); return false; }
    const DWORD corner = 2; DwmSetWindowAttribute(window_, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
    return true;
}
void CompletionNotice::layout(UINT dpi, const RECT& work) {
    dpi_ = dpi;
    auto px = [dpi](int value) { return MulDiv(value, static_cast<int>(dpi), 96); };
    const int width = std::min(px(card_width), static_cast<int>(work.right - work.left) - px(24)), height = px(card_height);
    HFONT font = CreateFontW(-px(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HFONT title = CreateFontW(-px(18), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HFONT caption = CreateFontW(-px(12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    auto place = [&](int id, int x, int y, int w, int h) {
        const auto child = GetDlgItem(window_, id);
        SetWindowPos(child, nullptr, px(x), px(y), w, px(h), SWP_NOACTIVATE | SWP_NOZORDER);
        const auto control_font = id == title_id ? title : id == brand_id || id == folder_caption_id ? caption : font;
        SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(control_font), FALSE);
    };
    place(brand_id, 48, 19, width - px(100), 18);
    place(title_id, 70, 54, width - px(90), 26);
    place(message_id, 70, 84, width - px(90), 20);
    place(folder_caption_id, 62, 120, width - px(94), 17);
    place(folder_id, 62, 140, width - px(94), 20);
    place(open_id, 20, 180, width - px(40), 38);
    place(close_id, MulDiv(width, 96, static_cast<int>(dpi)) - 44, 12, px(30), 30);
    if (font_) DeleteObject(font_); if (title_font_) DeleteObject(title_font_); if (caption_font_) DeleteObject(caption_font_);
    font_ = font; title_font_ = title; caption_font_ = caption;
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
            monitor.rcWork.bottom - monitor.rcWork.top < MulDiv(card_height + 24, static_cast<int>(x), 96)) return false;
        folder_ = notification.folder;
        const auto icon = notification.type & NIIF_ICON_MASK;
        warning_ = icon == NIIF_ERROR || icon == NIIF_WARNING;
        SetWindowTextW(GetDlgItem(window_, title_id), notification.title.c_str());
        SetWindowTextW(GetDlgItem(window_, message_id), notification.message.c_str());
        SetWindowTextW(GetDlgItem(window_, folder_id), folder_.c_str());
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
    if (caption_font_) { DeleteObject(caption_font_); caption_font_ = nullptr; }
    closed_ = {};
}
void CompletionNotice::paint(HDC dc) {
    const auto palette = panel_theme::colors();
    const auto px = [this](int value) { return MulDiv(value, static_cast<int>(dpi_), 96); };
    RECT rect{}; GetClientRect(window_, &rect);
    panel_theme::box(dc, rect, palette.surface, palette.border, px(14));
    const auto icon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_SUPERTOOL));
    DrawIconEx(dc, px(20), px(16), icon, px(20), px(20), 0, nullptr, DI_NORMAL);
    const auto status_color = palette.high_contrast ? palette.text : warning_ ? RGB(244,199,141) : RGB(142,228,203);
    const auto status_background = palette.high_contrast ? palette.surface : warning_ ? RGB(59,48,32) : RGB(25,61,53);
    const auto saved = SaveDC(dc);
    const auto badge = CreateSolidBrush(status_background);
    SelectObject(dc, badge); SelectObject(dc, GetStockObject(NULL_PEN));
    Ellipse(dc, px(20), px(58), px(56), px(94));
    const auto stroke = CreatePen(PS_SOLID, std::max(1, px(2)), status_color);
    SelectObject(dc, stroke);
    if (warning_) {
        MoveToEx(dc, px(38), px(66), nullptr); LineTo(dc, px(38), px(79));
        MoveToEx(dc, px(38), px(83), nullptr); LineTo(dc, px(38), px(86));
    } else {
        POINT check[]{{px(30),px(76)},{px(35),px(81)},{px(46),px(70)}}; Polyline(dc, check, 3);
    }
    RestoreDC(dc, saved); DeleteObject(stroke); DeleteObject(badge);
    const RECT folder{px(20),px(112),rect.right-px(20),px(164)};
    panel_theme::box(dc, folder, palette.canvas, palette.canvas, px(8));
    const auto folder_state = SaveDC(dc);
    const auto folder_pen = CreatePen(PS_SOLID, std::max(1, px(1)), palette.muted);
    SelectObject(dc, folder_pen); SelectObject(dc, GetStockObject(NULL_BRUSH));
    POINT outline[]{{px(32),px(132)},{px(39),px(132)},{px(42),px(136)},{px(50),px(136)},
        {px(50),px(146)},{px(32),px(146)},{px(32),px(132)}};
    Polyline(dc, outline, 7);
    RestoreDC(dc, folder_state); DeleteObject(folder_pen);
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
    case WM_CTLCOLORSTATIC: {
        const auto id = GetDlgCtrlID(reinterpret_cast<HWND>(l));
        const auto background = id == folder_id || id == folder_caption_id ? palette.canvas : palette.surface;
        const auto foreground = id == brand_id || id == message_id || id == folder_caption_id ? palette.muted : palette.text;
        SetTextColor(reinterpret_cast<HDC>(w), foreground);
        SetBkColor(reinterpret_cast<HDC>(w), background); SetDCBrushColor(reinterpret_cast<HDC>(w), background);
        return reinterpret_cast<LRESULT>(GetStockObject(DC_BRUSH));
    }
    case WM_NOTIFY: {
        const auto item = reinterpret_cast<NMHDR*>(l);
        if (item->code == NM_CUSTOMDRAW && (item->idFrom == open_id || item->idFrom == close_id)) {
            const auto draw = reinterpret_cast<NMCUSTOMDRAW*>(l);
            if (draw->dwDrawStage == CDDS_PREPAINT) {
                panel_theme::fill(draw->hdc, draw->rc, palette.surface);
                if (item->idFrom == open_id) panel_theme::button(*draw, palette, self->font_, self->dpi_, true);
                else {
                    const bool hot = (draw->uItemState & (CDIS_HOT | CDIS_SELECTED)) != 0;
                    panel_theme::box(draw->hdc, draw->rc, hot ? palette.input : palette.surface,
                        hot ? palette.border : palette.surface, MulDiv(8, static_cast<int>(self->dpi_), 96));
                    panel_theme::text(draw->hdc, L"×", draw->rc, self->title_font_, palette.muted,
                        DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                    if ((draw->uItemState & CDIS_FOCUS) && panel_theme::focus_visible(draw->hdr.hwndFrom)) {
                        auto focus = draw->rc; InflateRect(&focus, -2, -2); DrawFocusRect(draw->hdc, &focus);
                    }
                }
                return CDRF_SKIPDEFAULT;
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
