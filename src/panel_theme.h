#pragma once
#include <windows.h>
#include <commctrl.h>

namespace shiftwheel::panel_theme {
struct Palette {
    COLORREF canvas, surface, input, border, text, muted, accent, selection, selected_text;
    bool high_contrast;
};
Palette colors();
bool focus_visible(HWND control);
void fill(HDC dc, RECT rect, COLORREF color);
void box(HDC dc, RECT rect, COLORREF background, COLORREF border, int radius);
void text(HDC dc, const wchar_t* value, RECT rect, HFONT font, COLORREF color,
          UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
void button(const NMCUSTOMDRAW& item, const Palette& palette, HFONT font, UINT dpi, bool primary);
void combo_border(HWND control, HDC dc, const Palette& palette, UINT dpi);
}
