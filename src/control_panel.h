#pragma once
#include "options.h"
#include <windows.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace shiftwheel {
struct PanelRow {
    uint64_t id = 0;
    std::wstring name, target, status, detail;
    bool retryable = false, removable = false, openable = false;
};
struct PanelState {
    std::wstring headline, detail;
    int percent = -1;
    bool running = false, queue_paused = false;
    std::vector<PanelRow> jobs;
    bool recovery_pending = false;
    std::wstring update_detail;
    bool update_checking=false,update_ready=false,update_available=false;
};
struct PanelActions {
    std::function<void()> cancel, pause_queue, clear_queue, export_diagnostics, clear_history;
    std::function<void(uint64_t)> open_result, retry, remove;
    std::function<void(const Settings&)> save;
    std::function<void()> check_update,install_update,later_update;
};
class ControlPanel {
public:
    ControlPanel();
    ~ControlPanel();
    ControlPanel(const ControlPanel&) = delete;
    ControlPanel& operator=(const ControlPanel&) = delete;
    HRESULT initialize(HINSTANCE instance, const Settings& settings, PanelActions actions);
    void update(const PanelState& state);
    void show(bool settings = false, bool activate = true);
    // Explicitly replaces the settings draft. Progress updates never alter it.
    void set_settings(const Settings& settings);
    void shutdown();
    HWND handle() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
