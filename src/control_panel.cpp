#include "control_panel.h"
#include <commctrl.h>
#include <windowsx.h>
#include <shobjidl.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <exception>
#include <limits>
#include <utility>

namespace shiftwheel {
namespace {
constexpr wchar_t panel_class[] = L"ShiftWheel.ControlPanel";
constexpr wchar_t page_class[] = L"ShiftWheel.SettingsPage";
enum Control : int {
    Tabs = 100, Headline, Summary, Progress, ProgressText, Jobs, JobDetail,
    Cancel, Pause, Clear, Open, Retry, Remove,
    Quality = 200, Resources, ImageSize, VideoSize, AudioBitrate, PdfDpi,
    Output, Browse, AutoShow, CategoryChoice, Formats, MoveUp, MoveDown,
    Apply, Defaults, SettingsError, PdfOcr, OcrEnglish, OcrRussian, OcrAzerbaijani, Acceleration, Startup, AutoUpdate, UpdateChannel, UpdateLabel, Notifications, NotificationsLabel, SettingsSection, ExportDiagnostics,
    QualityLabel = 300, ResourcesLabel, ImageLabel, VideoLabel, AudioLabel,
    PdfLabel, OutputLabel, OutputHint, WheelLabel, WheelHint, CategoryLabel, AccelerationLabel,
    HistorySearch = 120, HistoryLabel, ClearHistory,
    UpdateInfo = 130, CheckUpdate, InstallUpdate, LaterUpdate,
    Modules=140,ModuleInfo,InstallModule,RemoveModule,CancelModule,RefreshModules,
    Codec=320,CodecLabel,ImageMetadataChoice,ImageMetadataLabel
};
struct CategoryEntry { Category category; const wchar_t* name; const wchar_t* sample; };
constexpr std::array categories{
    CategoryEntry{Category::Image, L"Изображения", L"sample.png"},
    CategoryEntry{Category::Video, L"Видео", L"sample.mp4"},
    CategoryEntry{Category::Audio, L"Аудио", L"sample.wav"},
    CategoryEntry{Category::Document, L"Документы", L"sample.docx"},
    CategoryEntry{Category::Spreadsheet, L"Таблицы", L"sample.xlsx"},
    CategoryEntry{Category::Pdf, L"PDF", L"sample.pdf"},
    CategoryEntry{Category::Presentation, L"Презентации", L"sample.pptx"}
};
std::wstring text_of(HWND control) {
    const int size = GetWindowTextLengthW(control);
    std::wstring text(static_cast<size_t>(size) + 1, L'\0');
    text.resize(static_cast<size_t>(GetWindowTextW(control, text.data(), size + 1)));
    return text;
}
void set_text(HWND control, const std::wstring& text) {
    if (text_of(control) != text) SetWindowTextW(control, text.c_str());
}
std::wstring exception_reason(const std::exception& error) {
    constexpr int max_bytes = 4096;
    constexpr size_t max_characters = 160;
    const char* source = error.what();
    if (!source) return L"Причину ошибки не удалось прочитать.";
    int bytes = 0;
    while (bytes < max_bytes && source[bytes]) ++bytes;
    bool truncated = bytes == max_bytes;
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, source, bytes, nullptr, 0);
    // A bounded prefix can cut one UTF-8 character; discard only that tail.
    if (!count && truncated) {
        for (int tail = 1; tail <= 3 && !count; ++tail) {
            count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, source, bytes - tail, nullptr, 0);
            if (count) bytes -= tail;
        }
    }
    if (!count) return L"Причину ошибки не удалось прочитать.";
    std::wstring decoded(static_cast<size_t>(count), L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, source, bytes, decoded.data(), count))
        return L"Причину ошибки не удалось прочитать.";
    std::wstring result;
    for (wchar_t value : decoded) {
        if (value < 32 || (value >= 127 && value <= 159) || (value >= 0x200b && value <= 0x200f) ||
            (value >= 0x2028 && value <= 0x202e) || (value >= 0x2066 && value <= 0x2069)) value = L' ';
        if (value == L' ' && (result.empty() || result.back() == L' ')) continue;
        if (result.size() >= max_characters) { truncated = true; break; }
        result += value;
    }
    while (!result.empty() && result.back() == L' ') result.pop_back();
    if (!result.empty() && result.back() >= 0xd800 && result.back() <= 0xdbff) result.pop_back();
    if (result.empty()) return L"Причину ошибки не удалось прочитать.";
    if (truncated) result += L'…';
    return result;
}
bool unsigned_value(HWND control, unsigned& result) {
    const auto text = text_of(control);
    if (text.empty() || text.size() > 10) return false;
    unsigned value = 0;
    for (const auto c : text) {
        if (c < L'0' || c > L'9') return false;
        const auto digit = static_cast<unsigned>(c - L'0');
        if (value > (std::numeric_limits<unsigned>::max() - digit) / 10) return false;
        value = value * 10 + digit;
    }
    result = value;
    return true;
}
}

struct ControlPanel::Impl {
    HWND window = nullptr, page = nullptr;
    HINSTANCE instance = nullptr;
    HFONT font = nullptr, heading_font = nullptr;
    HBRUSH background = CreateSolidBrush(RGB(250, 251, 253));
    UINT dpi = 96;
    int scroll = 0, category_index = 0, settings_section = 0;
    bool settings_visible = false, rebuilding = false;
    Settings draft;
    PanelState state;
    PanelState unfiltered;
    PanelActions actions;
    std::vector<HWND> task_controls, settings_controls;
    std::vector<Format> format_order;

