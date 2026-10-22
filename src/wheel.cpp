#include "wheel.h"
#include "file_policy.h"
#include <shellapi.h>
#include <shellscalingapi.h>
#include <wincodec.h>
#include <cmath>
#include <numbers>
#include <vector>
#include <algorithm>
namespace shiftwheel {
using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;
namespace {
constexpr UINT_PTR animation_timer_id = 1;
constexpr double show_ms = 160, hide_ms = 100, hover_ms = 110;
double now_ms() {
    static const double frequency = [] { LARGE_INTEGER value; QueryPerformanceFrequency(&value); return static_cast<double>(value.QuadPart); }();
    LARGE_INTEGER value; QueryPerformanceCounter(&value);
    return static_cast<double>(value.QuadPart) * 1000.0 / frequency;
}
D2D1_COLOR_F blend(UINT32 from, UINT32 to, float amount) {
    const D2D1::ColorF a(from), b(to);
    return D2D1::ColorF(a.r + (b.r - a.r) * amount, a.g + (b.g - a.g) * amount, a.b + (b.b - a.b) * amount);
}
}
Wheel::Wheel(std::function<bool()> busy, std::function<void(fs::path, Format)> submit) : busy_(std::move(busy)), submit_(std::move(submit)) {}
Wheel::~Wheel() { shutdown(); }
HRESULT Wheel::initialize(HINSTANCE instance) {
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory_.GetAddressOf());
    if (FAILED(hr)) return hr;
    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(text_factory_.GetAddressOf()));
    if (FAILED(hr)) return hr;
    auto font = [&](float size, DWRITE_FONT_WEIGHT weight, ComPtr<IDWriteTextFormat>& result) {
        HRESULT value = text_factory_->CreateTextFormat(L"Segoe UI", nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"ru-RU", &result);
        if (SUCCEEDED(value)) { result->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER); result->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER); result->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP); }
        return value;
    };
    if (FAILED(hr = font(24, DWRITE_FONT_WEIGHT_SEMI_BOLD, large_text_))) return hr;
    if (FAILED(hr = font(11, DWRITE_FONT_WEIGHT_NORMAL, small_text_))) return hr;
    if (FAILED(hr = font(9, DWRITE_FONT_WEIGHT_SEMI_BOLD, micro_text_))) return hr;
    WNDCLASSW cls{}; cls.lpfnWndProc = window_proc; cls.hInstance = instance; cls.lpszClassName = L"ShiftWheel.Wheel"; cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!RegisterClassW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return HRESULT_FROM_WIN32(GetLastError());
    window_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED, cls.lpszClassName, L"ShiftWheel", WS_POPUP, 0, 0, 320, 320, nullptr, nullptr, instance, this);
    if (!window_) return HRESULT_FROM_WIN32(GetLastError());
    hr = RegisterDragDrop(window_, this);
    registered_ = SUCCEEDED(hr);
    return hr;
}
void Wheel::shutdown() {
    open_ = false;
    if (animation_timer_) { KillTimer(window_, animation_timer_); animation_timer_ = 0; }
    if (registered_) { registered_ = false; RevokeDragDrop(window_); }
    if (window_) { DestroyWindow(window_); window_ = nullptr; }
    release_target();
}
void Wheel::release_target() { brush_.Reset(); target_.Reset(); }
bool Wheel::visible() const { return window_ && open_; }
void Wheel::update_animation_setting() {
    BOOL enabled = TRUE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0);
    animations_ = enabled != FALSE;
    if (!animations_) {
        opacity_.snap(opacity_.to);
        for (auto& highlight : highlights_) highlight.snap(highlight.to);
    }
}
void Wheel::show(POINT point, bool preview) {
    if (!window_) return;
    const bool was_visible = IsWindowVisible(window_) != FALSE;
    update_animation_setting();
    preview_ = preview; acceptable_ = false; ole_active_ = false; file_.clear(); files_.clear(); hovered_ = -1;
    formats_ = preview ? common_formats({L"preview.png"}) : std::vector<Format>{};
    highlights_.assign(formats_.size(), {});
    open_ = true;
    SetWindowLongPtrW(window_, GWL_EXSTYLE, GetWindowLongPtrW(window_, GWL_EXSTYLE) & ~WS_EX_TRANSPARENT);
    auto monitor = MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{sizeof(info)}; GetMonitorInfoW(monitor, &info);
    UINT dpi_x = 96, dpi_y = 96;
    GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpi_x, &dpi_y);
    const float scale = static_cast<float>(dpi_x) / 96.0f;
    if (scale_ != scale) release_target();
    scale_ = scale;
    const int size = static_cast<int>(320 * scale_);
    const int x = std::max(info.rcWork.left, std::min(point.x - size / 2, info.rcWork.right - size));
    const int y = std::max(info.rcWork.top, std::min(point.y - size / 2, info.rcWork.bottom - size));
    auto region = CreateEllipticRgn(0, 0, size, size);
    if (!SetWindowRgn(window_, region, FALSE)) DeleteObject(region);
    if (!was_visible) opacity_.snap(0.12f);
    opacity_.aim(1, now_ms(), animations_ ? show_ms : 0);
    // Set alpha before showing, so no stale opaque frame flashes on screen.
    animate();
    SetWindowPos(window_, HWND_TOPMOST, x, y, size, size, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    RedrawWindow(window_, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
}
void Wheel::hide() {
    if (!window_ || !open_) return;
    open_ = false; ole_active_ = false;
    // A fading window must not capture an Explorer drop or clicks underneath it.
    SetWindowLongPtrW(window_, GWL_EXSTYLE, GetWindowLongPtrW(window_, GWL_EXSTYLE) | WS_EX_TRANSPARENT);
    opacity_.aim(0, now_ms(), animations_ ? hide_ms : 0);
    animate();
}
void Wheel::set_hover(int sector) {
    if (sector == hovered_) return;
    hovered_ = sector;
    const double now = now_ms();
    for (std::size_t i = 0; i < highlights_.size(); ++i)
        highlights_[i].aim(static_cast<int>(i) == sector ? 1.0f : 0.0f, now, animations_ ? hover_ms : 0);
    animate();
}
void Wheel::animate() {
    if (!window_) return;
    const double now = now_ms();
    bool active = opacity_.active(now);
    for (const auto& highlight : highlights_) active = active || (open_ && highlight.active(now));
    const int alpha = static_cast<int>(std::lround(opacity_.value(now) * 255));
    if (alpha != last_alpha_) {
        SetLayeredWindowAttributes(window_, 0, static_cast<BYTE>(alpha), LWA_ALPHA);
        last_alpha_ = alpha;
    }
    if (!open_ && !opacity_.active(now)) {
        ShowWindow(window_, SW_HIDE);
        file_.clear(); hovered_ = -1; acceptable_ = false;
    } else {
        InvalidateRect(window_, nullptr, FALSE);
    }
    if (active && !animation_timer_) {
        animation_timer_ = SetTimer(window_, animation_timer_id, 16, nullptr);
        if (!animation_timer_) {
            opacity_.snap(opacity_.to);
            for (auto& highlight : highlights_) highlight.snap(highlight.to);
            animate();
        }
    } else if (!active && animation_timer_) {
        KillTimer(window_, animation_timer_); animation_timer_ = 0;
    }
}
HRESULT STDMETHODCALLTYPE Wheel::QueryInterface(REFIID id, void** out) {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (id == IID_IUnknown || id == IID_IDropTarget) { *out = static_cast<IDropTarget*>(this); AddRef(); return S_OK; }
    return E_NOINTERFACE;
}
ULONG STDMETHODCALLTYPE Wheel::AddRef() { return static_cast<ULONG>(InterlockedIncrement(&references_)); }
ULONG STDMETHODCALLTYPE Wheel::Release() { auto count = InterlockedDecrement(&references_); if (!count) delete this; return static_cast<ULONG>(count); }
bool Wheel::read_files(IDataObject* data, std::vector<fs::path>& files) {
    files.clear();
    if (!data) return false;
    FORMATETC format{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM medium{};
    if (FAILED(data->GetData(&format, &medium))) return false;
    bool valid = false;
    if (medium.tymed == TYMED_HGLOBAL) {
        HDROP drop = reinterpret_cast<HDROP>(medium.hGlobal);
        const UINT count = DragQueryFileW(drop, 0xffffffff, nullptr, 0);
        if (count > 0 && count <= (batch_submit_ ? 256U : 1U)) {
            valid = true;
            for (UINT i = 0; i < count; ++i) {
                UINT length = DragQueryFileW(drop, i, nullptr, 0);
                if (!length || length >= 32768) { valid = false; break; }
                std::vector<wchar_t> path(static_cast<std::size_t>(length) + 1);
                if (DragQueryFileW(drop, i, path.data(), length + 1) != length || !supported_local_file(path.data())) { valid = false; break; }
                files.emplace_back(path.data());
            }
        }
    }
    ReleaseStgMedium(&medium);
    if (!valid) files.clear();
    return valid;
}
std::vector<Format> Wheel::common_formats(const std::vector<fs::path>& files) const {
    if (files.empty()) return {};
    auto formats = [&](const fs::path& p) { return format_provider_ ? format_provider_(p) : formats_for(p); };
    auto result = formats(files.front());
    for (size_t i = 1; i < files.size(); ++i) {
        const auto available = formats(files[i]);
        std::erase_if(result, [&](Format f) { return std::find(available.begin(), available.end(), f) == available.end(); });
    }
    return result;
}
int Wheel::hit(POINTL point) const {
    POINT client{point.x, point.y}; ScreenToClient(window_, &client);
    return sector_at(static_cast<float>(client.x) / scale_ - 160, static_cast<float>(client.y) / scale_ - 160, 62, 151, static_cast<int>(formats_.size()));
}
HRESULT STDMETHODCALLTYPE Wheel::DragEnter(IDataObject* data, DWORD keys, POINTL point, DWORD* effect) {
    if (!effect) return E_POINTER;
    if (!open_ || preview_) { *effect = DROPEFFECT_NONE; return S_OK; }
    ole_active_ = true;
    file_.clear();
    acceptable_ = read_files(data, files_);
    if (!files_.empty()) file_ = files_.front();
    formats_ = common_formats(files_);
    acceptable_ = acceptable_ && !formats_.empty();
    highlights_.assign(formats_.size(), {}); hovered_ = -1;
    InvalidateRect(window_, nullptr, FALSE);
    return DragOver(keys, point, effect);
}
HRESULT STDMETHODCALLTYPE Wheel::DragOver(DWORD keys, POINTL point, DWORD* effect) {
    if (!effect) return E_POINTER;
    int sector = hit(point);
    const bool allow = open_ && ole_active_ && !preview_ && can_accept((keys & MK_SHIFT) != 0, (*effect & DROPEFFECT_COPY) != 0, files_.size(), acceptable_, sector, busy_(), static_cast<int>(formats_.size()));
    *effect = allow ? DROPEFFECT_COPY : DROPEFFECT_NONE;
    if (open_) set_hover(allow ? sector : -1);
    return S_OK;
}
HRESULT STDMETHODCALLTYPE Wheel::DragLeave() { hide(); return S_OK; }
HRESULT STDMETHODCALLTYPE Wheel::Drop(IDataObject* data, DWORD keys, POINTL point, DWORD* effect) {
    if (!effect) return E_POINTER;
    std::vector<fs::path> inputs;
    const int sector = hit(point);
    const bool same_source = read_files(data, inputs) && inputs == files_ && common_formats(inputs) == formats_;
    const bool valid = open_ && ole_active_ && !preview_ && can_accept((keys & MK_SHIFT) != 0, (*effect & DROPEFFECT_COPY) != 0, inputs.size(), same_source, sector, busy_(), static_cast<int>(formats_.size()));
    *effect = DROPEFFECT_NONE;
    if (valid) {
        try {
            if (batch_submit_) batch_submit_(inputs, formats_[sector]); else submit_(inputs.front(), formats_[sector]);
            *effect = DROPEFFECT_COPY;
        }
        catch (...) { hide(); return E_FAIL; }
    }
    hide();
    return S_OK;
}
void Wheel::cache_geometry() {
    if (geometry_.size() == formats_.size()) return;
    geometry_.clear();
    const int count = static_cast<int>(formats_.size());
    for (int i = 0; i < count; ++i) {
        ComPtr<ID2D1PathGeometry> geometry; ComPtr<ID2D1GeometrySink> sink;
        if (FAILED(factory_->CreatePathGeometry(&geometry)) || FAILED(geometry->Open(&sink))) { geometry_.clear(); return; }
        const float angle = i * 2 * std::numbers::pi_v<float> / count;
        const float start = angle - std::numbers::pi_v<float> / count + 0.025f;
        const float end = angle + std::numbers::pi_v<float> / count - 0.025f;
        auto point = [](float a, float r) { return D2D1::Point2F(160 + std::sin(a) * r, 160 - std::cos(a) * r); };
        sink->BeginFigure(point(start, 151), D2D1_FIGURE_BEGIN_FILLED);
        sink->AddArc(D2D1::ArcSegment(point(end, 151), D2D1::SizeF(151, 151), 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, count == 1 ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL));
        sink->AddLine(point(end, 67));
        sink->AddArc(D2D1::ArcSegment(point(start, 67), D2D1::SizeF(67, 67), 0, D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE, count == 1 ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        if (FAILED(sink->Close())) { geometry_.clear(); return; }
        geometry_.push_back(std::move(geometry));
    }
}
void Wheel::draw(ID2D1RenderTarget* target, ID2D1SolidColorBrush* brush) {
    target->Clear(D2D1::ColorF(0x10151e));
    cache_geometry();
    auto text = [&](const std::wstring& value, IDWriteTextFormat* style, D2D1_RECT_F rect, UINT32 color) {
        brush->SetColor(D2D1::ColorF(color));
        target->DrawTextW(value.c_str(), static_cast<UINT32>(value.size()), style, rect, brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
    };
    const UINT32 colors[]{0xf4c78d, 0x8ee4cb, 0xaabfff, 0xeab4d6, 0xa7d7ea, 0xcbdc94, 0xf0b29b};
    const int count = static_cast<int>(formats_.size());
    const double now = now_ms();
    for (int i = 0; i < count; ++i) {
        if (geometry_.size() != formats_.size()) break;
        const float angle = i * 2 * std::numbers::pi_v<float> / count;
        auto point = [](float a, float r) { return D2D1::Point2F(160 + std::sin(a) * r, 160 - std::cos(a) * r); };
        const float selected = highlights_[i].value(now);
        const auto color = colors[static_cast<size_t>(i) % std::size(colors)];
        brush->SetColor(blend(0x1e2733, color, selected * 0.18f)); target->FillGeometry(geometry_[i].Get(), brush);
        brush->SetColor(blend(0x354251, color, selected)); target->DrawGeometry(geometry_[i].Get(), brush, 1 + selected);
        auto center = point(angle, 106 + selected * 2);
        text(label(formats_[i]), large_text_.Get(), D2D1::RectF(center.x - 55, center.y - 26, center.x + 55, center.y + 6), color);
        const auto hint = category(file_) == Category::Presentation && (folder_output(file_, formats_[i]) || formats_[i] == Format::Pdf) ? L"Все слайды" :
            folder_output(file_, formats_[i]) && category(file_) == Category::Pdf ? L"Все страницы" :
            category(file_) == Category::Pdf && formats_[i] == Format::Txt ? L"Текст / OCR" : caption(formats_[i]);
        text(hint, small_text_.Get(), D2D1::RectF(center.x - 58, center.y + 5, center.x + 58, center.y + 24), 0xb2bbc6);
    }
    brush->SetColor(D2D1::ColorF(0x10151e)); target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(160, 160), 62, 62), brush);
    brush->SetColor(D2D1::ColorF(0x354251)); target->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(160, 160), 61, 61), brush);
    text(preview_ ? L"ПРОСМОТР" : L"SHIFTWHEEL", micro_text_.Get(), D2D1::RectF(108, 128, 212, 144), 0x8ee4cb);
    std::wstring name = file_.empty() ? L"Перетащи файл" : acceptable_ ? file_.filename().wstring() : L"Нет форматов";
    if (acceptable_ && files_.size() > 1) name = L"Файлов: " + std::to_wstring(files_.size());
    if (preview_) name = file_.empty() ? L"Выбери формат" : file_.filename().wstring();
    if (name.size() > 17) name = name.substr(0, 14) + L"…";
    text(name, small_text_.Get(), D2D1::RectF(107, 149, 213, 172), 0xf2f4f8);
    text(preview_ ? L"Esc — закрыть" : L"Брось в сектор", micro_text_.Get(), D2D1::RectF(108, 179, 212, 195), 0x8995a6);
}
void Wheel::paint() {
    PAINTSTRUCT paint{}; BeginPaint(window_, &paint);
    if (!target_) {
        RECT rect{}; GetClientRect(window_, &rect);
        auto properties = D2D1::RenderTargetProperties(); properties.dpiX = properties.dpiY = 96 * scale_;
        factory_->CreateHwndRenderTarget(properties, D2D1::HwndRenderTargetProperties(window_, D2D1::SizeU(rect.right, rect.bottom)), &target_);
    }
    if (target_ && !brush_) target_->CreateSolidColorBrush(D2D1::ColorF(0xffffff), &brush_);
    if (target_ && brush_) { target_->BeginDraw(); draw(target_.Get(), brush_.Get()); if (target_->EndDraw() == D2DERR_RECREATE_TARGET) release_target(); }
    EndPaint(window_, &paint);
}
LRESULT CALLBACK Wheel::window_proc(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
    auto self = reinterpret_cast<Wheel*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) { self = static_cast<Wheel*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams); SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)); }
    if (!self) return DefWindowProcW(hwnd, message, w, l);
    switch (message) {
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: self->paint(); return 0;
    case WM_TIMER: if (w == animation_timer_id && self->animation_timer_) self->animate(); return 0;
    case WM_SETTINGCHANGE: self->update_animation_setting(); if (self->visible() || self->animation_timer_) self->animate(); return 0;
    case WM_CLOSE: self->hide(); return 0;
    case WM_DPICHANGED: {
        self->scale_ = static_cast<float>(HIWORD(w)) / 96.0f;
        self->release_target();
        const int size = static_cast<int>(320 * self->scale_);
        auto region = CreateEllipticRgn(0, 0, size, size);
        if (!SetWindowRgn(hwnd, region, FALSE)) DeleteObject(region);
        const auto suggested = reinterpret_cast<const RECT*>(l);
        SetWindowPos(hwnd, nullptr, suggested->left, suggested->top, size, size, SWP_NOACTIVATE | SWP_NOZORDER);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_MOUSEMOVE:
        if (self->preview_ && self->open_) {
            POINT point{static_cast<short>(LOWORD(l)), static_cast<short>(HIWORD(l))}; ClientToScreen(hwnd, &point);
            self->set_hover(self->hit(POINTL{point.x, point.y}));
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd, 0}; TrackMouseEvent(&track);
        }
        return 0;
    case WM_MOUSELEAVE: if (self->preview_ && self->open_) self->set_hover(-1); return 0;
    case WM_LBUTTONUP: if (self->preview_) self->hide(); return 0;
    }
    return DefWindowProcW(hwnd, message, w, l);
}
HRESULT Wheel::render_preview(const fs::path& output, const fs::path& sample) {
    file_ = sample; formats_ = formats_for(sample);
    ComPtr<IWICImagingFactory> wic; ComPtr<IWICBitmap> bitmap; ComPtr<ID2D1RenderTarget> target;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
    if (FAILED(hr)) return hr;
    if (FAILED(hr = wic->CreateBitmap(320, 320, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bitmap))) return hr;
    if (FAILED(hr = factory_->CreateWicBitmapRenderTarget(bitmap.Get(), D2D1::RenderTargetProperties(), &target))) return hr;
    ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(hr = target->CreateSolidColorBrush(D2D1::ColorF(0xffffff), &brush))) return hr;
    preview_ = true; hovered_ = 1;
    highlights_.assign(formats_.size(), {});
    if (highlights_.size() > 1) highlights_[1].snap(1);
    target->BeginDraw(); draw(target.Get(), brush.Get());
    if (FAILED(hr = target->EndDraw())) return hr;
    ComPtr<IWICStream> stream; ComPtr<IWICBitmapEncoder> encoder; ComPtr<IWICBitmapFrameEncode> frame;
    if (FAILED(hr = wic->CreateStream(&stream))) return hr;
    if (FAILED(hr = stream->InitializeFromFilename(output.c_str(), GENERIC_WRITE))) return hr;
    if (FAILED(hr = wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder))) return hr;
    if (FAILED(hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache))) return hr;
    if (FAILED(hr = encoder->CreateNewFrame(&frame, nullptr))) return hr;
    if (FAILED(hr = frame->Initialize(nullptr))) return hr;
    if (FAILED(hr = frame->WriteSource(bitmap.Get(), nullptr))) return hr;
    if (FAILED(hr = frame->Commit())) return hr;
    return encoder->Commit();
}
}
