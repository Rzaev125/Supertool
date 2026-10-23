#include "panel_theme.h"
#include <string>
#include <algorithm>

namespace shiftwheel::panel_theme {
bool focus_visible(HWND control) {
    return (SendMessageW(control,WM_QUERYUISTATE,0,0)&UISF_HIDEFOCUS)==0;
}
Palette colors() {
    HIGHCONTRASTW contrast{sizeof(contrast)};
    if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) && (contrast.dwFlags & HCF_HIGHCONTRASTON)) {
        return {GetSysColor(COLOR_BTNFACE), GetSysColor(COLOR_BTNFACE), GetSysColor(COLOR_WINDOW),
            GetSysColor(COLOR_WINDOWTEXT), GetSysColor(COLOR_WINDOWTEXT), GetSysColor(COLOR_BTNTEXT),
            GetSysColor(COLOR_HIGHLIGHT), GetSysColor(COLOR_HIGHLIGHT), GetSysColor(COLOR_HIGHLIGHTTEXT), true};
    }
    return {RGB(17,21,28), RGB(27,34,45), RGB(35,44,57), RGB(51,65,85),
        RGB(237,243,250), RGB(155,170,192), RGB(92,164,255), RGB(28,53,84), RGB(153,204,255), false};
}
void fill(HDC dc, RECT rect, COLORREF color) {
    const auto brush=CreateSolidBrush(color); FillRect(dc,&rect,brush); DeleteObject(brush);
}
void box(HDC dc, RECT rect, COLORREF background, COLORREF border, int radius) {
    const auto saved=SaveDC(dc);
    const auto brush=CreateSolidBrush(background); const auto pen=CreatePen(PS_SOLID,1,border);
    SelectObject(dc,brush); SelectObject(dc,pen);
    RoundRect(dc,rect.left,rect.top,rect.right,rect.bottom,radius,radius);
    RestoreDC(dc,saved); DeleteObject(brush); DeleteObject(pen);
}
void text(HDC dc, const wchar_t* value, RECT rect, HFONT font, COLORREF color, UINT flags) {
    const auto saved=SaveDC(dc); SetBkMode(dc,TRANSPARENT); SetTextColor(dc,color);
    if(font)SelectObject(dc,font);
    DrawTextW(dc,value,-1,&rect,flags|DT_NOPREFIX);
    RestoreDC(dc,saved);
}
void button(const NMCUSTOMDRAW& item, const Palette& p, HFONT font, UINT dpi, bool primary) {
    const HWND control=item.hdr.hwndFrom;
    const auto px=[dpi](int n){return MulDiv(n,static_cast<int>(dpi),96);};
    const bool disabled=!IsWindowEnabled(control), hot=(item.uItemState&CDIS_HOT)!=0, pressed=(item.uItemState&CDIS_SELECTED)!=0;
    const bool checkbox=(GetWindowLongPtrW(control,GWL_STYLE)&BS_TYPEMASK)==BS_AUTOCHECKBOX;
    std::wstring label(static_cast<size_t>(GetWindowTextLengthW(control))+1,L'\0');
    GetWindowTextW(control,label.data(),static_cast<int>(label.size()));
    RECT rect=item.rc;
    COLORREF foreground=disabled?p.muted:p.text;
    if(checkbox) {
        fill(item.hdc,rect,p.surface);
        const bool checked=SendMessageW(control,BM_GETCHECK,0,0)==BST_CHECKED;
        const int side=px(18); RECT mark{rect.left+px(1),rect.top+(rect.bottom-rect.top-side)/2,rect.left+px(1)+side,0};mark.bottom=mark.top+side;
        box(item.hdc,mark,checked?p.accent:p.input,hot?p.accent:p.border,px(5));
        if(checked) {
            const auto saved=SaveDC(item.hdc);
            const auto pen=CreatePen(PS_SOLID,std::max(1,px(2)),p.high_contrast?p.selected_text:p.canvas);
            SelectObject(item.hdc,pen);
            POINT points[]{{mark.left+px(4),mark.top+px(9)},{mark.left+px(8),mark.top+px(13)},{mark.left+px(14),mark.top+px(5)}};
            Polyline(item.hdc,points,3); RestoreDC(item.hdc,saved);DeleteObject(pen);
        }
        rect.left+=px(30);
        text(item.hdc,label.c_str(),rect,font,foreground);
    } else {
        const COLORREF background=disabled?p.surface:primary?p.accent:pressed?p.selection:hot?p.border:p.input;
        box(item.hdc,rect,background,hot?p.accent:p.border,px(8));
        if(primary&&!disabled)foreground=p.high_contrast?p.selected_text:p.canvas;
        rect.left+=px(8);rect.right-=px(8);
        text(item.hdc,label.c_str(),rect,font,foreground,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    }
    if((item.uItemState&CDIS_FOCUS)!=0 && focus_visible(control)) { InflateRect(&rect,-px(3),-px(3));DrawFocusRect(item.hdc,&rect); }
}
void combo_border(HWND control, HDC dc, const Palette& p, UINT dpi) {
    const auto px=[dpi](int n){return MulDiv(n,static_cast<int>(dpi),96);};
    RECT r{};GetClientRect(control,&r);
    fill(dc,r,p.input);
    const auto selected=SendMessageW(control,CB_GETCURSEL,0,0);
    const auto count=selected>=0?SendMessageW(control,CB_GETLBTEXTLEN,selected,0):CB_ERR;
    if(count>=0 && count<4096) {
        std::wstring value(static_cast<size_t>(count)+1,L'\0');
        SendMessageW(control,CB_GETLBTEXT,selected,reinterpret_cast<LPARAM>(value.data()));
        RECT label=r;label.left+=px(10);label.right-=px(30);
        text(dc,value.c_str(),label,reinterpret_cast<HFONT>(SendMessageW(control,WM_GETFONT,0,0)),IsWindowEnabled(control)?p.text:p.muted);
    }
    RECT arrow{r.right-px(30),r.top,r.right,r.bottom};fill(dc,arrow,p.input);
    const auto saved=SaveDC(dc);
    const auto pen=CreatePen(PS_SOLID,std::max(1,px(1)),GetFocus()==control?p.accent:p.border);
    SelectObject(dc,pen);SelectObject(dc,GetStockObject(NULL_BRUSH));
    Rectangle(dc,r.left,r.top,r.right,r.bottom);
    const int x=(arrow.left+arrow.right)/2,y=(arrow.top+arrow.bottom)/2;
    const auto chevron=CreatePen(PS_SOLID,std::max(1,px(2)),IsWindowEnabled(control)?p.muted:p.border);SelectObject(dc,chevron);
    POINT points[]{{x-px(4),y-px(2)},{x,y+px(2)},{x+px(4),y-px(2)}};Polyline(dc,points,3);
    RestoreDC(dc,saved);DeleteObject(pen);DeleteObject(chevron);
}
}
