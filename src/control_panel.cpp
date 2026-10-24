#include "control_panel.h"
#include "resource.h"
#include "panel_theme.h"
#include "profiles.h"
#include <dwmapi.h>
#include <uxtheme.h>
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
    Codec=320,CodecLabel,ImageMetadataChoice,ImageMetadataLabel,
    SectionTitle=350,SectionHint,
    ProfileCategory=400,ProfileChoice,ProfileActive,ProfileName,ProfileFormat,ProfileNew,ProfileCopy,ProfileDelete,ProfilePhoto,ProfileVideo,ProfileAudio,
    ProfileQuality=420,ProfileResources,ProfileSize,ProfileCodec,ProfileAcceleration,ProfileMetadata,ProfileOcr,ProfileEnglish,ProfileRussian,ProfileAzerbaijani,
    ProfileCategoryLabel=440,ProfileActiveLabel,ProfileChoiceLabel,ProfileNameLabel,ProfileFormatLabel,ProfileConsequences,
    ProfileQualityLabel=450,ProfileResourcesLabel,ProfileSizeLabel,ProfileCodecLabel,ProfileAccelerationLabel,ProfileMetadataLabel
};
constexpr std::array section_names{L"Основные",L"Конвертация",L"Колесо",L"Обновления",L"Модули"};
constexpr std::array section_hints{
    L"Работа приложения и сохранение готовых файлов.",
    L"Параметры новых задач. 0 — исходный размер или автоматический битрейт.",
    L"Оставьте нужные форматы и задайте порядок в меню Shift.",
    L"Проверка и установка новых версий приложения.",
    L"Добавляйте компоненты для нужных типов файлов."};
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
    HFONT font = nullptr, heading_font = nullptr, section_font = nullptr;
    panel_theme::Palette palette = panel_theme::colors();
    HBRUSH background = CreateSolidBrush(palette.canvas), surface = CreateSolidBrush(palette.surface), input = CreateSolidBrush(palette.input);
    struct Card { int top, height; const wchar_t* title; };
    std::vector<Card> cards;
    bool feedback_success = false;
    bool scroll_drag = false;
    int scroll_grab = 0;
    UINT dpi = 96;
    int scroll = 0, category_index = 0, settings_section = 0;
    bool settings_visible = false, rebuilding = false;
    Settings draft;
    PanelState state;
    PanelState unfiltered;
    PanelActions actions;
    std::vector<HWND> task_controls, settings_controls;
    std::vector<Format> format_order;
    int profile_category_index=0,profile_index=-1;
    std::vector<int> profile_indices;
    std::vector<Format> profile_format_order;

    ~Impl() {
        if (window) DestroyWindow(window);
        if (font) DeleteObject(font);
        if (heading_font) DeleteObject(heading_font);
        if (section_font) DeleteObject(section_font);
        if (background) DeleteObject(background);
        if (surface) DeleteObject(surface);
        if (input) DeleteObject(input);
    }
    int px(int value) const { return MulDiv(value, static_cast<int>(dpi), 96); }
    HWND main_control(int id) const { return GetDlgItem(window, id); }
    HWND setting(int id) const { return GetDlgItem(page, id); }
    HWND add(HWND parent, int id, const wchar_t* type, const wchar_t* text,
             DWORD style = 0, DWORD extended = 0) {
        if (lstrcmpiW(type,L"COMBOBOX")==0) style |= CBS_OWNERDRAWFIXED | CBS_HASSTRINGS;
        if (lstrcmpiW(type,L"EDIT")==0) { extended &= ~WS_EX_CLIENTEDGE; style |= WS_BORDER; }
        if (lstrcmpiW(type,WC_LISTVIEWW)==0) { extended &= ~WS_EX_CLIENTEDGE; style |= WS_BORDER; }
        HWND control = CreateWindowExW(extended, type, text, WS_CHILD | WS_VISIBLE | style,
            0, 0, 0, 0, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
        if (control) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
        if (control && (lstrcmpiW(type,L"COMBOBOX")==0 || lstrcmpiW(type,L"EDIT")==0)) SetWindowTheme(control,L"",L"");
        if (control && lstrcmpiW(type,L"EDIT")==0) SendMessageW(control,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELPARAM(px(8),px(8)));
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
        HFONT old = font, old_heading = heading_font, old_section = section_font;
        font = CreateFontW(-MulDiv(11, static_cast<int>(dpi), 72), 0, 0, 0, FW_NORMAL,
            FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        heading_font = CreateFontW(-MulDiv(18, static_cast<int>(dpi), 72), 0, 0, 0, FW_SEMIBOLD,
            FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        section_font = CreateFontW(-MulDiv(11, static_cast<int>(dpi), 72), 0, 0, 0, FW_SEMIBOLD,
            FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        if (window) {
            EnumChildWindows(window, [](HWND child, LPARAM value) -> BOOL {
                SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(value), TRUE); return TRUE;
            }, reinterpret_cast<LPARAM>(font));
            SendMessageW(main_control(Headline), WM_SETFONT, reinterpret_cast<WPARAM>(heading_font), TRUE);
            SendMessageW(setting(SectionTitle),WM_SETFONT,reinterpret_cast<WPARAM>(heading_font),TRUE);
            SendMessageW(main_control(SettingsSection),LB_SETITEMHEIGHT,0,px(48));
            TabCtrl_SetItemSize(main_control(Tabs),px(120),px(36));
            EnumChildWindows(window,[](HWND control,LPARAM value)->BOOL {
                auto* self=reinterpret_cast<Impl*>(value);wchar_t type[32]{};GetClassNameW(control,type,32);
                if(lstrcmpiW(type,L"COMBOBOX")==0) {
                    SendMessageW(control,CB_SETITEMHEIGHT,static_cast<WPARAM>(-1),self->px(26));
                    SendMessageW(control,CB_SETITEMHEIGHT,0,self->px(30));
                    SendMessageW(control,CB_SETDROPPEDWIDTH,0,self->px(GetDlgCtrlID(control)==ImageMetadataChoice?680:360));
                }
                return TRUE;
            },reinterpret_cast<LPARAM>(this));
        }
        if (old) DeleteObject(old);
        if (old_heading) DeleteObject(old_heading);
        if (old_section) DeleteObject(old_section);
    }
    bool create_controls() {
        add(window, Tabs, WC_TABCONTROLW, L"", WS_TABSTOP | TCS_FIXEDWIDTH | TCS_OWNERDRAWFIXED);
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

        settings_controls.push_back(add(window, SettingsSection, L"LISTBOX", L"", WS_TABSTOP | LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT));
        for(auto section : section_names) SendMessageW(main_control(SettingsSection),LB_ADDSTRING,0,reinterpret_cast<LPARAM>(section));
        SendMessageW(main_control(SettingsSection),LB_SETCURSEL,0,0);
        page = CreateWindowExW(WS_EX_CONTROLPARENT, page_class, L"", WS_CHILD | WS_VSCROLL | WS_CLIPCHILDREN,
            0, 0, 0, 0, window, nullptr, instance, this);
        if (!page) return false;
        add(page,SectionTitle,L"STATIC",section_names[0],SS_LEFT);
        add(page,SectionHint,L"STATIC",section_hints[0],SS_LEFT);
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
        label_control(ImageLabel, L"Изображение, пиксели"); number(ImageSize);
        label_control(VideoLabel, L"Высота видео, пиксели"); number(VideoSize);
        label_control(AudioLabel, L"Битрейт аудио, кбит/с"); number(AudioBitrate);
        label_control(PdfLabel, L"PDF в изображение, DPI"); number(PdfDpi);
        label_control(AccelerationLabel, L"Ускорение кодирования"); combo(Acceleration);
        label_control(ImageMetadataLabel, L"JPEG/TIFF, без GPS; остальные — без метаданных"); combo(ImageMetadataChoice);
        for (const auto value : {L"Удалять EXIF/XMP", L"Только дата: JPEG/TIFF → JPEG/TIFF", L"Дата, автор, copyright: JPEG/TIFF/HEIC → JPEG/TIFF"}) SendMessageW(setting(ImageMetadataChoice), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value));
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
        for(auto id:{ProfileCategory,ProfileChoice,ProfileActive,ProfileFormat,ProfileQuality,ProfileResources,ProfileCodec,ProfileAcceleration,ProfileMetadata})combo(id);
        for(const auto& entry:categories)SendMessageW(setting(ProfileCategory),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(entry.name));
        add(page,ProfileName,L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL);SendMessageW(setting(ProfileName),EM_SETLIMITTEXT,64,0);number(ProfileSize);
        for(auto item:{std::pair{ProfileNew,L"Создать"},std::pair{ProfileCopy,L"Дублировать"},std::pair{ProfileDelete,L"Удалить"},std::pair{ProfilePhoto,L"Шаблон фото"},std::pair{ProfileVideo,L"Шаблон видео"},std::pair{ProfileAudio,L"Шаблон аудио"}})add(page,item.first,L"BUTTON",item.second,WS_TABSTOP);
        for(auto item:{std::pair{ProfileCategoryLabel,L"Категория профилей"},std::pair{ProfileActiveLabel,L"Активный профиль"},std::pair{ProfileChoiceLabel,L"Редактировать профиль"},std::pair{ProfileNameLabel,L"Название"},std::pair{ProfileFormatLabel,L"Выходной формат"},std::pair{ProfileQualityLabel,L"Качество"},std::pair{ProfileResourcesLabel,L"Ресурсы"},std::pair{ProfileSizeLabel,L"Размер"},std::pair{ProfileCodecLabel,L"Кодек"},std::pair{ProfileAccelerationLabel,L"Ускорение"},std::pair{ProfileMetadataLabel,L"Метаданные"}})label_control(item.first,item.second);
        add(page,ProfileConsequences,L"EDIT",L"",WS_TABSTOP|ES_READONLY|ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL);
        for(auto item:{std::pair{ProfileOcr,L"OCR сканов PDF → TXT"},std::pair{ProfileEnglish,L"Английский"},std::pair{ProfileRussian,L"Русский"},std::pair{ProfileAzerbaijani,L"Азербайджанский"}})add(page,item.first,L"BUTTON",item.second,WS_TABSTOP|BS_AUTOCHECKBOX);
        for(auto pair:{std::pair{ProfileQuality,Quality},std::pair{ProfileResources,Resources},std::pair{ProfileCodec,Codec},std::pair{ProfileAcceleration,Acceleration},std::pair{ProfileMetadata,ImageMetadataChoice}}){
            for(LRESULT i=0;i<SendMessageW(setting(pair.second),CB_GETCOUNT,0,0);++i){wchar_t value[256]{};SendMessageW(setting(pair.second),CB_GETLBTEXT,i,reinterpret_cast<LPARAM>(value));SendMessageW(setting(pair.first),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(value));}
        }
        settings_controls.push_back(add(window, SettingsError, L"STATIC", L"", SS_LEFT | SS_NOPREFIX));
        settings_controls.push_back(add(window, Defaults, L"BUTTON", L"По умолчанию", WS_TABSTOP));
        settings_controls.push_back(add(window, Apply, L"BUTTON", L"Сохранить настройки", WS_TABSTOP | BS_DEFPUSHBUTTON));
        EnumChildWindows(window, [](HWND control, LPARAM value) -> BOOL {
            SetWindowSubclass(control, control_proc, 1, static_cast<DWORD_PTR>(value)); return TRUE;
        }, reinterpret_cast<LPARAM>(this));
        for(const auto id : {Jobs,Formats}) {
            const auto list=id==Jobs?main_control(id):setting(id);
            ListView_SetBkColor(list,palette.input);ListView_SetTextBkColor(list,palette.input);ListView_SetTextColor(list,palette.text);
        }
        make_fonts();
        load_draft(draft);
        update_state(state);
        switch_tab(false);
        return std::all_of(task_controls.begin(), task_controls.end(), [](HWND control) { return control != nullptr; }) &&
            std::all_of(settings_controls.begin(), settings_controls.end(), [](HWND control) { return control != nullptr; });
    }

    ConversionProfile* editing_profile(){return profile_index>=0&&profile_index<static_cast<int>(draft.profiles.size())?&draft.profiles[profile_index]:nullptr;}
    int profile_offset() const {
        if(profile_index<0||profile_index>=static_cast<int>(draft.profiles.size()))return 480;
        auto c=draft.profiles[profile_index].category;
        int rows=profile_extracts_audio(draft.profiles[profile_index])?2:c==Category::Image||c==Category::Video||c==Category::Pdf?3:c==Category::Audio?2:0;
        return 640+74*rows;
    }
    void profile_hint(){
        auto*p=editing_profile();std::wstring hint;
        if(!p){hint=L"Выберите профиль для редактирования или создайте новый. Активный профиль применяется только к своему формату; остальные секторы используют общие параметры ниже. Изменения вступают в силу после «Сохранить настройки».";auto a=draft.active_profiles.find(categories[profile_category_index].category);if(a!=draft.active_profiles.end())for(auto&v:draft.profiles)if(v.id==a->second){p=&v;hint+=L"\r\nАктивен: "+p->name+L". ";break;}if(!p){set_text(setting(ProfileConsequences),hint);return;}}
        hint+=profile_consequences(*p);auto configured=configured_formats(category_sample(p->category),draft);
        if(std::find(configured.begin(),configured.end(),p->format)==configured.end())hint+=L"\r\nФормат скрыт в настройках колеса: профиль пока не применяется.";
        else if(available_formats(category_sample(p->category),{p->format},[&](auto id){auto it=state.installed_modules.find(std::string(id));return it!=state.installed_modules.end()&&it->second;}).empty())hint+=L"\r\nНужный модуль недоступен: формат не появится в колесе до установки.";
        set_text(setting(ProfileConsequences),hint);
    }
    bool save_profile_editor(bool feedback=true){
        auto*p=editing_profile();if(!p)return true;auto candidate=*p;
        candidate.name=text_of(setting(ProfileName));while(!candidate.name.empty()&&candidate.name.front()==L' ')candidate.name.erase(0,1);while(!candidate.name.empty()&&candidate.name.back()==L' ')candidate.name.pop_back();
        auto f=SendMessageW(setting(ProfileFormat),CB_GETCURSEL,0,0);if(f<0||static_cast<size_t>(f)>=profile_format_order.size())return false;candidate.format=profile_format_order[f];
        auto& o=candidate.options;auto c=candidate.category;bool okay=true;const bool sound=c==Category::Audio||profile_extracts_audio(candidate);
        if(c==Category::Image||c==Category::Video||c==Category::Audio){o.quality=static_cast<QualityPreset>(SendMessageW(setting(ProfileQuality),CB_GETCURSEL,0,0));o.resources=static_cast<ResourceMode>(SendMessageW(setting(ProfileResources),CB_GETCURSEL,0,0));}
        if(c==Category::Image)okay=unsigned_value(setting(ProfileSize),o.image_max_dimension);
        if(c==Category::Video&&!sound){okay=unsigned_value(setting(ProfileSize),o.video_max_height);o.video_codec=static_cast<VideoCodec>(SendMessageW(setting(ProfileCodec),CB_GETCURSEL,0,0));o.video_acceleration=static_cast<VideoAcceleration>(SendMessageW(setting(ProfileAcceleration),CB_GETCURSEL,0,0));}
        if(sound&&candidate.format!=Format::Flac&&candidate.format!=Format::Wav)okay=unsigned_value(setting(ProfileSize),o.audio_bitrate_kbps);
        if(c==Category::Image)o.image_metadata=static_cast<ImageMetadata>(SendMessageW(setting(ProfileMetadata),CB_GETCURSEL,0,0));
        if(c==Category::Pdf){if(candidate.format!=Format::Txt)okay=unsigned_value(setting(ProfileSize),o.pdf_dpi);else{o.pdf_ocr=Button_GetCheck(setting(ProfileOcr))==BST_CHECKED;o.ocr_languages.clear();for(auto pair:{std::pair{ProfileEnglish,"eng"},std::pair{ProfileRussian,"rus"},std::pair{ProfileAzerbaijani,"aze"}})if(Button_GetCheck(setting(pair.first))==BST_CHECKED){if(!o.ocr_languages.empty())o.ocr_languages+='+';o.ocr_languages+=pair.second;}}}
        try{auto next=draft;next.profiles[profile_index]=candidate;if(!okay)throw std::runtime_error("number");validate_profiles(next);*p=std::move(candidate);return true;}
        catch(...){if(feedback)settings_error(L"Профиль: проверьте уникальное имя (до 64 символов), размеры, языки и совместимость кодека/формата. Изменения оставлены в редакторе.");return false;}
    }
    void fill_profiles(){
        rebuilding=true;auto c=categories[profile_category_index].category;profile_indices.clear();
        SendMessageW(setting(ProfileCategory),CB_SETCURSEL,profile_category_index,0);
        for(auto id:{ProfileChoice,ProfileActive}){SendMessageW(setting(id),CB_RESETCONTENT,0,0);SendMessageW(setting(id),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(id==ProfileActive?L"Общие настройки":L"Выберите профиль…"));}
        int selected=0,active=0;
        for(size_t i=0;i<draft.profiles.size();++i)if(draft.profiles[i].category==c){profile_indices.push_back(static_cast<int>(i));auto index=static_cast<int>(profile_indices.size());for(auto id:{ProfileChoice,ProfileActive})SendMessageW(setting(id),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(draft.profiles[i].name.c_str()));if(static_cast<int>(i)==profile_index)selected=index;if(auto a=draft.active_profiles.find(c);a!=draft.active_profiles.end()&&a->second==draft.profiles[i].id)active=index;}
        SendMessageW(setting(ProfileChoice),CB_SETCURSEL,selected,0);SendMessageW(setting(ProfileActive),CB_SETCURSEL,active,0);
        auto*p=editing_profile();EnableWindow(setting(ProfileCopy),p!=nullptr);EnableWindow(setting(ProfileDelete),p!=nullptr);
        if(p){set_text(setting(ProfileName),p->name);profile_format_order=formats_for(category_sample(c));SendMessageW(setting(ProfileFormat),CB_RESETCONTENT,0,0);int chosen=0;for(size_t i=0;i<profile_format_order.size();++i){auto f=profile_format_order[i];SendMessageW(setting(ProfileFormat),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label(f)));if(f==p->format)chosen=static_cast<int>(i);}SendMessageW(setting(ProfileFormat),CB_SETCURSEL,chosen,0);
            auto&o=p->options;for(auto pair:{std::pair{ProfileQuality,static_cast<int>(o.quality)},std::pair{ProfileResources,static_cast<int>(o.resources)},std::pair{ProfileCodec,static_cast<int>(o.video_codec)},std::pair{ProfileAcceleration,static_cast<int>(o.video_acceleration)},std::pair{ProfileMetadata,static_cast<int>(o.image_metadata)}})SendMessageW(setting(pair.first),CB_SETCURSEL,pair.second,0);
            auto value=c==Category::Image?o.image_max_dimension:c==Category::Audio||profile_extracts_audio(*p)?o.audio_bitrate_kbps:c==Category::Video?o.video_max_height:o.pdf_dpi;set_text(setting(ProfileSize),std::to_wstring(value));Button_SetCheck(setting(ProfileOcr),o.pdf_ocr?BST_CHECKED:BST_UNCHECKED);for(auto pair:{std::pair{ProfileEnglish,"eng"},std::pair{ProfileRussian,"rus"},std::pair{ProfileAzerbaijani,"aze"}})Button_SetCheck(setting(pair.first),o.ocr_languages.find(pair.second)!=std::string::npos?BST_CHECKED:BST_UNCHECKED);
        }
        rebuilding=false;profile_hint();layout();
    }
    void new_profile(int kind,bool copy=false){
        if(draft.profiles.size()>=32){settings_error(L"Можно сохранить до 32 профилей.");return;}
        Settings current;if(!read_settings(current))return;draft=std::move(current);
        auto bounded=[](std::wstring base,const std::wstring&suffix){base.resize(std::min(base.size(),64-suffix.size()));if(!base.empty()&&base.back()>=0xd800&&base.back()<=0xdbff)base.pop_back();return base+suffix;};
        auto p=make_profile(categories[profile_category_index].category,draft.conversion,kind);if(copy){if(auto*old=editing_profile()){auto id=p.id;p=*old;p.id=id;p.name=bounded(p.name,L" копия");}}
        auto base=p.name;for(unsigned n=1;;++n){bool exists=std::any_of(draft.profiles.begin(),draft.profiles.end(),[&](auto&v){return v.category==p.category&&CompareStringOrdinal(v.name.c_str(),-1,p.name.c_str(),-1,TRUE)==CSTR_EQUAL;});if(!exists)break;p.name=bounded(base,L" "+std::to_wstring(n));}
        draft.profiles.push_back(std::move(p));profile_index=static_cast<int>(draft.profiles.size())-1;for(size_t i=0;i<categories.size();++i)if(categories[i].category==draft.profiles.back().category)profile_category_index=static_cast<int>(i);fill_profiles();settings_error(L"Профиль создан в черновике. Выберите его активным и сохраните настройки.");
    }
    void layout_profiles(int width){
        const int inside=width-56,half=(inside-20)/2,right=48+half,third=(inside-16)/3;
        auto place=[&](int id,int x,int y,int w,int h){ShowWindow(setting(id),SW_SHOW);move_setting(id,x,y,w,h);};
        auto row=[&](int l,int ctl,int x,int y,int w){place(l,x,y,w,22);place(ctl,x,y+26,w,ctl==ProfileName||ctl==ProfileSize?34:200);};
        row(ProfileCategoryLabel,ProfileCategory,28,138,half);row(ProfileActiveLabel,ProfileActive,right,138,half);row(ProfileChoiceLabel,ProfileChoice,28,212,inside);
        for(auto pair:{std::pair{ProfileNew,0},std::pair{ProfileCopy,1},std::pair{ProfileDelete,2}})place(pair.first,28+(third+8)*pair.second,286,third,36);
        for(auto pair:{std::pair{ProfilePhoto,0},std::pair{ProfileVideo,1},std::pair{ProfileAudio,2}})place(pair.first,28+(third+8)*pair.second,334,third,46);
        auto*p=editing_profile();if(!p){place(ProfileConsequences,28,400,inside,142);return;}
        row(ProfileNameLabel,ProfileName,28,394,inside);row(ProfileFormatLabel,ProfileFormat,28,468,inside);
        int y=542;auto c=p->category;const bool sound=c==Category::Audio||profile_extracts_audio(*p);
        if(c==Category::Image||c==Category::Video||c==Category::Audio){row(ProfileQualityLabel,ProfileQuality,28,y,half);row(ProfileResourcesLabel,ProfileResources,right,y,half);y+=74;}
        EnableWindow(setting(ProfileQuality),!(sound&&(p->format==Format::Wav||p->format==Format::Flac)));
        if(c==Category::Image||c==Category::Video||c==Category::Audio||c==Category::Pdf){
            set_text(setting(ProfileSizeLabel),c==Category::Image?L"Сторона, px (0 — исходная)":sound?L"Битрейт, кбит/с (0 — авто)":c==Category::Video?L"Высота, px (0 — исходная)":L"Растровый экспорт, DPI");
            row(ProfileSizeLabel,ProfileSize,28,y,half);EnableWindow(setting(ProfileSize),!(sound&&(p->format==Format::Wav||p->format==Format::Flac))&&!(c==Category::Pdf&&p->format==Format::Txt));
            if(c==Category::Video&&!sound)row(ProfileCodecLabel,ProfileCodec,right,y,half);y+=74;
        }
        if(c==Category::Image){row(ProfileMetadataLabel,ProfileMetadata,28,y,inside);y+=74;}
        if(c==Category::Video&&!sound){row(ProfileAccelerationLabel,ProfileAcceleration,28,y,inside);y+=74;}
        if(c==Category::Pdf){place(ProfileOcr,28,y,inside,30);place(ProfileEnglish,28,y+40,half,30);place(ProfileRussian,right,y+40,half,30);place(ProfileAzerbaijani,28,y+78,inside,30);for(auto id:{ProfileOcr,ProfileEnglish,ProfileRussian,ProfileAzerbaijani})EnableWindow(setting(id),p->format==Format::Txt);y+=148;}
        place(ProfileConsequences,28,y+8,inside,150);
    }
    int section_height() const {
        RECT rect{}; GetClientRect(page,&rect);
        const bool narrow=MulDiv(rect.right,96,static_cast<int>(dpi))<556;
        constexpr int heights[]{554,848,542,554,510};
        return heights[settings_section]+(settings_section==1?profile_offset()+(narrow?36:0):0);
    }
    void layout_page() {
        if (!page) return;
        const std::vector<std::vector<int>> groups{
            {AutoShow,Startup,NotificationsLabel,Notifications,OutputLabel,Output,Browse,OutputHint,ExportDiagnostics},
            {QualityLabel,Quality,ResourcesLabel,Resources,ImageLabel,ImageSize,VideoLabel,VideoSize,AudioLabel,AudioBitrate,PdfLabel,PdfDpi,AccelerationLabel,Acceleration,CodecLabel,Codec,ImageMetadataLabel,ImageMetadataChoice,PdfOcr,OcrEnglish,OcrRussian,OcrAzerbaijani},
            {WheelLabel,WheelHint,CategoryLabel,CategoryChoice,Formats,MoveUp,MoveDown},
            {AutoUpdate,UpdateLabel,UpdateChannel,UpdateInfo,CheckUpdate,InstallUpdate,LaterUpdate},
            {Modules,ModuleInfo,InstallModule,RemoveModule,CancelModule,RefreshModules}};
        for(size_t i=0;i<groups.size();++i) for(auto id:groups[i]) ShowWindow(setting(id),i==static_cast<size_t>(settings_section)?SW_SHOW:SW_HIDE);
        for(int id=400;id<=455;++id)if(setting(id))ShowWindow(setting(id),SW_HIDE);
        RECT rect{};GetClientRect(page,&rect);
        const int width=MulDiv(rect.right,96,static_cast<int>(dpi)), inside=width-56, half=(inside-20)/2, right=48+half;
        set_text(setting(SectionTitle),section_names[settings_section]);set_text(setting(SectionHint),section_hints[settings_section]);
        move_setting(SectionTitle,24,4,inside,34);move_setting(SectionHint,24,44,inside,40);
        cards.clear();
        if(settings_section==0) {
            cards={{92,218,L"Работа приложения"},{328,152,L"Результаты конвертации"}};
            move_setting(AutoShow,28,138,inside,30);move_setting(Startup,28,178,inside,30);
            move_setting(NotificationsLabel,28,220,inside,22);move_setting(Notifications,28,246,inside,180);
            move_setting(OutputLabel,28,372,inside,22);move_setting(Output,28,398,inside-110,34);move_setting(Browse,width-126,398,98,34);
            move_setting(OutputHint,28,440,inside,34);move_setting(ExportDiagnostics,28,498,242,36);
            EnableWindow(setting(ExportDiagnostics),static_cast<bool>(actions.export_diagnostics));
        } else if(settings_section==1) {
            const int offset=profile_offset();
            cards={{92,264,L"Качество и размеры"},{374,120,L"Кодирование видео"},{512,154,L"Метаданные изображений"},{684,width<556?180:144,L"Распознавание сканов PDF"}};
            for(auto&card:cards)card.top+=offset;
            cards.insert(cards.begin(),{92,offset-18,L"Профили: повторяемые параметры"});
            layout_profiles(width);
            const auto conv_move=[&](int id,int x,int y,int w,int h){move_setting(id,x,y+offset,w,h);};
            for(const auto& row : {std::array<int,5>{QualityLabel,Quality,ResourcesLabel,Resources,138},
                std::array<int,5>{ImageLabel,ImageSize,VideoLabel,VideoSize,212},std::array<int,5>{AudioLabel,AudioBitrate,PdfLabel,PdfDpi,286},
                std::array<int,5>{CodecLabel,Codec,AccelerationLabel,Acceleration,420}}) {
                conv_move(row[0],28,row[4],half,22);conv_move(row[1],28,row[4]+26,half,row[1]==Quality||row[1]==Codec?180:34);
                conv_move(row[2],right,row[4],half,22);conv_move(row[3],right,row[4]+26,half,row[3]==Resources||row[3]==Acceleration?180:34);
            }
            conv_move(ImageMetadataLabel,28,554,inside,40);conv_move(ImageMetadataChoice,28,604,inside,160);
            conv_move(PdfOcr,28,730,inside,30);conv_move(OcrEnglish,28,774,width<556?160:inside/3,30);
            conv_move(OcrRussian,width<556?202:28+inside/3,774,width<556?160:inside/3,30);
            conv_move(OcrAzerbaijani,width<556?28:28+2*(inside/3),width<556?810:774,width<556?inside:inside/3,30);
        } else if(settings_section==2) {
            cards={{92,430,L"Выбор форматов"}};
            move_setting(WheelLabel,28,136,inside,24);move_setting(WheelHint,28,170,inside,44);
            move_setting(CategoryLabel,28,218,inside,22);move_setting(CategoryChoice,28,244,inside,220);
            move_setting(Formats,28,294,inside-128,200);move_setting(MoveUp,width-144,294,116,36);move_setting(MoveDown,width-144,340,116,36);
            ListView_SetColumnWidth(setting(Formats),0,px(inside-148));
        } else if(settings_section==3) {
            cards={{92,300,L"Обновления приложения"},{410,120,L"Источник обновлений"}};
            move_setting(AutoUpdate,28,138,inside,34);move_setting(UpdateInfo,28,186,inside,80);
            move_setting(CheckUpdate,28,282,128,38);move_setting(InstallUpdate,168,282,180,38);move_setting(LaterUpdate,28,332,180,38);
            move_setting(UpdateLabel,28,454,inside,22);move_setting(UpdateChannel,28,480,inside,34);
        } else {
            cards={{92,396,L"Компоненты конвертации"}};
            move_setting(Modules,28,138,inside,220);move_setting(ModuleInfo,28,188,inside,156);
            move_setting(InstallModule,28,362,210,38);move_setting(RemoveModule,250,362,116,38);
            move_setting(CancelModule,28,412,210,38);move_setting(RefreshModules,250,412,190,38);
        }
        InvalidateRect(page,nullptr,TRUE);
    }
    void layout() {
        if (!window || !main_control(Tabs)) return;
        RECT rect{};GetClientRect(window,&rect);
        const int width=MulDiv(rect.right,96,static_cast<int>(dpi)), height=MulDiv(rect.bottom,96,static_cast<int>(dpi));
        move(main_control(Tabs),width-284,22,260,38);
        move(main_control(Headline),24,104,width-144,34);move(main_control(ProgressText),width-116,114,92,24);
        move(main_control(Summary),24,148,width-48,40);move(main_control(Progress),24,190,width-48,10);
        move(main_control(Cancel),24,216,160,36);move(main_control(Pause),196,216,218,36);move(main_control(Clear),426,216,180,36);
        move(main_control(HistoryLabel),24,274,106,24);move(main_control(HistorySearch),136,268,width-372,32);move(main_control(ClearHistory),width-214,268,190,34);
        const int list_height=std::max(54,height-490);
        move(main_control(Jobs),24,312,width-48,list_height);
        const int name_width=(width-70)*46/100;
        ListView_SetColumnWidth(main_control(Jobs),0,px(name_width));ListView_SetColumnWidth(main_control(Jobs),1,px(86));ListView_SetColumnWidth(main_control(Jobs),2,px(width-70-name_width-86));
        const int detail_height=std::clamp(height-68-(326+list_height),32,88);
        move(main_control(JobDetail),24,326+list_height,width-48,detail_height);
        move(main_control(Open),24,height-54,188,38);move(main_control(Retry),224,height-54,132,38);move(main_control(Remove),368,height-54,188,38);
        move(main_control(SettingsSection),20,96,184,252);
        move(page,216,96,width-240,height-216);
        // Measure without a native bar first; its width can wrap the last OCR row.
        ShowScrollBar(page,SB_VERT,FALSE);
        RECT viewport{};GetClientRect(page,&viewport);
        const bool native_bar=palette.high_contrast&&px(section_height())>viewport.bottom;
        ShowScrollBar(page,SB_VERT,native_bar);
        GetClientRect(page,&viewport);
        SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE|SIF_POS};info.nMin=0;info.nMax=px(section_height())-1;info.nPage=static_cast<UINT>(std::max(0L,viewport.bottom));
        scroll=std::clamp(scroll,0,std::max(0,px(section_height())-static_cast<int>(viewport.bottom)));info.nPos=scroll;SetScrollInfo(page,SB_VERT,&info,FALSE);
        ShowScrollBar(page,SB_VERT,native_bar);
        move(main_control(SettingsError),240,height-112,width-264,44);
        move(main_control(Defaults),240,height-54,170,38);move(main_control(Apply),width-246,height-54,222,38);
        layout_page();InvalidateRect(window,nullptr,TRUE);
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
        EnableWindow(setting(AutoUpdate),state.online_updates_allowed);
        EnableWindow(setting(UpdateChannel),state.online_updates_allowed);
        EnableWindow(setting(CheckUpdate),state.online_updates_allowed&&!state.update_checking&&static_cast<bool>(actions.check_update));
        EnableWindow(setting(InstallUpdate),state.online_updates_allowed&&state.update_ready&&!state.update_checking&&static_cast<bool>(actions.install_update));
        EnableWindow(setting(LaterUpdate),state.online_updates_allowed&&state.update_available&&static_cast<bool>(actions.later_update));
        if(state.modules!=previous.modules){
            auto module_selection=SendMessageW(setting(Modules),CB_GETCURSEL,0,0);SendMessageW(setting(Modules),CB_RESETCONTENT,0,0);
            for(auto& row:state.modules)SendMessageW(setting(Modules),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(row.c_str()));
            SendMessageW(setting(Modules),CB_SETCURSEL,module_selection<0?0:module_selection,0);
        }
        set_text(setting(ModuleInfo),state.module_detail);
        set_text(setting(InstallModule),state.module_downloads_allowed?(state.selected_module_installed?L"Обновить / восстановить":L"Установить"):(state.selected_module_installed?L"Доступен локально":L"Загрузка отключена"));
        EnableWindow(setting(InstallModule),state.module_downloads_allowed&&state.modules_allowed&&!state.modules_busy&&static_cast<bool>(actions.change_module));
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
        if(previous.installed_modules!=state.installed_modules&&editing_profile()&&save_profile_editor(false))profile_hint();
    }
    void settings_error(const wchar_t* message, bool success = false) { feedback_success=success;set_text(main_control(SettingsError),message);InvalidateRect(main_control(SettingsError),nullptr,TRUE); }
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
        profile_index=-1;fill_profiles();fill_formats(); settings_error(L"");
    }
    bool read_settings(Settings& result) {
        if(!save_profile_editor())return false;
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
        try { validate_options(result.conversion); validate_profiles(result); }
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
        try { actions.save(result); draft = std::move(result); settings_error(L"Настройки сохранены. Они применятся к новым задачам.",true); }
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
        if(id==ProfileFormat&&notification==CBN_SELCHANGE){
            auto*p=editing_profile();if(!p)return;auto chosen=SendMessageW(setting(ProfileFormat),CB_GETCURSEL,0,0);auto old=std::find(profile_format_order.begin(),profile_format_order.end(),p->format);
            SendMessageW(setting(ProfileFormat),CB_SETCURSEL,std::distance(profile_format_order.begin(),old),0);
            if(!save_profile_editor()){set_text(setting(ProfileConsequences),L"Исправьте параметры текущего формата перед переходом. Ошибочный черновик оставлен доступным для редактирования.");return;}
            if(chosen<0||static_cast<size_t>(chosen)>=profile_format_order.size())return;
            auto next=draft;next.profiles[profile_index].format=profile_format_order[chosen];
            try{validate_profiles(next);*p=std::move(next.profiles[profile_index]);fill_profiles();settings_error(L"");}
            catch(...){settings_error(L"Формат несовместим с текущим кодеком. Сначала измените кодек; прежний формат сохранён.");}return;
        }
        if((id==ProfileCategory||id==ProfileChoice||id==ProfileActive)&&notification==CBN_SELCHANGE){
            const int chosen=static_cast<int>(SendMessageW(setting(id),CB_GETCURSEL,0,0));
            if(!save_profile_editor()){
                int old=0;if(id==ProfileCategory)old=profile_category_index;
                else for(size_t i=0;i<profile_indices.size();++i){auto& p=draft.profiles[profile_indices[i]];if(id==ProfileChoice&&profile_indices[i]==profile_index)old=static_cast<int>(i)+1;if(id==ProfileActive){auto a=draft.active_profiles.find(categories[profile_category_index].category);if(a!=draft.active_profiles.end()&&a->second==p.id)old=static_cast<int>(i)+1;}}
                SendMessageW(setting(id),CB_SETCURSEL,old,0);return;
            }
            if(id==ProfileCategory){profile_category_index=std::clamp(chosen,0,6);profile_index=-1;}
            else if(id==ProfileChoice)profile_index=chosen>0&&static_cast<size_t>(chosen)<=profile_indices.size()?profile_indices[chosen-1]:-1;
            else{auto c=categories[profile_category_index].category;if(chosen<=0)draft.active_profiles.erase(c);else if(static_cast<size_t>(chosen)<=profile_indices.size())draft.active_profiles[c]=draft.profiles[profile_indices[chosen-1]].id;}
            fill_profiles();return;
        }
        if(notification==BN_CLICKED&&(id==ProfileNew||id==ProfileCopy||id==ProfilePhoto||id==ProfileVideo||id==ProfileAudio)){new_profile(id==ProfilePhoto?0:id==ProfileVideo?1:id==ProfileAudio?2:-1,id==ProfileCopy);return;}
        if(notification==BN_CLICKED&&id==ProfileDelete){if(auto*p=editing_profile()){const auto c=p->category;auto a=draft.active_profiles.find(c);if(a!=draft.active_profiles.end()&&a->second==p->id)draft.active_profiles.erase(a);draft.profiles.erase(draft.profiles.begin()+profile_index);profile_index=-1;fill_profiles();settings_error(L"Профиль удалён из черновика. Примените сохранение.");}return;}
        if(((id==ProfileName||id==ProfileSize)&&notification==EN_CHANGE)||((id>=ProfileQuality&&id<=ProfileMetadata)&&notification==CBN_SELCHANGE)||((id>=ProfileOcr&&id<=ProfileAzerbaijani)&&notification==BN_CLICKED)){
            if(save_profile_editor(false)){profile_hint();if(id==ProfileFormat)layout();}else set_text(setting(ProfileConsequences),L"Проверьте параметры профиля: есть недопустимое значение. Черновик сохранён в редакторе, применение заблокировано.");return;
        }
        if (id == IDCANCEL) { ShowWindow(window, SW_HIDE); return; }
        if (id == IDOK) { if (settings_visible) apply(); return; }
        if (id == CategoryChoice && notification == CBN_SELCHANGE) {
            save_visible_formats(); category_index = static_cast<int>(SendMessageW(setting(CategoryChoice), CB_GETCURSEL, 0, 0));
            category_index = std::clamp(category_index, 0, static_cast<int>(categories.size()) - 1); fill_formats(); return;
        }
        if(id == SettingsSection && notification == LBN_SELCHANGE) {
            settings_section=static_cast<int>(SendMessageW(main_control(SettingsSection),LB_GETCURSEL,0,0));
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
        if (id == Defaults) { if(!save_profile_editor())return;Settings defaults;defaults.profiles=draft.profiles;load_draft(defaults); settings_error(L"Общие настройки восстановлены, активные профили отключены. Созданные профили сохранены. Нажмите «Сохранить настройки»."); return; }
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
        wchar_t type[32]{};GetClassNameW(header->hwndFrom,type,32);
        if(header->code==NM_CUSTOMDRAW && (header->idFrom==Formats||header->idFrom==Jobs)) {
            const auto& draw=*reinterpret_cast<NMLVCUSTOMDRAW*>(header);
            if(draw.nmcd.dwDrawStage==CDDS_PREPAINT)return CDRF_NOTIFYITEMDRAW;
            if(draw.nmcd.dwDrawStage==CDDS_ITEMPREPAINT) {
                paint_list_row(draw);return CDRF_SKIPDEFAULT;
            }
        }
        if(header->code==NM_CUSTOMDRAW && lstrcmpiW(type,L"Button")==0) {
            const auto& draw=*reinterpret_cast<NMCUSTOMDRAW*>(header);
            if(draw.dwDrawStage==CDDS_PREPAINT) {
                panel_theme::button(draw,palette,font,dpi,header->idFrom==Apply);
                return CDRF_SKIPDEFAULT;
            }
        }
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
        if (!settings_visible || !page || GetParent(control)!=page) return;
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
        if(message==WM_LBUTTONDOWN) SendMessageW(self->window,WM_CHANGEUISTATE,MAKEWPARAM(UIS_SET,UISF_HIDEFOCUS),0);
        if(message==WM_KEYDOWN && (wparam==VK_TAB||wparam==VK_LEFT||wparam==VK_RIGHT||wparam==VK_UP||wparam==VK_DOWN||wparam==VK_HOME||wparam==VK_END||wparam==VK_PRIOR||wparam==VK_NEXT||wparam==VK_SPACE))
            SendMessageW(self->window,WM_CHANGEUISTATE,MAKEWPARAM(UIS_CLEAR,UISF_HIDEFOCUS),0);
        if (message == WM_SETFOCUS) self->ensure_visible(hwnd);
        if (message == WM_NCDESTROY) RemoveWindowSubclass(hwnd, control_proc, subclass_id);
        wchar_t type[32]{};GetClassNameW(hwnd,type,32);
        if((GetDlgCtrlID(hwnd)==Tabs || lstrcmpiW(type,WC_HEADERW)==0) && (message==WM_PAINT||message==WM_PRINTCLIENT)) {
            PAINTSTRUCT paint{};const HDC dc=message==WM_PAINT?BeginPaint(hwnd,&paint):reinterpret_cast<HDC>(wparam);
            if(GetDlgCtrlID(hwnd)==Tabs)self->paint_tabs(hwnd,dc);else self->paint_header(hwnd,dc);
            if(message==WM_PAINT)EndPaint(hwnd,&paint);
            return 0;
        }
        const auto result=DefSubclassProc(hwnd,message,wparam,lparam);
        if(lstrcmpiW(type,L"ComboBox")==0 && (message==WM_PAINT||message==WM_PRINTCLIENT)) {
            const HDC dc=message==WM_PAINT?GetDC(hwnd):reinterpret_cast<HDC>(wparam);
            panel_theme::combo_border(hwnd,dc,self->palette,self->dpi);
            if(message==WM_PAINT)ReleaseDC(hwnd,dc);
        }
        if((lstrcmpiW(type,L"Edit")==0||lstrcmpiW(type,WC_LISTVIEWW)==0) && message==WM_NCPAINT) {
            const auto dc=GetWindowDC(hwnd);RECT r{};GetWindowRect(hwnd,&r);OffsetRect(&r,-r.left,-r.top);
            const auto brush=CreateSolidBrush(GetFocus()==hwnd?self->palette.accent:self->palette.border);
            FrameRect(dc,&r,brush);DeleteObject(brush);ReleaseDC(hwnd,dc);
        }
        return result;
    }
    LRESULT colors(HWND control, HDC dc) const {
        const int id=GetDlgCtrlID(control);
        wchar_t type[32]{};GetClassNameW(control,type,32);
        const bool field=lstrcmpiW(type,L"Edit")==0 || lstrcmpiW(type,L"ComboLBox")==0 || lstrcmpiW(type,L"ComboBox")==0;
        const bool in_card=GetParent(control)==page && id!=SectionTitle && id!=SectionHint;
        const bool secondary=id==SectionHint||id==OutputHint||id==WheelHint||id==ImageMetadataLabel;
        SetBkColor(dc,field?palette.input:in_card?palette.surface:palette.canvas);
        SetTextColor(dc,id==SettingsError&&!palette.high_contrast?(feedback_success?RGB(119,220,172):RGB(255,164,150)):secondary?palette.muted:palette.text);
        return reinterpret_cast<LRESULT>(field?input:in_card?surface:background);
    }
    void paint_page(HDC dc) const {
        RECT r{};GetClientRect(page,&r);panel_theme::fill(dc,r,palette.canvas);
        for(const auto& card:cards) {
            RECT bounds{px(8),px(card.top)-scroll,r.right-px(8),px(card.top+card.height)-scroll};
            panel_theme::box(dc,bounds,palette.surface,palette.border,px(12));
            RECT label{px(28),bounds.top+px(14),bounds.right-px(20),bounds.top+px(38)};
            panel_theme::text(dc,card.title,label,section_font,palette.text);
        }
        const auto thumb=scroll_thumb();
        if(!palette.high_contrast&&thumb.bottom>thumb.top)panel_theme::box(dc,thumb,scroll_drag?palette.accent:palette.border,scroll_drag?palette.accent:palette.border,px(5));
    }
    RECT scroll_thumb() const {
        RECT r{};GetClientRect(page,&r);SCROLLINFO info{sizeof(info),SIF_ALL};GetScrollInfo(page,SB_VERT,&info);
        if(info.nMax+1<=static_cast<int>(info.nPage)||r.bottom<=0)return {};
        const int thumb=std::min(static_cast<int>(r.bottom),std::max(px(36),MulDiv(static_cast<int>(info.nPage),r.bottom,info.nMax+1)));
        const int top=MulDiv(scroll,r.bottom-thumb,info.nMax+1-static_cast<int>(info.nPage));
        return {r.right-px(5),top,r.right-px(1),top+thumb};
    }
    void drag_scroll(int y) {
        RECT r{};GetClientRect(page,&r);const auto thumb=scroll_thumb();
        SCROLLINFO info{sizeof(info),SIF_ALL};GetScrollInfo(page,SB_VERT,&info);
        const int travel=r.bottom-(thumb.bottom-thumb.top);
        if(travel>0) {const int target=MulDiv(y-scroll_grab,info.nMax+1-static_cast<int>(info.nPage),travel);scroll_page(-1,scroll-target);}
    }
    void paint_window(HDC dc) const {
        RECT r{};GetClientRect(window,&r);panel_theme::fill(dc,r,palette.canvas);
        auto icon=static_cast<HICON>(LoadImageW(instance,MAKEINTRESOURCEW(IDI_SHIFTWHEEL),IMAGE_ICON,px(36),px(36),LR_SHARED));
        if(icon)DrawIconEx(dc,px(24),px(22),icon,px(36),px(36),0,nullptr,DI_NORMAL);
        panel_theme::text(dc,L"ShiftWheel",RECT{px(72),px(14),px(330),px(46)},heading_font,palette.text);
        panel_theme::text(dc,L"Конвертация файлов",RECT{px(73),px(46),px(330),px(66)},font,palette.muted);
        panel_theme::fill(dc,RECT{px(24),px(80),r.right-px(24),px(81)},palette.border);
        if(settings_visible) {
            panel_theme::fill(dc,RECT{px(215),px(96),px(216),r.bottom-px(24)},palette.border);
            panel_theme::fill(dc,RECT{px(240),r.bottom-px(124),r.right-px(24),r.bottom-px(123)},palette.border);
            panel_theme::text(dc,L"Зажми Shift",RECT{px(24),r.bottom-px(80),px(204),r.bottom-px(56)},section_font,palette.muted);
            panel_theme::text(dc,L"при перетаскивании",RECT{px(24),r.bottom-px(54),px(204),r.bottom-px(32)},font,palette.muted);
        }
    }
    void paint_tabs(HWND control,HDC dc) const {
        RECT r{};GetClientRect(control,&r);panel_theme::fill(dc,r,palette.canvas);
        for(int i=0;i<2;++i) {
            RECT item{};TabCtrl_GetItemRect(control,i,&item);InflateRect(&item,-px(2),-px(1));
            const bool active=TabCtrl_GetCurSel(control)==i;
            panel_theme::box(dc,item,active?palette.selection:palette.surface,active?palette.accent:palette.border,px(8));
            panel_theme::text(dc,i?L"Настройки":L"Задачи",item,section_font,active?palette.selected_text:palette.muted,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
            if(GetFocus()==control&&active&&panel_theme::focus_visible(control)) {InflateRect(&item,-px(3),-px(3));DrawFocusRect(dc,&item);}
        }
    }
    void paint_header(HWND control,HDC dc) const {
        RECT r{};GetClientRect(control,&r);panel_theme::fill(dc,r,palette.surface);
        for(int i=0;i<Header_GetItemCount(control);++i) {
            RECT item{};Header_GetItemRect(control,i,&item);item.left+=px(10);
            wchar_t title[160]{};HDITEMW h{};h.mask=HDI_TEXT;h.pszText=title;h.cchTextMax=160;Header_GetItem(control,i,&h);
            panel_theme::text(dc,title,item,section_font,palette.muted);
        }
    }
    void paint_list_row(const NMLVCUSTOMDRAW& draw) const {
        const auto list=draw.nmcd.hdr.hwndFrom;const int index=static_cast<int>(draw.nmcd.dwItemSpec);
        RECT row{};ListView_GetItemRect(list,index,&row,LVIR_BOUNDS);
        const bool selected=(ListView_GetItemState(list,index,LVIS_SELECTED)&LVIS_SELECTED)!=0;
        const auto saved=SaveDC(draw.nmcd.hdc);IntersectClipRect(draw.nmcd.hdc,row.left,row.top,row.right,row.bottom);
        panel_theme::fill(draw.nmcd.hdc,row,selected?palette.selection:palette.input);
        const int columns=draw.nmcd.hdr.idFrom==Formats?1:3;
        int left=0;
        for(int column=0;column<columns;++column) {
            wchar_t value[512]{};ListView_GetItemText(list,index,column,value,512);
            const int width=ListView_GetColumnWidth(list,column);
            RECT cell{left+px(10),row.top,left+width-px(8),row.bottom};
            if(draw.nmcd.hdr.idFrom==Formats) {
                const bool checked=ListView_GetCheckState(list,index)!=FALSE;
                const int side=px(15);RECT mark{px(3),row.top+(row.bottom-row.top-side)/2,px(3)+side,0};mark.bottom=mark.top+side;
                panel_theme::box(draw.nmcd.hdc,mark,checked?palette.accent:palette.input,palette.border,px(4));
                if(checked) {
                    const auto pen=CreatePen(PS_SOLID,std::max(1,px(2)),palette.high_contrast?palette.selected_text:palette.canvas);
                    SelectObject(draw.nmcd.hdc,pen);POINT points[]{{mark.left+px(3),mark.top+px(7)},{mark.left+px(6),mark.top+px(11)},{mark.left+px(12),mark.top+px(3)}};
                    Polyline(draw.nmcd.hdc,points,3);SelectObject(draw.nmcd.hdc,GetStockObject(BLACK_PEN));DeleteObject(pen);
                }
                cell.left=px(26);
            }
            panel_theme::text(draw.nmcd.hdc,value,cell,font,selected?palette.selected_text:palette.text);left+=width;
        }
        if(selected&&GetFocus()==list&&panel_theme::focus_visible(list)) {InflateRect(&row,-1,-1);DrawFocusRect(draw.nmcd.hdc,&row);}
        RestoreDC(draw.nmcd.hdc,saved);
    }
    LRESULT draw_item(const DRAWITEMSTRUCT& item) const {
        if(item.CtlType==ODT_TAB) { paint_tabs(item.hwndItem,item.hDC);return TRUE; }
        if(item.CtlType!=ODT_LISTBOX && item.CtlType!=ODT_COMBOBOX)return FALSE;
        RECT rect=item.rcItem;
        const bool selected=(item.itemState&ODS_SELECTED)!=0;
        const bool navigation=item.CtlType==ODT_LISTBOX;
        panel_theme::fill(item.hDC,rect,navigation?palette.canvas:palette.input);
        if(item.itemID==static_cast<UINT>(-1))return TRUE;
        std::wstring value;
        if(navigation&&item.itemID<section_names.size())value=section_names[item.itemID];
        else if(!navigation) {
            const auto count=SendMessageW(item.hwndItem,CB_GETLBTEXTLEN,item.itemID,0);
            if(count>=0&&count<4096) {value.resize(static_cast<size_t>(count)+1);SendMessageW(item.hwndItem,CB_GETLBTEXT,item.itemID,reinterpret_cast<LPARAM>(value.data()));}
        }
        if(navigation) {
            InflateRect(&rect,-px(3),-px(3));
            if(selected) {
                panel_theme::box(item.hDC,rect,palette.selection,palette.selection,px(8));
                panel_theme::fill(item.hDC,RECT{rect.left,rect.top+px(11),rect.left+px(3),rect.bottom-px(11)},palette.accent);
            }
            rect.left+=px(16);rect.right-=px(8);
        } else {
            if(selected&&!(item.itemState&ODS_COMBOBOXEDIT))panel_theme::fill(item.hDC,rect,palette.selection);
            rect.left+=px(10);rect.right-=px((item.itemState&ODS_COMBOBOXEDIT)?30:8);
        }
        panel_theme::text(item.hDC,value.c_str(),rect,navigation?section_font:font,
            (item.itemState&ODS_DISABLED)?palette.muted:selected?palette.selected_text:palette.text);
        if((item.itemState&ODS_FOCUS)&&!(item.itemState&ODS_NOFOCUSRECT)&&panel_theme::focus_visible(item.hwndItem)) {InflateRect(&rect,-px(2),-px(2));DrawFocusRect(item.hDC,&rect);}
        return TRUE;
    }
    void refresh_theme() {
        palette=panel_theme::colors();
        for(auto brush:{background,surface,input})if(brush)DeleteObject(brush);
        background=CreateSolidBrush(palette.canvas);surface=CreateSolidBrush(palette.surface);input=CreateSolidBrush(palette.input);
        const BOOL dark=!palette.high_contrast;DwmSetWindowAttribute(window,DWMWA_USE_IMMERSIVE_DARK_MODE,&dark,sizeof(dark));
        const COLORREF caption=palette.high_contrast?DWMWA_COLOR_DEFAULT:palette.canvas,caption_text=palette.high_contrast?DWMWA_COLOR_DEFAULT:palette.text;
        DwmSetWindowAttribute(window,DWMWA_CAPTION_COLOR,&caption,sizeof(caption));
        DwmSetWindowAttribute(window,DWMWA_TEXT_COLOR,&caption_text,sizeof(caption_text));
        for(const auto id:{Jobs,Formats}) {
            const auto list=id==Jobs?main_control(id):setting(id);
            ListView_SetBkColor(list,palette.input);ListView_SetTextBkColor(list,palette.input);ListView_SetTextColor(list,palette.text);
        }
        layout();RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN);
    }
    static LRESULT CALLBACK page_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
        auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (self) {
            switch (message) {
            case WM_DRAWITEM: return self->draw_item(*reinterpret_cast<DRAWITEMSTRUCT*>(lparam));
            case WM_MEASUREITEM: reinterpret_cast<MEASUREITEMSTRUCT*>(lparam)->itemHeight=self->px(30);return TRUE;
            case WM_COMMAND: self->command(LOWORD(wparam), HIWORD(wparam)); return 0;
            case WM_NOTIFY: return self->notify(reinterpret_cast<NMHDR*>(lparam));
            case WM_VSCROLL: self->scroll_page(LOWORD(wparam)); return 0;
            case WM_LBUTTONDOWN: {
                RECT r{};GetClientRect(hwnd,&r);const auto thumb=self->scroll_thumb();
                if(!self->palette.high_contrast&&GET_X_LPARAM(lparam)>=r.right-self->px(14)&&thumb.bottom>thumb.top) {
                    const int y=GET_Y_LPARAM(lparam);self->scroll_grab=y>=thumb.top&&y<=thumb.bottom?y-thumb.top:(thumb.bottom-thumb.top)/2;
                    self->scroll_drag=true;SetCapture(hwnd);self->drag_scroll(y);return 0;
                }break;
            }
            case WM_MOUSEMOVE: if(self->scroll_drag){self->drag_scroll(GET_Y_LPARAM(lparam));return 0;}break;
            case WM_LBUTTONUP: if(self->scroll_drag){self->scroll_drag=false;ReleaseCapture();InvalidateRect(hwnd,nullptr,FALSE);return 0;}break;
            case WM_CAPTURECHANGED: self->scroll_drag=false;InvalidateRect(hwnd,nullptr,FALSE);return 0;
            case WM_MOUSEWHEEL: self->scroll_page(-1, MulDiv(GET_WHEEL_DELTA_WPARAM(wparam), self->px(96), WHEEL_DELTA)); return 0;
            case WM_CTLCOLORSTATIC: return self->colors(reinterpret_cast<HWND>(lparam), reinterpret_cast<HDC>(wparam));
            case WM_CTLCOLOREDIT: case WM_CTLCOLORLISTBOX: case WM_CTLCOLORBTN: return self->colors(reinterpret_cast<HWND>(lparam),reinterpret_cast<HDC>(wparam));
            case WM_PAINT: { PAINTSTRUCT paint{};const auto dc=BeginPaint(hwnd,&paint);self->paint_page(dc);EndPaint(hwnd,&paint);return 0; }
            case WM_PRINTCLIENT: self->paint_page(reinterpret_cast<HDC>(wparam));return 0;
            case WM_ERASEBKGND: self->paint_page(reinterpret_cast<HDC>(wparam));return 1;
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
            case WM_DRAWITEM: return self->draw_item(*reinterpret_cast<DRAWITEMSTRUCT*>(lparam));
            case WM_MEASUREITEM: reinterpret_cast<MEASUREITEMSTRUCT*>(lparam)->itemHeight=self->px(48);return TRUE;
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
            case WM_CTLCOLOREDIT: case WM_CTLCOLORLISTBOX: case WM_CTLCOLORBTN: return self->colors(reinterpret_cast<HWND>(lparam),reinterpret_cast<HDC>(wparam));
            case WM_SYSCOLORCHANGE: case WM_SETTINGCHANGE: self->refresh_theme();return 0;
            case WM_PAINT: { PAINTSTRUCT paint{};const auto dc=BeginPaint(hwnd,&paint);self->paint_window(dc);EndPaint(hwnd,&paint);return 0; }
            case WM_PRINTCLIENT: self->paint_window(reinterpret_cast<HDC>(wparam));return 0;
            case WM_ERASEBKGND: self->paint_window(reinterpret_cast<HDC>(wparam));return 1;
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
    type.hIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_SHIFTWHEEL), IMAGE_ICON,
        GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_SHARED));
    type.hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_SHIFTWHEEL), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED));
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
    const int width = std::min(impl_->px(1040), static_cast<int>(work.right - work.left));
    const int height = std::min(impl_->px(790), static_cast<int>(work.bottom - work.top));
    SetWindowPos(impl_->window, nullptr, work.left + std::max(0L, (work.right - work.left - width) / 2),
        work.top + std::max(0L, (work.bottom - work.top - height) / 2), width, height, SWP_NOACTIVATE | SWP_NOZORDER);
    impl_->refresh_theme();impl_->layout();
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
