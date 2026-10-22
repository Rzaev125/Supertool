#include "control_panel.h"
#include <commctrl.h>
#include <windowsx.h>
#include <ole2.h>
#include <iostream>
#include <stdexcept>

using namespace shiftwheel;
namespace {
// Stable native control IDs are exercised through the same HWND messages as the UI.
enum : int { Tabs = 100, Headline, Summary, Progress, ProgressText, Jobs, JobDetail,
    Cancel, Pause, Clear, Open, Retry, Remove,
    Quality = 200, Resources, ImageSize, VideoSize, AudioBitrate, PdfDpi,
    Output, Browse, AutoShow, CategoryChoice, Formats, MoveUp, MoveDown,
    Apply, Defaults, SettingsError, PdfOcr, OcrEnglish, OcrRussian, OcrAzerbaijani, Acceleration, Startup, AutoUpdate, UpdateChannel, UpdateLabel };
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
HWND child(HWND parent, int id) {
    struct Search { int id; HWND result = nullptr; } search{id};
    EnumChildWindows(parent, [](HWND window, LPARAM param) -> BOOL {
        auto& value = *reinterpret_cast<Search*>(param);
        if (GetDlgCtrlID(window) == value.id) { value.result = window; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    check(search.result != nullptr, "control exists"); return search.result;
}
std::wstring control_text(HWND window) {
    std::wstring value(static_cast<size_t>(GetWindowTextLengthW(window)) + 1, L'\0');
    value.resize(static_cast<size_t>(GetWindowTextW(window, value.data(), static_cast<int>(value.size()))));
    return value;
}
void click(HWND window) { SendMessageW(window, BM_CLICK, 0, 0); }
void select(HWND list, int index) {
    ListView_SetItemState(list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_SetItemState(list, index, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
}
}
int main() {
    if (FAILED(OleInitialize(nullptr))) return 1;
    ControlPanel panel;
    int failed = 0, cancelled = 0, paused = 0, cleared = 0, saved = 0;
    uint64_t opened = 0, retried = 0, removed = 0;
    bool reject_save = false;
    std::string save_error = "Выберите существующую папку результата.";
    std::string open_error;
    Settings saved_settings;
    PanelActions actions;
    actions.cancel = [&] { ++cancelled; };
    actions.pause_queue = [&] { ++paused; };
    actions.clear_queue = [&] { ++cleared; };
    actions.open_result = [&](uint64_t id) { if (!open_error.empty()) throw std::runtime_error(open_error); opened = id; };
    actions.retry = [&](uint64_t id) { retried = id; };
    actions.remove = [&](uint64_t id) { removed = id; };
    actions.save = [&](const Settings& settings) {
        if (reject_save) throw std::runtime_error(save_error);
        ++saved; saved_settings = settings;
    };
    if (FAILED(panel.initialize(GetModuleHandleW(nullptr), Settings{}, std::move(actions)))) return 1;
    const HWND window = panel.handle();
    // A permanently hidden host makes show/close tests headless: the panel and its
    // native children never appear on the interactive desktop or acquire focus.
    HWND host = CreateWindowExW(0, L"STATIC", L"Panel test host", WS_POPUP,
        0, 0, 1000, 900, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    check(host != nullptr, "hidden host created");
    SetWindowLongPtrW(window, GWL_STYLE, (GetWindowLongPtrW(window, GWL_STYLE) & ~WS_POPUP) | WS_CHILD);
    SetParent(window, host);
    auto run = [&](const char* name, auto test) {
        try { test(); std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& error) { ++failed; std::cerr << "FAIL " << name << ": " << error.what() << '\n'; }
    };
    run("empty history disables unavailable actions", [&] {
        panel.update({});
        for (int id : {Cancel, Pause, Clear, Open, Retry, Remove}) check(!IsWindowEnabled(child(window, id)), "empty action disabled");
        check(ListView_GetItemCount(child(window, Jobs)) == 0, "empty session list");
    });
    PanelState state;
    state.headline = L"Преобразование"; state.detail = L"Обработка изображения";
    state.running = true; state.percent = 42;
    state.jobs = {{11, L"Фото.png", L"JPG", L"Выполняется", L"Создание результата", false, false, false},
                  {12, L"Видео.mp4", L"WebM", L"В очереди", L"Ожидает запуска", false, true, false},
                  {13, L"Ошибка.pdf", L"TXT", L"Ошибка", L"Не удалось прочитать исходный файл", true, false, false},
                  {14, L"Готово.wav", L"MP3", L"Готово", L"Результат сохранён", false, false, true}};
    run("queue and selected row actions send correct identities", [&] {
        panel.update(state);
        check(ListView_GetItemCount(child(window, Jobs)) == 4, "all session jobs listed");
        click(child(window, Cancel)); click(child(window, Pause)); click(child(window, Clear));
        check(cancelled == 1 && paused == 1 && cleared == 1, "global action callbacks");
        select(child(window, Jobs), 1);
        check(IsWindowEnabled(child(window, Remove)), "pending job removable");
        check(!IsWindowEnabled(child(window, Retry)) && !IsWindowEnabled(child(window, Open)), "pending job cannot retry or open");
        click(child(window, Remove)); check(removed == 12, "remove selected id");
        select(child(window, Jobs), 2); click(child(window, Retry)); check(retried == 13, "retry selected id");
        check(control_text(child(window, JobDetail)) == state.jobs[2].detail, "selected error detail visible");
        select(child(window, Jobs), 3); click(child(window, Open)); check(opened == 14, "open selected id");
        check(!IsWindowEnabled(child(window, Remove)), "completed row not removable");
    });
    run("progress updates preserve selection and never show hidden panel", [&] {
        SendMessageW(window, WM_CLOSE, 0, 0);
        state.percent = 69; panel.update(state);
        check((GetWindowLongPtrW(window, GWL_STYLE) & WS_VISIBLE) == 0, "hidden panel stays hidden");
        check(ListView_GetNextItem(child(window, Jobs), -1, LVNI_SELECTED) == 3, "progress keeps current selection");
        check(control_text(child(window, ProgressText)) == L"69 %", "known percentage rendered");
        check(SendMessageW(child(window, Progress), PBM_GETPOS, 0, 0) == 69, "progress bar receives exact percentage");
        state.jobs.erase(state.jobs.begin()); panel.update(state);
        check(ListView_GetNextItem(child(window, Jobs), -1, LVNI_SELECTED) == 2, "structural update preserves selected job identity");
        state.percent = -1; panel.update(state);
        check((GetWindowLongPtrW(child(window, Progress), GWL_STYLE) & WS_VISIBLE) == 0, "unknown progress has no animated bar");
        check(control_text(child(window, ProgressText)).empty(), "unknown percentage omitted");
        check((GetWindowLongPtrW(child(window, Progress), GWL_STYLE) & PBS_MARQUEE) == 0, "no indefinite marquee timer");
    });
    run("closing hides without destroying or invoking cancel", [&] {
        panel.show(false, false);
        check((GetWindowLongPtrW(window, GWL_STYLE) & WS_VISIBLE) != 0, "show sets window visibility");
        check(!IsWindowVisible(window), "test host prevents desktop visibility");
        SendMessageW(window, WM_CLOSE, 0, 0);
        check(IsWindow(window) && panel.handle() == window, "close retains panel");
        check((GetWindowLongPtrW(window, GWL_STYLE) & WS_VISIBLE) == 0, "close removes visibility");
        check(cancelled == 1, "close never cancels work");
    });
    run("settings draft survives progress and invalid numeric values", [&] {
        panel.show(true, false);
        SetWindowTextW(child(window, ImageSize), L"2048");
        SetWindowTextW(child(window, PdfDpi), L"0");
        panel.update(state);
        check(control_text(child(window, ImageSize)) == L"2048", "progress never overwrites draft");
        click(child(window, Apply));
        check(saved == 0 && control_text(child(window, PdfDpi)) == L"0", "invalid values stay editable without save");
        check(!control_text(child(window, SettingsError)).empty(), "invalid value explained inline");
        SetWindowTextW(child(window, PdfDpi), L"144");
        SetWindowTextW(child(window, VideoSize), L"4294967296");
        click(child(window, Apply)); check(saved == 0, "overflow rejected");
        SetWindowTextW(child(window, VideoSize), L"0");
        SetWindowTextW(child(window, Output), L"relative-folder");
        click(child(window, Apply)); check(saved == 0, "relative folder rejected");
        SetWindowTextW(child(window, Output), L"");
        click(child(window, Apply));
        check(saved == 1 && saved_settings.conversion.image_max_dimension == 2048, "valid values delivered to save callback");
    });
    run("failed save retains unsaved edits and reports localized error", [&] {
        reject_save = true;
        SetWindowTextW(child(window, PdfDpi), L"300"); click(child(window, Apply));
        check(saved == 1 && control_text(child(window, PdfDpi)) == L"300", "failed save preserves draft");
        check(control_text(child(window, SettingsError)).find(L"Не удалось сохранить") != std::wstring::npos, "localized save error");
        check(control_text(child(window, SettingsError)).find(L"Выберите существующую папку результата.") != std::wstring::npos, "actual save reason preserved");
        save_error = std::string("\xc3\x28", 2); click(child(window, Apply));
        check(control_text(child(window, SettingsError)).find(L"Причину ошибки не удалось прочитать.") != std::wstring::npos, "invalid UTF-8 uses localized fallback");
        check(saved == 1 && control_text(child(window, PdfDpi)) == L"300", "unreadable exception preserves draft");
        reject_save = false; click(child(window, Apply));
        check(saved == 2 && saved_settings.conversion.pdf_dpi == 300, "retry saves retained changes");
    });
    run("action errors preserve reasons and sanitize bounded messages", [&] {
        panel.update(state); select(child(window, Jobs), 2);
        open_error = "Файл\r\n\tрезультата больше не существует.";
        click(child(window, Open));
        const auto reason = control_text(child(window, JobDetail));
        check(reason.find(L"Файл результата больше не существует.") != std::wstring::npos, "action reason retained with normalized controls");
        check(reason.find_first_of(L"\r\n\t") == std::wstring::npos, "control characters removed");
        open_error = std::string(5000, 'x'); click(child(window, Open));
        check(control_text(child(window, JobDetail)).size() < 220, "exception output bounded");
        check(control_text(child(window, JobDetail)).back() == L'…', "truncated message indicated");
        open_error.clear();
    });
    run("format visibility and order persist and cannot become empty", [&] {
        panel.set_settings(Settings{});
        HWND formats = child(window, Formats);
        check(ListView_GetItemCount(formats) == 7, "all image formats displayed");
        select(formats, 0); click(child(window, MoveDown));
        click(child(window, Apply));
        check(saved_settings.wheels.at(Category::Image).front() == Format::Png, "visible order changed and saved");
        for (int i = 1; i < ListView_GetItemCount(formats); ++i) ListView_SetCheckState(formats, i, FALSE);
        ListView_SetCheckState(formats, 0, FALSE);
        check(ListView_GetCheckState(formats, 0), "last visible format cannot be unchecked");
        click(child(window, Apply));
        check(saved_settings.wheels.at(Category::Image) == std::vector<Format>{Format::Png}, "subset saved in selected order");
        SendMessageW(child(window, CategoryChoice), CB_SETCURSEL, 1, 0);
        SendMessageW(GetParent(child(window, CategoryChoice)), WM_COMMAND, MAKEWPARAM(CategoryChoice, CBN_SELCHANGE), reinterpret_cast<LPARAM>(child(window, CategoryChoice)));
        check(ListView_GetItemCount(formats) == 6, "category changes available formats");
        click(child(window, Defaults)); click(child(window, Apply));
        check(saved_settings.conversion.pdf_dpi == 144 && saved_settings.conversion.image_max_dimension == 0, "default settings restored only on save");
    });
    run("paused empty queue can resume and idle state clears progress", [&] {
        PanelState idle; idle.queue_paused = true; panel.update(idle);
        check(IsWindowEnabled(child(window, Pause)), "paused queue can always resume");
        check(control_text(child(window, Pause)) == L"Продолжить очередь", "resume action label");
        check(!IsWindowEnabled(child(window, Cancel)), "idle cannot cancel");
        check(control_text(child(window, ProgressText)).empty(), "idle percentage cleared");
        check((GetWindowLongPtrW(child(window, Progress), GWL_STYLE) & WS_VISIBLE) == 0, "idle bar hidden");
    });
    run("OCR languages and presentation category persist in settings", [&] {
        panel.set_settings(Settings{}); panel.show(true, false);
        check(SendMessageW(child(window, Acceleration), CB_GETCOUNT, 0, 0) == 2, "video auto/CPU choices exist");
        check(SendMessageW(child(window, Acceleration), CB_GETCURSEL, 0, 0) == 0, "automatic acceleration enabled by default");
        check(SendMessageW(child(window, PdfOcr), BM_GETCHECK, 0, 0) == BST_CHECKED, "OCR enabled by default");
        for (int id : {OcrEnglish, OcrRussian, OcrAzerbaijani}) SendMessageW(child(window, id), BM_SETCHECK, BST_UNCHECKED, 0);
        const int before = saved; click(child(window, Apply));
        check(saved == before && !control_text(child(window, SettingsError)).empty(), "empty OCR language list rejected");
        SendMessageW(child(window, OcrAzerbaijani), BM_SETCHECK, BST_CHECKED, 0);
        SendMessageW(child(window, PdfOcr), BM_SETCHECK, BST_UNCHECKED, 0);
        SendMessageW(child(window, Acceleration), CB_SETCURSEL, 1, 0);
        SendMessageW(child(window, CategoryChoice), CB_SETCURSEL, 6, 0);
        SendMessageW(GetParent(child(window, CategoryChoice)), WM_COMMAND, MAKEWPARAM(CategoryChoice, CBN_SELCHANGE), reinterpret_cast<LPARAM>(child(window, CategoryChoice)));
        check(ListView_GetItemCount(child(window, Formats)) == 5, "presentation format choices");
        click(child(window, Apply));
        check(saved == before + 1 && !saved_settings.conversion.pdf_ocr && saved_settings.conversion.ocr_languages == "aze", "OCR settings saved");
        check(saved_settings.conversion.video_acceleration == VideoAcceleration::Cpu, "CPU-only setting saved");
        panel.set_settings(saved_settings);
        check(SendMessageW(child(window, Acceleration), CB_GETCURSEL, 0, 0) == 1, "CPU-only choice restored");
        SendMessageW(child(window, Acceleration), CB_SETCURSEL, static_cast<WPARAM>(-1), 0);
        click(child(window, Apply)); check(saved == before + 1, "empty acceleration choice rejected");
        panel.set_settings(Settings{});
    });
    run("DPI resize preserves usable controls and keyboard scrolls settings", [&] {
        panel.show(true, false);
        RECT proposed{0, 0, 1110, 810};
        SendMessageW(window, WM_DPICHANGED, MAKELONG(144, 144), reinterpret_cast<LPARAM>(&proposed));
        HWND formats = child(window, Formats);
        HWND page = GetParent(formats);
        SCROLLINFO info{sizeof(info), SIF_ALL};
        check(GetScrollInfo(page, SB_VERT, &info) && info.nMax >= static_cast<int>(info.nPage), "small settings viewport scrolls");
        SendMessageW(formats, WM_SETFOCUS, 0, 0);
        GetScrollInfo(page, SB_VERT, &info); check(info.nPos > 0, "keyboard focus scrolls lower controls into view");
        RECT item{}, viewport{}; GetWindowRect(formats, &item); GetClientRect(page, &viewport);
        MapWindowPoints(nullptr, page, reinterpret_cast<POINT*>(&item), 2);
        check(item.top >= 0 && item.bottom <= viewport.bottom, "focused format list fits viewport");
        SendMessageW(child(window, Quality), WM_SETFOCUS, 0, 0);
        GetScrollInfo(page, SB_VERT, &info); check(info.nPos < 80, "keyboard returns to top fields");
        HFONT font = reinterpret_cast<HFONT>(SendMessageW(child(window, Apply), WM_GETFONT, 0, 0));
        LOGFONTW description{}; check(GetObjectW(font, sizeof(description), &description) != 0, "DPI font exists");
        check(description.lfHeight == -20, "Segoe UI text scales with DPI");
        check(!IsWindowVisible(window), "DPI testing remains headless");
    });
    run("startup and update settings survive UI edits", [&] {
        panel.set_settings(Settings{});
        SendMessageW(child(window, Startup), BM_SETCHECK, BST_CHECKED, 0);
        SendMessageW(child(window, AutoUpdate), BM_SETCHECK, BST_UNCHECKED, 0);
        SetWindowTextW(child(window, UpdateChannel), L"https://example.com/stable.txt");
        click(child(window, Apply));
        check(saved_settings.start_with_windows && !saved_settings.automatic_updates && saved_settings.update_channel == L"https://example.com/stable.txt", "application settings saved");
    });
    panel.shutdown();
    check(!panel.handle() && !IsWindow(window), "shutdown destroys native window");
    DestroyWindow(host);
    OleUninitialize();
    return failed ? 1 : 0;
}
