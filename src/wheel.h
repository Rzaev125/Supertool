#pragma once
#include "policy.h"
#include "motion.h"
#include <windows.h>
#include <oleidl.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <functional>
namespace shiftwheel {
class Wheel final : public IDropTarget {
public:
    Wheel(std::function<bool()> busy, std::function<void(std::filesystem::path, Format)> submit);
    void set_batch_handler(std::function<void(std::vector<std::filesystem::path>, Format)> submit) { batch_submit_ = std::move(submit); }
    void set_format_provider(std::function<std::vector<Format>(const std::filesystem::path&)> provider) { format_provider_ = std::move(provider); }
    HRESULT initialize(HINSTANCE instance);
    void shutdown();
    void show(POINT point, bool preview = false);
    void hide();
    bool visible() const;
    bool has_ole_drag() const { return ole_active_; }
    HWND handle() const { return window_; }
    HRESULT render_preview(const std::filesystem::path& output, const std::filesystem::path& sample = L"preview.png");
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject*, DWORD, POINTL, DWORD*) override;
    HRESULT STDMETHODCALLTYPE DragOver(DWORD, POINTL, DWORD*) override;
    HRESULT STDMETHODCALLTYPE DragLeave() override;
    HRESULT STDMETHODCALLTYPE Drop(IDataObject*, DWORD, POINTL, DWORD*) override;
private:
    ~Wheel();
    static LRESULT CALLBACK window_proc(HWND, UINT, WPARAM, LPARAM);
    void paint();
    void draw(ID2D1RenderTarget* target, ID2D1SolidColorBrush* brush);
    void cache_geometry();
    void set_hover(int sector);
    void animate();
    void update_animation_setting();
    void release_target();
    bool read_files(IDataObject* data, std::vector<std::filesystem::path>& files);
    std::vector<Format> common_formats(const std::vector<std::filesystem::path>& files) const;
    int hit(POINTL point) const;
    LONG references_ = 1;
    HWND window_ = nullptr;
    bool registered_ = false, preview_ = false, acceptable_ = false;
    bool ole_active_ = false;
    bool open_ = false, animations_ = true;
    UINT_PTR animation_timer_ = 0;
    int last_alpha_ = -1;
    Transition opacity_;
    std::vector<Transition> highlights_{6};
    float scale_ = 1;
    int hovered_ = -1;
    std::filesystem::path file_;
    std::vector<std::filesystem::path> files_;
    std::vector<Format> formats_{Format::Jpeg, Format::Png, Format::Bmp, Format::Avif, Format::Webp, Format::Tiff};
    std::function<bool()> busy_;
    std::function<void(std::filesystem::path, Format)> submit_;
    std::function<void(std::vector<std::filesystem::path>, Format)> batch_submit_;
    std::function<std::vector<Format>(const std::filesystem::path&)> format_provider_;
    Microsoft::WRL::ComPtr<ID2D1Factory> factory_;
    Microsoft::WRL::ComPtr<IDWriteFactory> text_factory_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> large_text_, small_text_, micro_text_;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> target_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush_;
    std::vector<Microsoft::WRL::ComPtr<ID2D1PathGeometry>> geometry_;
};
}
