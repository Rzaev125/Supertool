#pragma once
#include "tray_notifications.h"
#include "motion.h"
#include <functional>

namespace shiftwheel {
class CompletionNotice {
public:
    ~CompletionNotice();
    bool initialize(HINSTANCE instance, HWND owner, std::function<void(std::filesystem::path)> closed);
    bool show(const TrayNotification& notification);
    void hide();
    void shutdown();
    bool visible() const { return visible_; }
    HWND handle() const { return window_; }
private:
    static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM w, LPARAM l);
    void layout(UINT dpi, const RECT& work);
    void paint(HDC dc);
    void dismiss(bool open);
    void finish_dismiss();
    void animate();
    void update_animation_setting();
    HWND window_ = nullptr;
    HFONT font_ = nullptr, title_font_ = nullptr, caption_font_ = nullptr;
    UINT dpi_ = 96;
    bool visible_ = false;
    bool warning_ = false;
    bool closing_ = false, open_on_close_ = false, animations_ = true;
    UINT_PTR animation_timer_ = 0;
    Transition opacity_;
    int last_alpha_ = -1;
    std::filesystem::path folder_;
    std::function<void(std::filesystem::path)> closed_;
};
}
