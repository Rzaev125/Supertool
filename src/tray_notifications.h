#pragma once
#include <windows.h>
#include <shellapi.h>
#include <deque>
#include <filesystem>
#include <optional>
#include <string>
namespace shiftwheel {
inline constexpr UINT tray_tooltip_flags = NIF_TIP | NIF_SHOWTIP;
struct TrayNotification {
    std::wstring title, message;
    DWORD type = NIIF_INFO;
    std::filesystem::path target;
};
// Only one balloon is submitted to the shell at a time. Its click target stays
// stable until a terminal callback; later results wait in the application.
class TrayNotifications {
public:
    void push(TrayNotification notification) { pending_.push_back(std::move(notification)); }
    bool begin() {
        if(active_ || pending_.empty()) return false;
        active_ = std::move(pending_.front()); pending_.pop_front(); return true;
    }
    const TrayNotification* current() const { return active_ ? &*active_ : nullptr; }
    std::filesystem::path finish() {
        if(!active_) return {};
        auto target=std::move(active_->target); active_.reset(); return target;
    }
    void restart() {
        if(active_) { pending_.push_front(std::move(*active_)); active_.reset(); }
    }
private:
    std::deque<TrayNotification> pending_;
    std::optional<TrayNotification> active_;
};
}