    ~Impl() {
        if (window) DestroyWindow(window);
        if (font) DeleteObject(font);
        if (heading_font) DeleteObject(heading_font);
        if (background) DeleteObject(background);
    }
    int px(int value) const { return MulDiv(value, static_cast<int>(dpi), 96); }
    HWND main_control(int id) const { return GetDlgItem(window, id); }
    HWND setting(int id) const { return GetDlgItem(page, id); }
    HWND add(HWND parent, int id, const wchar_t* type, const wchar_t* text,
             DWORD style = 0, DWORD extended = 0) {
        HWND control = CreateWindowExW(extended, type, text, WS_CHILD | WS_VISIBLE | style,
            0, 0, 0, 0, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
        if (control) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
        return control;
    }
    HWND task(int id, const wchar_t* type, const wchar_t* text, DWORD style = 0, DWORD extended = 0) {
        HWND control = add(window, id, type, text, style, extended);
        task_controls.push_back(control);
        return control;
    }
    void move(HWND control, int x, int y, int width, int height) {
        if (control) MoveWindow(control, px(x), px(y), px(std::max(0, width)), px(std::max(0, height)), TRUE);
    }
    void move_setting(int id, int x, int y, int width, int height) {
        if (HWND control = setting(id)) MoveWindow(control, px(x), px(y) - scroll, px(width), px(height), TRUE);
    }
    void make_fonts() {
        HFONT old = font, old_heading = heading_font;
        font = CreateFontW(-MulDiv(10, static_cast<int>(dpi), 72), 0, 0, 0, FW_NORMAL,
            FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        heading_font = CreateFontW(-MulDiv(13, static_cast<int>(dpi), 72), 0, 0, 0, FW_SEMIBOLD,
            FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        if (window) {
            EnumChildWindows(window, [](HWND child, LPARAM value) -> BOOL {
                SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(value), TRUE); return TRUE;
            }, reinterpret_cast<LPARAM>(font));
            SendMessageW(main_control(Headline), WM_SETFONT, reinterpret_cast<WPARAM>(heading_font), TRUE);
        }
        if (old) DeleteObject(old);
        if (old_heading) DeleteObject(old_heading);
    }
    bool create_controls() {
        add(window, Tabs, WC_TABCONTROLW, L"", WS_TABSTOP);
        TCITEMW tab{}; tab.mask = TCIF_TEXT;
        tab.pszText = const_cast<wchar_t*>(L"Задачи"); TabCtrl_InsertItem(main_control(Tabs), 0, &tab);
        tab.pszText = const_cast<wchar_t*>(L"Настройки"); TabCtrl_InsertItem(main_control(Tabs), 1, &tab);
        task(Headline, L"STATIC", L"Готово к работе", SS_LEFT);
        task(Summary, L"STATIC", L"Зажмите Shift и перетащите файл, чтобы выбрать формат.", SS_LEFT);
        task(ProgressText, L"STATIC", L"", SS_RIGHT);
        task(Progress, PROGRESS_CLASSW, L"", PBS_SMOOTH);
        SendMessageW(main_control(Progress), PBM_SETRANGE32, 0, 100);
        task(Cancel, L"BUTTON", L"Отменить задачу", WS_TABSTOP);
        task(Pause, L"BUTTON", L"Приостановить очередь", WS_TABSTOP);
        task(Clear, L"BUTTON", L"Очистить очередь", WS_TABSTOP);
        HWND jobs = task(Jobs, WC_LISTVIEWW, L"", WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS, WS_EX_CLIENTEDGE);
        ListView_SetExtendedListViewStyle(jobs, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
        LVCOLUMNW column{}; column.mask = LVCF_TEXT | LVCF_WIDTH;
        column.pszText = const_cast<wchar_t*>(L"Файл"); column.cx = px(320); ListView_InsertColumn(jobs, 0, &column);
        column.pszText = const_cast<wchar_t*>(L"Формат"); column.cx = px(90); ListView_InsertColumn(jobs, 1, &column);
        column.pszText = const_cast<wchar_t*>(L"Состояние"); column.cx = px(250); ListView_InsertColumn(jobs, 2, &column);
        task(JobDetail, L"EDIT", L"Здесь появятся задачи и результаты текущего сеанса.",
            WS_TABSTOP | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL, WS_EX_CLIENTEDGE);
        task(Open, L"BUTTON", L"Показать результат", WS_TABSTOP);
        task(Retry, L"BUTTON", L"Повторить", WS_TABSTOP);
        task(Remove, L"BUTTON", L"Убрать из очереди", WS_TABSTOP);
        task(HistoryLabel,L"STATIC",L"Поиск истории",SS_LEFT);
        task(HistorySearch,L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL,WS_EX_CLIENTEDGE);
        task(ClearHistory,L"BUTTON",L"Очистить историю",WS_TABSTOP);

        settings_controls.push_back(add(window, SettingsSection, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST));
        for(auto section : {L"Основные",L"Конвертация",L"Колесо",L"Обновления",L"Модули"}) SendMessageW(main_control(SettingsSection),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(section));
        SendMessageW(main_control(SettingsSection),CB_SETCURSEL,0,0);
        page = CreateWindowExW(WS_EX_CONTROLPARENT, page_class, L"", WS_CHILD | WS_VSCROLL | WS_CLIPCHILDREN,
            0, 0, 0, 0, window, nullptr, instance, this);
        if (!page) return false;
        const auto combo = [&](int id) { return add(page, id, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL); };
        const auto number = [&](int id) {
            HWND control = add(page, id, L"EDIT", L"", WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE);
            SendMessageW(control, EM_SETLIMITTEXT, 10, 0);
        };
        const auto label_control = [&](int id, const wchar_t* text) { add(page, id, L"STATIC", text); };
        label_control(QualityLabel, L"Качество"); combo(Quality);
        for (const auto value : {L"Компактный файл", L"Сбалансированное", L"Высокое"}) SendMessageW(setting(Quality), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value));
        label_control(ResourcesLabel, L"Использование ресурсов"); combo(Resources);
        for (const auto value : {L"Экономное", L"Сбалансированное", L"Быстрое"}) SendMessageW(setting(Resources), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value));
        label_control(ImageLabel, L"Размер изображения, пикс. (0 — исходный)"); number(ImageSize);
        label_control(VideoLabel, L"Высота видео, пикс. (0 — исходная)"); number(VideoSize);
        label_control(AudioLabel, L"Битрейт аудио, кбит/с (0 — авто)"); number(AudioBitrate);
        label_control(PdfLabel, L"PDF в изображение, DPI"); number(PdfDpi);
        label_control(AccelerationLabel, L"Ускорение кодирования"); combo(Acceleration);
        label_control(ImageMetadataLabel, L"Метаданные фото (без GPS)"); combo(ImageMetadataChoice);
        for (const auto value : {L"Удалять EXIF/XMP", L"Сохранять только дату съёмки", L"Дата, автор и copyright"}) SendMessageW(setting(ImageMetadataChoice), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value));
        label_control(CodecLabel, L"Видеокодек"); combo(Codec);
        for (const auto value : {L"По формату: H.264 / VP9", L"H.264 — MP4 / MKV / MOV", L"HEVC — MP4 / MKV / MOV", L"AV1 — MP4 / MKV / WebM"}) SendMessageW(setting(Codec), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value));
        for (const auto value : {L"Автоматически (GPU / CPU)", L"Только процессор (CPU)"}) SendMessageW(setting(Acceleration), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value));
        add(page, PdfOcr, L"BUTTON", L"Распознавать сканы при конвертации PDF в TXT", WS_TABSTOP | BS_AUTOCHECKBOX);
        add(page, OcrEnglish, L"BUTTON", L"Английский", WS_TABSTOP | BS_AUTOCHECKBOX);
        add(page, OcrRussian, L"BUTTON", L"Русский", WS_TABSTOP | BS_AUTOCHECKBOX);
        add(page, OcrAzerbaijani, L"BUTTON", L"Азербайджанский", WS_TABSTOP | BS_AUTOCHECKBOX);
        label_control(OutputLabel, L"Папка результатов");
        add(page, Output, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE);
        SendMessageW(setting(Output), EM_SETLIMITTEXT, 32767, 0);
        add(page, Browse, L"BUTTON", L"Обзор…", WS_TABSTOP);
        label_control(OutputHint, L"Оставьте пустым, чтобы сохранять рядом с исходным файлом.");
        add(page, AutoShow, L"BUTTON", L"Показывать окно при запуске преобразования", WS_TABSTOP | BS_AUTOCHECKBOX);
        label_control(NotificationsLabel, L"Уведомления о конвертации"); combo(Notifications);
        for(auto mode : {L"Все результаты",L"Только ошибки",L"Без уведомлений"}) SendMessageW(setting(Notifications),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(mode));
        add(page, ExportDiagnostics, L"BUTTON", L"Экспорт диагностики…", WS_TABSTOP);
        add(page, Startup, L"BUTTON", L"Запускать вместе с Windows", WS_TABSTOP | BS_AUTOCHECKBOX);
        add(page, AutoUpdate, L"BUTTON", L"Автоматически проверять и загружать обновления", WS_TABSTOP | BS_AUTOCHECKBOX);
        label_control(UpdateLabel, L"Канал обновлений (HTTPS)");
        add(page, UpdateChannel, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE);
        SendMessageW(setting(UpdateChannel), EM_SETLIMITTEXT, 4096, 0);
        add(page,UpdateInfo,L"STATIC",L"Проверка обновлений ещё не выполнялась.",SS_LEFT|SS_NOPREFIX);
        add(page,CheckUpdate,L"BUTTON",L"Проверить",WS_TABSTOP);
        add(page,InstallUpdate,L"BUTTON",L"Установить сейчас",WS_TABSTOP);
        add(page,LaterUpdate,L"BUTTON",L"Позже — на сутки",WS_TABSTOP);
        combo(Modules);
        add(page,ModuleInfo,L"EDIT",L"",WS_TABSTOP|ES_READONLY|ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL,WS_EX_CLIENTEDGE);
        add(page,InstallModule,L"BUTTON",L"Установить / обновить",WS_TABSTOP);
        add(page,RemoveModule,L"BUTTON",L"Удалить",WS_TABSTOP);
        add(page,CancelModule,L"BUTTON",L"Отменить загрузку",WS_TABSTOP);
        add(page,RefreshModules,L"BUTTON",L"Обновить список",WS_TABSTOP);
        label_control(WheelLabel, L"Форматы в круговом меню");
        label_control(WheelHint, L"Отметьте нужные форматы и расположите их по порядку. В каждой категории нужен хотя бы один.");
        label_control(CategoryLabel, L"Категория"); combo(CategoryChoice);
        for (const auto& entry : categories) SendMessageW(setting(CategoryChoice), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(entry.name));
        HWND formats = add(page, Formats, WC_LISTVIEWW, L"", WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_NOCOLUMNHEADER, WS_EX_CLIENTEDGE);
        ListView_SetExtendedListViewStyle(formats, LVS_EX_FULLROWSELECT | LVS_EX_CHECKBOXES | LVS_EX_DOUBLEBUFFER);
        column.pszText = const_cast<wchar_t*>(L"Формат"); column.cx = px(270); ListView_InsertColumn(formats, 0, &column);
        add(page, MoveUp, L"BUTTON", L"Выше", WS_TABSTOP);
        add(page, MoveDown, L"BUTTON", L"Ниже", WS_TABSTOP);
        settings_controls.push_back(add(window, SettingsError, L"STATIC", L"", SS_LEFT | SS_NOPREFIX));
        settings_controls.push_back(add(window, Defaults, L"BUTTON", L"По умолчанию", WS_TABSTOP));
        settings_controls.push_back(add(window, Apply, L"BUTTON", L"Сохранить настройки", WS_TABSTOP | BS_DEFPUSHBUTTON));
        EnumChildWindows(page, [](HWND control, LPARAM value) -> BOOL {
            SetWindowSubclass(control, control_proc, 1, static_cast<DWORD_PTR>(value)); return TRUE;
        }, reinterpret_cast<LPARAM>(this));
        make_fonts();
        load_draft(draft);
        update_state(state);
        switch_tab(false);
        return std::all_of(task_controls.begin(), task_controls.end(), [](HWND control) { return control != nullptr; }) &&
            std::all_of(settings_controls.begin(), settings_controls.end(), [](HWND control) { return control != nullptr; });
    }

    int section_height() const { return settings_section == 0 ? 360 : settings_section == 1 ? 398 : settings_section == 2 ? 265 : 360; }
    void layout_page() {
        if (!page) return;
        const std::vector<std::vector<int>> groups{
            {AutoShow,Startup,NotificationsLabel,Notifications,OutputLabel,Output,Browse,OutputHint,ExportDiagnostics},
            {QualityLabel,Quality,ResourcesLabel,Resources,ImageLabel,ImageSize,VideoLabel,VideoSize,AudioLabel,AudioBitrate,PdfLabel,PdfDpi,AccelerationLabel,Acceleration,CodecLabel,Codec,ImageMetadataLabel,ImageMetadataChoice,PdfOcr,OcrEnglish,OcrRussian,OcrAzerbaijani},
            {WheelLabel,WheelHint,CategoryLabel,CategoryChoice,Formats,MoveUp,MoveDown},
            {AutoUpdate,UpdateLabel,UpdateChannel,UpdateInfo,CheckUpdate,InstallUpdate,LaterUpdate},
            {Modules,ModuleInfo,InstallModule,RemoveModule,CancelModule,RefreshModules}};
        for(size_t i=0;i<groups.size();++i) for(auto id:groups[i]) ShowWindow(setting(id),i==static_cast<size_t>(settings_section)?SW_SHOW:SW_HIDE);
        RECT rect{}; GetClientRect(page,&rect);
        const int width=MulDiv(rect.right,96,static_cast<int>(dpi)), half=(width-60)/2,right=40+half;
        if(settings_section == 0) {
            move_setting(AutoShow,20,10,width-40,28); move_setting(Startup,20,48,width-40,28);
            move_setting(NotificationsLabel,20,94,width-40,24);move_setting(Notifications,20,124,width-40,150);
            move_setting(OutputLabel,20,172,width-40,24);move_setting(Output,20,202,width-152,29);move_setting(Browse,width-122,202,102,29);
            move_setting(OutputHint,20,238,width-40,40);move_setting(ExportDiagnostics,20,300,240,32);
            EnableWindow(setting(ExportDiagnostics),static_cast<bool>(actions.export_diagnostics));
        } else if(settings_section == 1) {
            for(const auto& row : {std::array<int,5>{QualityLabel,Quality,ResourcesLabel,Resources,2},
                std::array<int,5>{ImageLabel,ImageSize,VideoLabel,VideoSize,66},std::array<int,5>{AudioLabel,AudioBitrate,PdfLabel,PdfDpi,130}}) {
                move_setting(row[0],20,row[4],half,23);move_setting(row[1],20,row[4]+26,half,row[1]==Quality?200:29);
                move_setting(row[2],right,row[4],half,23);move_setting(row[3],right,row[4]+26,half,row[3]==Resources?200:29);
            }
            move_setting(CodecLabel,20,196,half,23);move_setting(Codec,20,222,half,160);
            move_setting(AccelerationLabel,right,196,half,23);move_setting(Acceleration,right,222,half,140);
            move_setting(ImageMetadataLabel,20,330,width-40,23);move_setting(ImageMetadataChoice,20,356,width-40,130);
            move_setting(PdfOcr,20,260,width-40,26);move_setting(OcrEnglish,20,292,160,26);
            move_setting(OcrRussian,190,292,160,26);move_setting(OcrAzerbaijani,360,292,width-380,26);
        } else if(settings_section == 2) {
            move_setting(WheelLabel,20,10,width-40,24);move_setting(WheelHint,20,40,width-40,40);
            move_setting(CategoryLabel,20,92,178,22);move_setting(CategoryChoice,20,118,178,220);
            move_setting(Formats,218,92,width-372,158);move_setting(MoveUp,width-134,92,114,30);move_setting(MoveDown,width-134,130,114,30);
            ListView_SetColumnWidth(setting(Formats),0,px(width-394));
        } else if(settings_section==3) {
            move_setting(AutoUpdate,20,10,width-40,28);move_setting(UpdateLabel,20,60,width-40,23);move_setting(UpdateChannel,20,90,width-40,29);
            move_setting(UpdateInfo,20,136,width-40,100);
            move_setting(CheckUpdate,20,250,130,32);move_setting(InstallUpdate,162,250,180,32);move_setting(LaterUpdate,354,250,180,32);
        } else {
            move_setting(Modules,20,10,width-40,220);move_setting(ModuleInfo,20,56,width-40,170);
            move_setting(InstallModule,20,242,210,32);move_setting(RemoveModule,242,242,110,32);
            move_setting(CancelModule,20,290,210,32);move_setting(RefreshModules,242,290,190,32);
        }
    }
    void layout() {
        if (!window || !main_control(Tabs)) return;
        RECT rect{}; GetClientRect(window, &rect);
        const int width = MulDiv(rect.right, 96, static_cast<int>(dpi));
        const int height = MulDiv(rect.bottom, 96, static_cast<int>(dpi));
        move(main_control(Tabs), 20, 16, width - 40, 32);
        move(main_control(Headline), 20, 68, width - 140, 30);
        move(main_control(ProgressText), width - 112, 74, 92, 24);
        move(main_control(Summary), 20, 108, width - 40, 40);
        move(main_control(Progress), 20, 150, width - 40, 10);
        move(main_control(Cancel), 20, 176, 160, 32);
        move(main_control(Pause), 192, 176, 218, 32);
        move(main_control(Clear), 422, 176, 180, 32);
        move(main_control(HistoryLabel),20,226,106,24);
        move(main_control(HistorySearch),132,220,width-360,30);
        move(main_control(ClearHistory),width-210,220,190,30);
        const int list_height = std::max(70, height - 442);
        move(main_control(Jobs), 20, 262, width - 40, list_height);
        const int name_width = (width - 62) * 46 / 100;
        ListView_SetColumnWidth(main_control(Jobs), 0, px(name_width));
        ListView_SetColumnWidth(main_control(Jobs), 1, px(86));
        ListView_SetColumnWidth(main_control(Jobs), 2, px(width - 62 - name_width - 86));
        move(main_control(JobDetail), 20, 276 + list_height, width - 40, 88);
        move(main_control(Open), 20, height - 50, 188, 32);
        move(main_control(Retry), 220, height - 50, 132, 32);
        move(main_control(Remove), 364, height - 50, 188, 32);
        move(main_control(SettingsSection),20,68,width-40,180);
        move(page, 0, 112, width, height - 212);
        RECT viewport{}; GetClientRect(page, &viewport);
        SCROLLINFO info{sizeof(info), SIF_RANGE | SIF_PAGE | SIF_POS};
        info.nMin = 0; info.nMax = px(section_height()) - 1; info.nPage = static_cast<UINT>(std::max(0L, viewport.bottom));
        scroll = std::clamp(scroll, 0, std::max(0, px(section_height()) - static_cast<int>(viewport.bottom))); info.nPos = scroll;
        SetScrollInfo(page, SB_VERT, &info, TRUE);
        move(main_control(SettingsError), 20, height - 91, width - 40, 35);
        move(main_control(Defaults), 20, height - 47, 154, 32);
        move(main_control(Apply), width - 230, height - 47, 210, 32);
        layout_page();
    }
    void switch_tab(bool settings) {
        settings_visible = settings;
        TabCtrl_SetCurSel(main_control(Tabs), settings ? 1 : 0);
        for (HWND control : task_controls) ShowWindow(control, settings ? SW_HIDE : SW_SHOW);
        for (HWND control : settings_controls) ShowWindow(control, settings ? SW_SHOW : SW_HIDE);
        ShowWindow(page, settings ? SW_SHOW : SW_HIDE);
        if (!settings && (!state.running || state.percent < 0)) ShowWindow(main_control(Progress), SW_HIDE);
        layout();
    }
    const PanelRow* selected_row() const {
        const int selected = ListView_GetNextItem(main_control(Jobs), -1, LVNI_SELECTED);
        return selected >= 0 && static_cast<size_t>(selected) < state.jobs.size() ? &state.jobs[static_cast<size_t>(selected)] : nullptr;
    }
    void selection_changed() {
        const auto* row = selected_row();
        EnableWindow(main_control(Open), row && row->openable && static_cast<bool>(actions.open_result));
        EnableWindow(main_control(Retry), row && row->retryable && static_cast<bool>(actions.retry));
        EnableWindow(main_control(Remove), row && row->removable && static_cast<bool>(actions.remove));
        set_text(main_control(JobDetail), row ? row->detail : L"Здесь появятся задачи и результаты текущего сеанса.");
    }
    void update_state(const PanelState& next) {
        unfiltered=next;
        if (!window || !main_control(Jobs)) { state = next; return; }
        const auto* selected = selected_row();
        const uint64_t selected_id = selected ? selected->id : 0;
        const bool had_selection = selected != nullptr;
        auto next_copy = next;
        auto lower=[](std::wstring value){if(!value.empty())CharLowerBuffW(value.data(),static_cast<DWORD>(value.size()));return value;};
        auto query=lower(text_of(main_control(HistorySearch)));
        if(!query.empty())std::erase_if(next_copy.jobs,[&](const auto& row){return row.retryable&&lower(row.name+L" "+row.target+L" "+row.status+L" "+row.detail).find(query)==std::wstring::npos;});
        const bool same_rows=state.jobs.size()==next_copy.jobs.size()&&std::equal(state.jobs.begin(),state.jobs.end(),next_copy.jobs.begin(),[](const auto& a,const auto& b){return a.id==b.id;});
        const auto previous = std::move(state);
        state = std::move(next_copy);
        set_text(main_control(Headline), state.headline.empty() ? L"Готово к работе" : state.headline);
        set_text(main_control(Summary), state.detail);
        set_text(setting(UpdateInfo),state.update_detail.empty()?L"Проверка обновлений ещё не выполнялась.":state.update_detail);
        EnableWindow(setting(CheckUpdate),!state.update_checking&&static_cast<bool>(actions.check_update));
        EnableWindow(setting(InstallUpdate),state.update_ready&&!state.update_checking&&static_cast<bool>(actions.install_update));
        EnableWindow(setting(LaterUpdate),state.update_available&&static_cast<bool>(actions.later_update));
        if(state.modules!=previous.modules){
            auto module_selection=SendMessageW(setting(Modules),CB_GETCURSEL,0,0);SendMessageW(setting(Modules),CB_RESETCONTENT,0,0);
            for(auto& row:state.modules)SendMessageW(setting(Modules),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(row.c_str()));
            SendMessageW(setting(Modules),CB_SETCURSEL,module_selection<0?0:module_selection,0);
        }
        set_text(setting(ModuleInfo),state.module_detail);
        EnableWindow(setting(InstallModule),state.modules_allowed&&!state.modules_busy&&static_cast<bool>(actions.change_module));
        EnableWindow(setting(RemoveModule),state.modules_allowed&&!state.modules_busy&&static_cast<bool>(actions.change_module));
        EnableWindow(setting(CancelModule),state.modules_busy&&static_cast<bool>(actions.cancel_module));
        EnableWindow(setting(RefreshModules),!state.modules_busy&&static_cast<bool>(actions.refresh_modules));
        set_text(main_control(ProgressText), state.running && state.percent >= 0 ? std::to_wstring(std::clamp(state.percent, 0, 100)) + L" %" : L"");
        SendMessageW(main_control(Progress), PBM_SETPOS, std::clamp(state.percent, 0, 100), 0);
        ShowWindow(main_control(Progress), !settings_visible && state.running && state.percent >= 0 ? SW_SHOW : SW_HIDE);
        EnableWindow(main_control(Cancel), state.running && static_cast<bool>(actions.cancel));
        const bool has_pending = std::any_of(state.jobs.begin(), state.jobs.end(), [](const auto& row) { return row.removable; });
        EnableWindow(main_control(Clear), has_pending && static_cast<bool>(actions.clear_queue));
        EnableWindow(main_control(Pause), (state.running || has_pending || state.queue_paused) && static_cast<bool>(actions.pause_queue));
        set_text(main_control(Pause),state.recovery_pending?L"Восстановить очередь":state.queue_paused?L"Продолжить очередь":L"Приостановить очередь");
        EnableWindow(main_control(ClearHistory),static_cast<bool>(actions.clear_history)&&std::any_of(unfiltered.jobs.begin(),unfiltered.jobs.end(),[](const auto& r){return r.retryable;}));
        HWND jobs = main_control(Jobs);
        rebuilding = true;
        if (!same_rows) { SendMessageW(jobs, WM_SETREDRAW, FALSE, 0); ListView_DeleteAllItems(jobs); }
        int selection = -1;
        for (size_t index = 0; index < state.jobs.size(); ++index) {
            const auto& row = state.jobs[index];
            const auto item_index = static_cast<int>(index);
            if (!same_rows) {
                LVITEMW item{}; item.mask = LVIF_TEXT; item.iItem = item_index; item.pszText = const_cast<wchar_t*>(row.name.c_str());
                ListView_InsertItem(jobs, &item);
            }
            if (same_rows && row.name != previous.jobs[index].name) ListView_SetItemText(jobs, item_index, 0, const_cast<wchar_t*>(row.name.c_str()));
            if (!same_rows || row.target != previous.jobs[index].target) ListView_SetItemText(jobs, item_index, 1, const_cast<wchar_t*>(row.target.c_str()));
            if (!same_rows || row.status != previous.jobs[index].status) ListView_SetItemText(jobs, item_index, 2, const_cast<wchar_t*>(row.status.c_str()));
            if (had_selection && row.id == selected_id) selection = item_index;
        }
        if (!same_rows) {
            if (selection < 0 && !state.jobs.empty()) selection = 0;
            if (selection >= 0) ListView_SetItemState(jobs, selection, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            SendMessageW(jobs, WM_SETREDRAW, TRUE, 0);
            InvalidateRect(jobs, nullptr, FALSE);
        }
        rebuilding = false;
        selection_changed();
    }
    void settings_error(const wchar_t* message) { set_text(main_control(SettingsError), message); }
    void save_visible_formats() {
        if (!setting(Formats)) return;
        std::vector<Format> visible;
        for (size_t i = 0; i < format_order.size(); ++i)
            if (ListView_GetCheckState(setting(Formats), static_cast<int>(i))) visible.push_back(format_order[i]);
        if (!visible.empty()) draft.wheels[categories[static_cast<size_t>(category_index)].category] = std::move(visible);
    }
    void fill_formats(int selected = 0) {
        const auto& category_entry = categories[static_cast<size_t>(category_index)];
        const auto all = formats_for(category_entry.sample);
        const auto found = draft.wheels.find(category_entry.category);
        const auto& enabled = found != draft.wheels.end() && !found->second.empty() ? found->second : all;
        format_order = enabled;
        for (const auto format : all) if (std::find(format_order.begin(), format_order.end(), format) == format_order.end()) format_order.push_back(format);
        rebuilding = true;
        ListView_DeleteAllItems(setting(Formats));
        for (size_t i = 0; i < format_order.size(); ++i) {
            LVITEMW item{}; item.mask = LVIF_TEXT; item.iItem = static_cast<int>(i); item.pszText = const_cast<wchar_t*>(label(format_order[i]));
            ListView_InsertItem(setting(Formats), &item);
            ListView_SetCheckState(setting(Formats), item.iItem, std::find(enabled.begin(), enabled.end(), format_order[i]) != enabled.end());
        }
        if (selected >= 0 && static_cast<size_t>(selected) < format_order.size())
            ListView_SetItemState(setting(Formats), selected, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        rebuilding = false;
        format_selection_changed();
    }
    void format_selection_changed() {
        int selected = ListView_GetNextItem(setting(Formats), -1, LVNI_SELECTED);
        EnableWindow(setting(MoveUp), selected > 0);
        EnableWindow(setting(MoveDown), selected >= 0 && static_cast<size_t>(selected + 1) < format_order.size());
    }
    void load_draft(const Settings& settings) {
        draft = settings;
        if (!page) return;
        rebuilding = true;
        SendMessageW(setting(Quality), CB_SETCURSEL, static_cast<WPARAM>(draft.conversion.quality), 0);
        SendMessageW(setting(Resources), CB_SETCURSEL, static_cast<WPARAM>(draft.conversion.resources), 0);
        SendMessageW(setting(Acceleration), CB_SETCURSEL, static_cast<WPARAM>(draft.conversion.video_acceleration), 0);
        SendMessageW(setting(ImageMetadataChoice), CB_SETCURSEL, static_cast<WPARAM>(draft.conversion.image_metadata), 0);
        SendMessageW(setting(Codec), CB_SETCURSEL, static_cast<WPARAM>(draft.conversion.video_codec), 0);
        set_text(setting(ImageSize), std::to_wstring(draft.conversion.image_max_dimension));
        set_text(setting(VideoSize), std::to_wstring(draft.conversion.video_max_height));
        set_text(setting(AudioBitrate), std::to_wstring(draft.conversion.audio_bitrate_kbps));
        set_text(setting(PdfDpi), std::to_wstring(draft.conversion.pdf_dpi));
        Button_SetCheck(setting(PdfOcr), draft.conversion.pdf_ocr ? BST_CHECKED : BST_UNCHECKED);
        Button_SetCheck(setting(OcrEnglish), draft.conversion.ocr_languages.find("eng") != std::string::npos ? BST_CHECKED : BST_UNCHECKED);
        Button_SetCheck(setting(OcrRussian), draft.conversion.ocr_languages.find("rus") != std::string::npos ? BST_CHECKED : BST_UNCHECKED);
        Button_SetCheck(setting(OcrAzerbaijani), draft.conversion.ocr_languages.find("aze") != std::string::npos ? BST_CHECKED : BST_UNCHECKED);
        SendMessageW(setting(Notifications),CB_SETCURSEL,static_cast<WPARAM>(draft.notifications),0);
        Button_SetCheck(setting(Startup), draft.start_with_windows ? BST_CHECKED : BST_UNCHECKED);
        Button_SetCheck(setting(AutoUpdate), draft.automatic_updates ? BST_CHECKED : BST_UNCHECKED);
        set_text(setting(UpdateChannel), draft.update_channel);
        set_text(setting(Output), draft.output_directory.wstring());
        Button_SetCheck(setting(AutoShow), draft.auto_show_progress ? BST_CHECKED : BST_UNCHECKED);
        SendMessageW(setting(CategoryChoice), CB_SETCURSEL, category_index, 0);
        rebuilding = false;
        fill_formats(); settings_error(L"");
    }
    bool read_settings(Settings& result) {
        save_visible_formats();
        result = draft;
        const auto notifications=SendMessageW(setting(Notifications),CB_GETCURSEL,0,0);
        if(notifications<0 || notifications>2) {settings_error(L"Выберите режим уведомлений.");return false;}
        result.notifications=static_cast<NotificationMode>(notifications);
        result.start_with_windows = Button_GetCheck(setting(Startup)) == BST_CHECKED;
        result.automatic_updates = Button_GetCheck(setting(AutoUpdate)) == BST_CHECKED;
        result.update_channel = text_of(setting(UpdateChannel));
        const auto quality = SendMessageW(setting(Quality), CB_GETCURSEL, 0, 0);
        const auto resources = SendMessageW(setting(Resources), CB_GETCURSEL, 0, 0);
        const auto acceleration = SendMessageW(setting(Acceleration), CB_GETCURSEL, 0, 0);
        if (acceleration < 0 || acceleration > 1) { settings_error(L"Выберите режим кодирования видео."); return false; }
        result.conversion.video_acceleration = static_cast<VideoAcceleration>(acceleration);
        const auto metadata = SendMessageW(setting(ImageMetadataChoice), CB_GETCURSEL, 0, 0);
        if (metadata < 0 || metadata > 2) { settings_error(L"Выберите режим метаданных изображения."); return false; }
        result.conversion.image_metadata = static_cast<ImageMetadata>(metadata);
        const auto codec = SendMessageW(setting(Codec), CB_GETCURSEL, 0, 0);
        if (codec < 0 || codec > 3) { settings_error(L"Выберите видеокодек."); return false; }
        result.conversion.video_codec = static_cast<VideoCodec>(codec);
        if (quality < 0 || resources < 0) { settings_error(L"Выберите качество и использование ресурсов."); return false; }
        result.conversion.quality = static_cast<QualityPreset>(quality);
        result.conversion.resources = static_cast<ResourceMode>(resources);
        const auto number = [&](int id, unsigned& value, const wchar_t* error) {
            if (unsigned_value(setting(id), value)) return true;
            settings_error(error); SetFocus(setting(id)); SendMessageW(setting(id), EM_SETSEL, 0, -1); return false;
        };
        if (!number(ImageSize, result.conversion.image_max_dimension, L"Размер изображения: введите целое неотрицательное число.")) return false;
        if (!number(VideoSize, result.conversion.video_max_height, L"Высота видео: введите целое неотрицательное число.")) return false;
        if (!number(AudioBitrate, result.conversion.audio_bitrate_kbps, L"Битрейт аудио: введите целое неотрицательное число.")) return false;
        if (!number(PdfDpi, result.conversion.pdf_dpi, L"Разрешение PDF: введите целое положительное число.")) return false;
        if (result.conversion.image_max_dimension > 32768) { settings_error(L"Размер изображения должен быть от 1 до 32768 пикселей; 0 — исходный размер."); return false; }
        if (result.conversion.video_max_height == 1 || result.conversion.video_max_height > 8640) { settings_error(L"Высота видео должна быть от 2 до 8640 пикселей; 0 — исходная высота."); return false; }
        if (result.conversion.audio_bitrate_kbps != 0 && (result.conversion.audio_bitrate_kbps < 32 || result.conversion.audio_bitrate_kbps > 320)) { settings_error(L"Битрейт аудио должен быть от 32 до 320 кбит/с; 0 — выбор по качеству."); return false; }
        if (result.conversion.pdf_dpi < 36 || result.conversion.pdf_dpi > 600) { settings_error(L"Разрешение PDF должно быть от 36 до 600 DPI."); return false; }
        result.conversion.pdf_ocr = Button_GetCheck(setting(PdfOcr)) == BST_CHECKED;
        result.conversion.ocr_languages.clear();
        for (const auto& language : {std::pair{OcrEnglish, "eng"}, std::pair{OcrRussian, "rus"}, std::pair{OcrAzerbaijani, "aze"}}) {
            if (Button_GetCheck(setting(language.first)) != BST_CHECKED) continue;
            if (!result.conversion.ocr_languages.empty()) result.conversion.ocr_languages += '+';
            result.conversion.ocr_languages += language.second;
        }
        if (result.conversion.ocr_languages.empty()) { settings_error(L"Выберите хотя бы один язык распознавания сканов."); return false; }
        try { validate_options(result.conversion); }
        catch (...) { settings_error(L"Проверьте размеры, битрейт и DPI: одно из значений выходит за допустимый диапазон."); return false; }
        auto output = text_of(setting(Output));
        if (output.find_first_of(L"\r\n\"<>|?*") != std::wstring::npos) { settings_error(L"Путь к папке содержит недопустимые символы."); return false; }
        result.output_directory = std::move(output);
        if (!result.output_directory.empty() && !result.output_directory.is_absolute()) { settings_error(L"Укажите полный путь к папке или оставьте поле пустым."); return false; }
        result.auto_show_progress = Button_GetCheck(setting(AutoShow)) == BST_CHECKED;
        return true;
    }
    void apply() {
        Settings result;
        if (!read_settings(result)) return;
        if (!actions.save) { settings_error(L"Сохранение настроек сейчас недоступно."); return; }
        try { actions.save(result); draft = std::move(result); settings_error(L"Настройки сохранены. Они применятся к новым задачам."); }
        catch (const std::exception& error) { settings_error((L"Не удалось сохранить настройки. " + exception_reason(error)).c_str()); }
        catch (...) { settings_error(L"Не удалось сохранить настройки. Проверьте доступ к папке программы и повторите попытку."); }
    }
    void browse() {
        Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
        if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
            settings_error(L"Не удалось открыть выбор папки. Введите полный путь вручную."); return;
        }
        DWORD options = 0; dialog->GetOptions(&options);
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR);
        dialog->SetTitle(L"Папка для результатов ShiftWheel");
        const auto current = text_of(setting(Output));
        Microsoft::WRL::ComPtr<IShellItem> folder;
        if (!current.empty() && SUCCEEDED(SHCreateItemFromParsingName(current.c_str(), nullptr, IID_PPV_ARGS(&folder)))) dialog->SetFolder(folder.Get());
        if (SUCCEEDED(dialog->Show(window))) {
            Microsoft::WRL::ComPtr<IShellItem> item;
            PWSTR path = nullptr;
            if (SUCCEEDED(dialog->GetResult(&item)) && SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                SetWindowTextW(setting(Output), path); CoTaskMemFree(path); settings_error(L"");
            }
        }
    }
    void move_format(int direction) {
        const int selected = ListView_GetNextItem(setting(Formats), -1, LVNI_SELECTED);
        const int target = selected + direction;
        if (selected < 0 || target < 0 || static_cast<size_t>(target) >= format_order.size()) return;
        std::vector<bool> checked;
        for (size_t i = 0; i < format_order.size(); ++i) checked.push_back(ListView_GetCheckState(setting(Formats), static_cast<int>(i)) != FALSE);
        std::swap(format_order[static_cast<size_t>(selected)], format_order[static_cast<size_t>(target)]);
        const bool swap_value = checked[static_cast<size_t>(selected)];
        checked[static_cast<size_t>(selected)] = checked[static_cast<size_t>(target)]; checked[static_cast<size_t>(target)] = swap_value;
        rebuilding = true;
        for (int i : {selected, target}) {
            ListView_SetItemText(setting(Formats), i, 0, const_cast<wchar_t*>(label(format_order[static_cast<size_t>(i)])));
            ListView_SetCheckState(setting(Formats), i, checked[static_cast<size_t>(i)]);
        }
        ListView_SetItemState(setting(Formats), -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_SetItemState(setting(Formats), target, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        rebuilding = false;
        save_visible_formats(); format_selection_changed(); settings_error(L"");
    }
    void command(int id, int notification) {
        if (rebuilding) return;
        if (id == IDCANCEL) { ShowWindow(window, SW_HIDE); return; }
        if (id == IDOK) { if (settings_visible) apply(); return; }
        if (id == CategoryChoice && notification == CBN_SELCHANGE) {
            save_visible_formats(); category_index = static_cast<int>(SendMessageW(setting(CategoryChoice), CB_GETCURSEL, 0, 0));
            category_index = std::clamp(category_index, 0, static_cast<int>(categories.size()) - 1); fill_formats(); return;
        }
        if(id == SettingsSection && notification == CBN_SELCHANGE) {
            settings_section=static_cast<int>(SendMessageW(main_control(SettingsSection),CB_GETCURSEL,0,0));
            if(settings_section<0 || settings_section>4) settings_section=0;
            scroll=0;layout();return;
        }
        if(id==Modules&&notification==CBN_SELCHANGE){auto index=SendMessageW(setting(Modules),CB_GETCURSEL,0,0);if(index>=0&&actions.select_module)actions.select_module(static_cast<size_t>(index));return;}
        if(notification==BN_CLICKED&&(id==InstallModule||id==RemoveModule||id==CancelModule||id==RefreshModules)){
            if(!IsWindowEnabled(setting(id)))return;
            try{
                auto index=SendMessageW(setting(Modules),CB_GETCURSEL,0,0);
                if((id==InstallModule||id==RemoveModule)&&index>=0&&actions.change_module)actions.change_module(static_cast<size_t>(index),id==RemoveModule);
                if(id==CancelModule&&actions.cancel_module)actions.cancel_module();
                if(id==RefreshModules&&actions.refresh_modules)actions.refresh_modules();
            }catch(const std::exception& e){settings_error(exception_reason(e).c_str());}return;
        }
        if(id == ExportDiagnostics && actions.export_diagnostics) {
            try {actions.export_diagnostics();}catch(const std::exception& e){settings_error((L"Не удалось сохранить диагностику. "+exception_reason(e)).c_str());}
            return;
        }
        if(notification==BN_CLICKED&&(id==CheckUpdate||id==InstallUpdate||id==LaterUpdate)){
            if(!IsWindowEnabled(setting(id)))return;
            try{if(id==CheckUpdate&&actions.check_update)actions.check_update();if(id==InstallUpdate&&actions.install_update)actions.install_update();if(id==LaterUpdate&&actions.later_update)actions.later_update();}
            catch(const std::exception& e){settings_error(exception_reason(e).c_str());}return;
        }
        if(id==HistorySearch&&notification==EN_CHANGE){update_state(unfiltered);return;}
        if (notification == EN_CHANGE || notification == CBN_SELCHANGE) { settings_error(L""); return; }
        if (notification != BN_CLICKED) return;
        if (id == Browse) { browse(); return; }
        if (id == Apply) { apply(); return; }
        if (id == Defaults) { load_draft(Settings{}); settings_error(L"Настройки по умолчанию выбраны. Нажмите «Сохранить настройки»."); return; }
        if (id == MoveUp || id == MoveDown) { move_format(id == MoveUp ? -1 : 1); return; }
        HWND button = main_control(id);
        if (!button || !IsWindowEnabled(button)) return;
        const auto* row = selected_row();
        const uint64_t row_id = row ? row->id : 0;
        try {
            if (id == Cancel && actions.cancel) actions.cancel();
            if (id == Pause && actions.pause_queue) actions.pause_queue();
            if (id == Clear && actions.clear_queue) actions.clear_queue();
            if (id == ClearHistory && actions.clear_history) actions.clear_history();
            if (id == Open && row && row->openable && actions.open_result) actions.open_result(row_id);
            if (id == Retry && row && row->retryable && actions.retry) actions.retry(row_id);
            if (id == Remove && row && row->removable && actions.remove) actions.remove(row_id);
        } catch (const std::exception& error) { set_text(main_control(JobDetail), L"Не удалось выполнить действие. " + exception_reason(error)); }
        catch (...) { set_text(main_control(JobDetail), L"Не удалось выполнить действие. Проверьте доступ к исходному файлу и папке результата."); }
    }
    LRESULT notify(NMHDR* header) {
        if (rebuilding || !header) return 0;
        if (header->idFrom == Tabs && header->code == TCN_SELCHANGE) switch_tab(TabCtrl_GetCurSel(main_control(Tabs)) == 1);
        if (header->idFrom == Jobs && header->code == LVN_ITEMCHANGED) selection_changed();
        if (header->idFrom == Jobs && header->code == NM_DBLCLK) command(Open, BN_CLICKED);
        if (header->idFrom == Formats && header->code == LVN_ITEMCHANGING) {
            const auto* change = reinterpret_cast<NMLISTVIEW*>(header);
            if ((change->uChanged & LVIF_STATE) && (change->uOldState & LVIS_STATEIMAGEMASK) == INDEXTOSTATEIMAGEMASK(2) &&
                (change->uNewState & LVIS_STATEIMAGEMASK) == INDEXTOSTATEIMAGEMASK(1)) {
                int checked = 0;
                for (size_t i = 0; i < format_order.size(); ++i) if (ListView_GetCheckState(setting(Formats), static_cast<int>(i))) ++checked;
                if (checked <= 1) { settings_error(L"В каждой категории должен остаться хотя бы один формат."); return TRUE; }
            }
        }
        if (header->idFrom == Formats && header->code == LVN_ITEMCHANGED) { format_selection_changed(); save_visible_formats(); }
        return 0;
    }
    void scroll_page(int code, int wheel = 0) {
        SCROLLINFO info{sizeof(info), SIF_ALL}; GetScrollInfo(page, SB_VERT, &info);
        int position = info.nPos;
        switch (code) {
        case SB_LINEUP: position -= px(32); break;
        case SB_LINEDOWN: position += px(32); break;
        case SB_PAGEUP: position -= static_cast<int>(info.nPage); break;
        case SB_PAGEDOWN: position += static_cast<int>(info.nPage); break;
        case SB_THUMBTRACK: position = info.nTrackPos; break;
        case SB_TOP: position = 0; break;
        case SB_BOTTOM: position = info.nMax; break;
        default: position -= wheel; break;
        }
        position = std::clamp(position, 0, std::max(0, info.nMax - static_cast<int>(info.nPage) + 1));
        if (position != scroll) {
            scroll = position; info.fMask = SIF_POS; info.nPos = scroll; SetScrollInfo(page, SB_VERT, &info, TRUE);
            layout_page(); InvalidateRect(page, nullptr, TRUE);
        }
    }
    void ensure_visible(HWND control) {
        if (!settings_visible || !page) return;
        RECT item{}, viewport{}; GetWindowRect(control, &item); GetClientRect(page, &viewport);
        MapWindowPoints(nullptr, page, reinterpret_cast<POINT*>(&item), 2);
        int delta = 0;
        if (item.top < px(4)) delta = item.top - px(4);
        else if (item.bottom > viewport.bottom - px(4)) delta = item.bottom - viewport.bottom + px(4);
        if (delta) scroll_page(-1, -delta);
    }
    static LRESULT CALLBACK control_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam,
                                         UINT_PTR subclass_id, DWORD_PTR reference) {
        auto* self = reinterpret_cast<Impl*>(reference);
        if (message == WM_SETFOCUS) self->ensure_visible(hwnd);
        if (message == WM_NCDESTROY) RemoveWindowSubclass(hwnd, control_proc, subclass_id);
        return DefSubclassProc(hwnd, message, wparam, lparam);
    }
    LRESULT colors(HWND control, HDC dc) const {
        SetBkColor(dc, RGB(250, 251, 253));
        SetTextColor(dc, control == main_control(SettingsError) ? RGB(126, 48, 30) : RGB(28, 38, 54));
        return reinterpret_cast<LRESULT>(background);
    }
    static LRESULT CALLBACK page_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
        auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (self) {
            switch (message) {
            case WM_COMMAND: self->command(LOWORD(wparam), HIWORD(wparam)); return 0;
            case WM_NOTIFY: return self->notify(reinterpret_cast<NMHDR*>(lparam));
            case WM_VSCROLL: self->scroll_page(LOWORD(wparam)); return 0;
            case WM_MOUSEWHEEL: self->scroll_page(-1, MulDiv(GET_WHEEL_DELTA_WPARAM(wparam), self->px(96), WHEEL_DELTA)); return 0;
            case WM_CTLCOLORSTATIC: return self->colors(reinterpret_cast<HWND>(lparam), reinterpret_cast<HDC>(wparam));
            case WM_ERASEBKGND: { RECT rect{}; GetClientRect(hwnd, &rect); FillRect(reinterpret_cast<HDC>(wparam), &rect, self->background); return 1; }
            }
        }
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }
    static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
        auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (self) {
            switch (message) {
            case WM_CLOSE: ShowWindow(hwnd, SW_HIDE); return 0;
            case WM_COMMAND: self->command(LOWORD(wparam), HIWORD(wparam)); return 0;
            case WM_NOTIFY: return self->notify(reinterpret_cast<NMHDR*>(lparam));
            case WM_SIZE: self->layout(); return 0;
            case WM_MOUSEWHEEL: if (self->settings_visible) { self->scroll_page(-1, MulDiv(GET_WHEEL_DELTA_WPARAM(wparam), self->px(96), WHEEL_DELTA)); return 0; } break;
            case WM_GETMINMAXINFO: {
                auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
                info->ptMinTrackSize = {self->px(740), self->px(540)}; return 0;
            }
            case WM_DPICHANGED: {
                self->dpi = HIWORD(wparam); self->make_fonts();
                const auto* rect = reinterpret_cast<RECT*>(lparam);
                SetWindowPos(hwnd, nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top, SWP_NOACTIVATE | SWP_NOZORDER);
                self->layout(); return 0;
            }
            case WM_CTLCOLORSTATIC: return self->colors(reinterpret_cast<HWND>(lparam), reinterpret_cast<HDC>(wparam));
            case WM_ERASEBKGND: { RECT rect{}; GetClientRect(hwnd, &rect); FillRect(reinterpret_cast<HDC>(wparam), &rect, self->background); return 1; }
            case WM_NCDESTROY: self->window = nullptr; self->page = nullptr; SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0); break;
            }
        }
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }
};

ControlPanel::ControlPanel() : impl_(std::make_unique<Impl>()) {}
ControlPanel::~ControlPanel() = default;
HRESULT ControlPanel::initialize(HINSTANCE instance, const Settings& settings, PanelActions actions) {
    if (impl_->window) return S_FALSE;
    impl_->instance = instance; impl_->draft = settings; impl_->actions = std::move(actions);
    INITCOMMONCONTROLSEX common{sizeof(common), ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES | ICC_PROGRESS_CLASS};
    if (!InitCommonControlsEx(&common)) return HRESULT_FROM_WIN32(GetLastError());
    WNDCLASSEXW type{}; type.cbSize = sizeof(type); type.hInstance = instance; type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    type.lpfnWndProc = Impl::window_proc; type.lpszClassName = panel_class;
    type.hIcon = LoadIconW(nullptr, IDI_APPLICATION); type.hIconSm = type.hIcon;
    if (!RegisterClassExW(&type) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return HRESULT_FROM_WIN32(GetLastError());
    type.lpfnWndProc = Impl::page_proc; type.lpszClassName = page_class;
    if (!RegisterClassExW(&type) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return HRESULT_FROM_WIN32(GetLastError());
    impl_->window = CreateWindowExW(WS_EX_CONTROLPARENT, panel_class, L"ShiftWheel", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 840, 760, nullptr, nullptr, instance, impl_.get());
    if (!impl_->window) return HRESULT_FROM_WIN32(GetLastError());
    impl_->dpi = GetDpiForWindow(impl_->window);
    impl_->make_fonts();
    if (!impl_->create_controls()) { shutdown(); return E_OUTOFMEMORY; }
    RECT work{}; SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int width = std::min(impl_->px(840), static_cast<int>(work.right - work.left));
    const int height = std::min(impl_->px(790), static_cast<int>(work.bottom - work.top));
    SetWindowPos(impl_->window, nullptr, work.left + std::max(0L, (work.right - work.left - width) / 2),
        work.top + std::max(0L, (work.bottom - work.top - height) / 2), width, height, SWP_NOACTIVATE | SWP_NOZORDER);
    impl_->layout();
    return S_OK;
}
void ControlPanel::update(const PanelState& state) { impl_->update_state(state); }
void ControlPanel::show(bool settings, bool activate) {
    if (!impl_->window) return;
    impl_->switch_tab(settings);
    if (activate) {
        ShowWindow(impl_->window, IsIconic(impl_->window) ? SW_RESTORE : SW_SHOW);
        SetForegroundWindow(impl_->window);
    } else ShowWindow(impl_->window, SW_SHOWNOACTIVATE);
}
void ControlPanel::set_settings(const Settings& settings) { impl_->load_draft(settings); }
void ControlPanel::shutdown() {
    if (impl_->window) DestroyWindow(impl_->window);
    impl_->window = nullptr; impl_->page = nullptr;
    impl_->task_controls.clear(); impl_->settings_controls.clear();
}
HWND ControlPanel::handle() const { return impl_->window; }
}
