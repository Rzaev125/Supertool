#include "app.h"
#include "resource.h"
#include "modules.h"
#include "updates.h"
#include "update_state.h"
#include "update_signature.h"
#include <mutex>
#include <map>
#include "diagnostics.h"
#include "tray_notifications.h"
#include <shobjidl.h>
#include <thread>
#include <atomic>
#include "wheel.h"
#include "worker.h"
#include "control_panel.h"
#include "job_queue.h"
#include "job_store.h"
#include "file_policy.h"
#include <shellapi.h>
#include <shlobj.h>
#include <windowsx.h>
#include <stdexcept>
namespace shiftwheel {
using Microsoft::WRL::ComPtr;
namespace {
constexpr UINT wm_tray = WM_APP + 1, wm_trigger = WM_APP + 2, wm_hide = WM_APP + 3, wm_mouse_up = WM_APP + 4;
constexpr UINT wm_start_next = WM_APP + 5, wm_show_panel = WM_APP + 7, wm_update = WM_APP + 10;
constexpr UINT wm_next_notification = WM_APP + 11, wm_update_progress = WM_APP + 12, wm_modules = WM_APP + 13;
constexpr UINT menu_preview = 1, menu_cancel = 2, menu_open = 3, menu_exit = 4, menu_tasks = 5, menu_settings = 6, menu_update = 7;
class App {
public:
    ~App() {
        module_thread_.request_stop();if(module_thread_.joinable())module_thread_.join();
        update_thread_.request_stop();
        if (update_thread_.joinable()) update_thread_.join();
        if (mouse_) UnhookWindowsHookEx(mouse_);
        if (keyboard_) UnhookWindowsHookEx(keyboard_);
        worker_.shutdown();
        panel_.shutdown();
        if (wheel_) wheel_->shutdown();
        if (tray_) { NOTIFYICONDATAW icon{sizeof(icon)}; icon.hWnd = window_; icon.uID = 1; Shell_NotifyIconW(NIM_DELETE, &icon); }
        if (window_) DestroyWindow(window_);
        current_ = nullptr;
    }
    void initialize(HINSTANCE instance, bool preview, bool show_panel, bool show_settings, bool update_failed) {
        update_failed_ = update_failed;
        current_ = this;
        std::wstring executable(32768, L'\0');
        const auto length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
        if (!length || length >= executable.size()) throw std::runtime_error("Не удалось найти папку настроек.");
        executable.resize(length); settings_path_ = std::filesystem::path(executable).parent_path() / L"ShiftWheel.ini";
        try { settings_ = load_settings(settings_path_); }
        catch (const std::exception& error) { startup_notice_ = L"Настройки не загружены: " + from_utf8(error.what()); }
        if(update_signature_configured()&&settings_.update_channel==L"https://raw.githubusercontent.com/Rzaev125/Supertool/main/updates/stable.txt")settings_.update_channel=L"https://raw.githubusercontent.com/Rzaev125/Supertool/main/updates/stable-signed.txt";
        diagnostic_ = std::make_unique<DiagnosticLog>(settings_path_.parent_path()/L"ShiftWheel.diagnostics.log");
        diagnostic_->append("session-start");
        queue_path_=settings_path_.parent_path()/L"ShiftWheel.queue";
        try {
            if(auto saved=load_queue(queue_path_)) {
                queue_.restore(*saved);queue_.take_batch_outcomes();
                recovery_pending_=queue_.pending()!=0;
                for(auto it=queue_.items().rbegin();it!=queue_.items().rend();++it)if(it->state==JobState::Succeeded&&!it->output.empty()){last_output_=it->output;break;}
            }
        }catch(const std::exception& e){startup_notice_=from_utf8(e.what());}
        executable_ = executable; settings_.start_with_windows = startup_enabled(executable_);
        if (update_failed_) startup_notice_ = L"Обновление не установлено. Повторите проверку через меню трея. Автоматическая повторная попытка отложена; ручная проверка доступна.";
        WNDCLASSW cls{}; cls.lpfnWndProc = window_proc; cls.hInstance = instance; cls.lpszClassName = L"ShiftWheel.Controller";
        cls.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_SHIFTWHEEL));
        if (!RegisterClassW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) throw std::runtime_error("Не удалось создать окно управления.");
        window_ = CreateWindowExW(WS_EX_TOOLWINDOW, cls.lpszClassName, L"ShiftWheel", WS_OVERLAPPED, 0, 0, 0, 0, nullptr, nullptr, instance, this);
        if (!window_) throw std::runtime_error("Не удалось создать окно управления.");
        wheel_.Attach(new Wheel([this] { return queue_.pending() >= 256; }, [this](std::filesystem::path input, Format format) { enqueue({input}, format); }));
        wheel_->set_batch_handler([this](std::vector<std::filesystem::path> files, Format format) { enqueue(files, format); });
        wheel_->set_format_provider([this](const std::filesystem::path& input) {
            return available_formats(input, configured_formats(input, settings_), [this](std::string_view id) {
                const auto found = module_status_cache_.find(std::string(id));
                return !modules_busy_ && found != module_status_cache_.end() && found->second.installed;
            });
        });
        if (FAILED(wheel_->initialize(instance))) throw std::runtime_error("Не удалось создать колесо или зарегистрировать приём файлов.");
        update_policy_path_=settings_path_.parent_path()/L"ShiftWheel.update-state";
        try{update_policy_=load_update_policy(update_policy_path_);}catch(const std::exception& e){startup_notice_=from_utf8(e.what());}
        if(update_failed_){update_policy_.failure(update_now());save_update_policy(update_policy_path_,update_policy_);}
        if(!update_policy_.check_allowed(update_now(),false))update_status_=L"Автоматическая проверка отложена. Ручная проверка доступна.";
        scan_modules();
        PanelActions actions;
        actions.change_module=[this](size_t index,bool remove){change_module(index,remove);};
        actions.select_module=[this](size_t index){selected_module_=index;refresh();};
        actions.refresh_modules=[this]{scan_modules();refresh();};
        actions.cancel_module=[this]{module_thread_.request_stop();};
        actions.check_update=[this]{check_updates(true);};
        actions.install_update=[this]{
            if(pending_setup_.empty())throw std::runtime_error("Проверенный установщик ещё не загружен.");
            commit_install_request(update_policy_path_,update_policy_,install_requested_);
            update_status_=L"Установка запрошена. Дождёмся завершения всей очереди.";refresh();PostMessageW(window_,wm_start_next,0,0);
        };
        actions.later_update=[this]{
            install_requested_=false;update_policy_.later(update_now());save_update_policy(update_policy_path_,update_policy_);
            ++update_generation_;update_thread_.request_stop();discard_update_download(pending_setup_);pending_setup_.clear();
            update_status_=L"Обновление отложено на сутки. Очередь продолжит работу.";refresh();
        };
        actions.export_diagnostics = [this] { export_diagnostics(); };
        actions.cancel = [this] { cancel(); };
        actions.pause_queue = [this] { recovery_pending_=false;queue_.set_paused(!queue_.paused()); refresh(); PostMessageW(window_, wm_start_next, 0, 0); };
        actions.clear_history = [this] {queue_.clear_history();last_output_.clear();refresh();};
        actions.clear_queue = [this] { queue_.clear_pending(); notify_finished_batches(); refresh(); PostMessageW(window_, wm_start_next, 0, 0); };
        actions.remove = [this](uint64_t id) { queue_.remove(id); notify_finished_batches(); refresh(); PostMessageW(window_, wm_start_next, 0, 0); };
        actions.open_result = [this](uint64_t id) { if (auto job = queue_.find(id); job && !job->output.empty()) reveal(job->output); };
        actions.retry = [this](uint64_t id) {
            if (auto job = queue_.find(id); job && job->state != JobState::Queued && job->state != JobState::Running) {
                if (!supported_local_file(job->input)) throw std::runtime_error("Исходный файл больше недоступен.");
                const auto snapshot = *job;
                queue_.enqueue({snapshot.input}, snapshot.format, snapshot.options, snapshot.output_directory);
                queue_.capture_inputs();
                refresh(); PostMessageW(window_, wm_start_next, 1, 0);
            }
        };
        actions.save = [this](const Settings& next) {
            if (!next.output_directory.empty()) {
                validate_local_path(next.output_directory, false);
                if (!std::filesystem::is_directory(next.output_directory)) throw std::runtime_error("Выберите существующую папку результата.");
            }
            if (!next.update_channel.empty() && !https_url(next.update_channel)) throw std::runtime_error("Укажите корректный HTTPS-адрес канала обновлений.");
            const auto previous = settings_;
            save_settings(settings_path_, next);
            try { if (next.start_with_windows != settings_.start_with_windows) set_startup(executable_, next.start_with_windows); }
            catch (...) { save_settings(settings_path_, previous); throw; }
            settings_ = next; startup_notice_.clear();
            ++update_generation_; install_requested_=false;discard_update_download(pending_setup_);pending_setup_.clear();
            SetTimer(window_, 2, 30000, nullptr);
            wheel_->hide(); panel_.set_settings(settings_); refresh();
        };
        if (FAILED(panel_.initialize(instance, settings_, std::move(actions)))) throw std::runtime_error("Не удалось создать окно задач.");
        add_tray();
        taskbar_created_ = RegisterWindowMessageW(L"TaskbarCreated");
        keyboard_ = SetWindowsHookExW(WH_KEYBOARD_LL, keyboard_proc, instance, 0);
        mouse_ = SetWindowsHookExW(WH_MOUSE_LL, mouse_proc, instance, 0);
        if (!keyboard_ || !mouse_) throw std::runtime_error("Windows не разрешила наблюдение за жестом перетаскивания.");
        left_shift_ = (GetAsyncKeyState(VK_LSHIFT) & 0x8000) != 0;
        right_shift_ = (GetAsyncKeyState(VK_RSHIFT) & 0x8000) != 0;
        if (preview) { POINT point{}; GetCursorPos(&point); wheel_->show(point, true); }
        refresh();
        if (show_panel || show_settings) panel_.show(show_settings);
        else if(recovery_pending_)panel_.show(false,false);
        SetTimer(window_, 2, 30000, nullptr);
    }
    bool pretranslate(MSG& message) { return panel_.handle() && IsWindowVisible(panel_.handle()) && IsDialogMessageW(panel_.handle(), &message); }
private:
    static const wchar_t* status(JobState state) {
        switch (state) {
        case JobState::Queued: return L"Ожидает";
        case JobState::Running: return L"Конвертация";
        case JobState::Succeeded: return L"Готово";
        case JobState::Failed: return L"Ошибка";
        case JobState::Cancelled: return L"Отменено";
        case JobState::Interrupted: return L"Прервано — повторите вручную";
        }
        return L"";
    }
    void export_diagnostics() {
        ComPtr<IFileSaveDialog> dialog;
        if(FAILED(CoCreateInstance(CLSID_FileSaveDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog)))) throw std::runtime_error("Не удалось открыть выбор файла отчёта.");
        dialog->SetTitle(L"Сохранить диагностику ShiftWheel");
        dialog->SetOptions(FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST|FOS_DONTADDTORECENT);
        dialog->SetDefaultExtension(L"txt");
        const auto name=L"ShiftWheel-diagnostics-"+std::to_wstring(GetTickCount64())+L".txt";
        dialog->SetFileName(name.c_str());
        const auto shown=dialog->Show(panel_.handle());
        if(shown == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return;
        if(FAILED(shown)) throw std::runtime_error("Не удалось выбрать файл отчёта.");
        ComPtr<IShellItem> item;PWSTR path=nullptr;
        if(FAILED(dialog->GetResult(&item))||FAILED(item->GetDisplayName(SIGDN_FILESYSPATH,&path))) throw std::runtime_error("Не удалось прочитать путь отчёта.");
        const std::filesystem::path destination(path);CoTaskMemFree(path);
        validate_local_path(destination,false);diagnostic_->export_to(destination);
    }
    void enqueue(const std::vector<std::filesystem::path>& files, Format format) {
        try {
            for (const auto& input : files) if (!supported_local_file(input)) throw std::runtime_error("Один из исходных файлов недоступен.");
            queue_.enqueue_grouped(files, format, settings_.conversion, settings_.output_directory);
            queue_.capture_inputs();
            diagnostic_->append("conversion-queued",queue_.items().back().id);
            PostMessageW(window_, wm_start_next, 1, 0);
        } catch (const std::exception& error) { diagnostic_->append("admission-rejected"); if(settings_.notifications != NotificationMode::None) balloon(L"Не удалось добавить файлы", from_utf8(error.what()), NIIF_ERROR); throw; }
    }
    void cancel() { if (worker_.busy()) { cancelling_ = true; worker_.cancel(); refresh(); } }
    void start_next(bool new_admission) {
        if(modules_busy_){refresh();return;}
        if (may_install_update(install_requested_,worker_.busy(),queue_.pending()) && !pending_setup_.empty()) {
            try { launch_update_helper(pending_setup_, executable_, pending_hash_); PostQuitMessage(0); }
            catch (const std::exception& e) { balloon(L"Обновление отложено", from_utf8(e.what()), NIIF_ERROR); pending_setup_.clear();install_requested_=false;update_policy_.failure(update_now());save_update_policy(update_policy_path_,update_policy_);update_status_=L"Установка не началась. Повторная попытка отложена.";refresh(); }
            return;
        }
        const bool show = new_admission && settings_.auto_show_progress && !IsWindowVisible(panel_.handle());
        if (worker_.busy()) { refresh(); if (show) panel_.show(false, false); return; }
        while (auto job = queue_.next()) {
            const auto snapshot = *job;
            queue_.start(snapshot.id); cancelling_ = false;
            try {
                if(!persist_queue())throw std::runtime_error("Не удалось сохранить состояние. Очередь приостановлена.");
                if(!source_unchanged(snapshot))throw std::runtime_error("Исходный файл исчез или изменился после добавления. Проверьте его и нажмите «Повторить».");
                worker_.start(snapshot.input, snapshot.format, window_, snapshot.options, snapshot.output_directory);
                started_ = GetTickCount64(); tooltip(L"ShiftWheel — конвертация…"); break;
            } catch (const std::exception& error) {
                diagnostic_->append("conversion-start-failed",snapshot.id);
                queue_.complete(snapshot.id, JobState::Failed, {}, from_utf8(error.what()));
                notify_finished_batches();
                if(queue_.paused())break;
            }
        }
        refresh();
        if (show) panel_.show(false, false);
    }
    void notify_finished_batches() {
        for(const auto& group : queue_.take_batch_outcomes()) {
            if(!notify_batch(settings_.notifications, group)) continue;
            auto message=group.total == 1 ? group.last_message : std::to_wstring(group.succeeded)+L" готово, "+std::to_wstring(group.failed)+L" ошибок, "+std::to_wstring(group.cancelled)+L" отменено";
            balloon(group.failed ? L"Конвертация: есть ошибки" : L"Конвертация завершена", message, group.failed ? NIIF_ERROR : NIIF_INFO, group.last_output);
        }
    }
    bool persist_queue() {
        if(saved_revision_==queue_.revision())return true;
        saved_revision_=queue_.revision();
        try{save_queue(queue_path_,queue_.snapshot());return true;}
        catch(const std::exception& e){startup_notice_=from_utf8(e.what());queue_.set_paused(true);saved_revision_=queue_.revision();return false;}
    }
    void refresh() {
        persist_queue();
        PanelState state; state.queue_paused = queue_.paused(); state.running = worker_.busy();
        state.recovery_pending=recovery_pending_;
        state.modules=module_rows_;state.modules_busy=modules_busy_;state.modules_allowed=!worker_.busy()&&queue_.pending()==0;
        if(selected_module_<module_catalog().size()){
            auto& m=module_catalog()[selected_module_];auto total=m.bytes;
            std::wstring dep=L"нет";
            if(!m.dependency.empty())for(auto& d:module_catalog())if(d.id==m.dependency){dep=d.name;if(!module_status_cache_[m.dependency].installed)total+=d.bytes;}
            auto mib=[](auto n){return std::to_wstring((n+1024*1024-1)/(1024*1024))+L" МБ";};
            state.module_detail=m.name+L"\r\nДоступная версия: "+m.version+L"; загрузка: "+mib(m.bytes)+L"; на диске: "+mib(m.unpacked)+L"\r\nЗависимости: "+dep+L". Общая загрузка: "+mib(total)+L".\r\n";
            {std::lock_guard lock(module_mutex_);state.module_detail+=module_notice_;}
            if(!state.modules_allowed)state.module_detail+=L"\r\nИзменение модулей доступно после завершения/очистки очереди.";
        }
        state.update_detail=update_status_;state.update_checking=checking_;state.update_ready=!pending_setup_.empty()&&!install_requested_;
        {std::lock_guard lock(update_mutex_);state.update_available=!available_release_.version.empty()&&newer_version(available_release_.version,app_version);}
        state.headline = queue_.paused() ? L"Очередь приостановлена" : L"Готов к конвертации";
        state.detail = startup_notice_.empty() ? L"Перетащите файлы и удерживайте Shift. В очереди: " + std::to_wstring(queue_.pending()) : startup_notice_;
        if(recovery_pending_&&startup_notice_.empty())state.detail=L"Найдена сохранённая очередь. Нажмите «Восстановить очередь» или очистите её. Конвертация ещё не запущена.";
        if (auto active = queue_.active()) {
            const auto progress = worker_.progress();
            state.headline = cancelling_ ? L"Отменяем конвертацию…" : active->input.filename().wstring();
            state.percent = cancelling_ ? -1 : progress.percent;
            state.detail = (progress.stage.empty() ? L"Подготовка" : progress.stage) + L" · " + std::to_wstring((GetTickCount64() - started_) / 1000) + L" с · В очереди: " + std::to_wstring(queue_.pending());
        }
        auto add = [&](const ConversionJob& job) {
            const bool pending = job.state == JobState::Queued, running = job.state == JobState::Running;
            auto detail = job.input.wstring();
            if (!job.message.empty()) detail += L"\r\n" + job.message;
            if (!job.output.empty()) detail += L"\r\nРезультат: " + job.output.wstring();
            if(auto batch=queue_.batch(job.batch_id))detail+=L"\r\nГруппа "+std::to_wstring(batch->id)+L": всего "+std::to_wstring(batch->total)+L", готово "+std::to_wstring(batch->succeeded)+L", ошибок "+std::to_wstring(batch->failed)+L", отменено "+std::to_wstring(batch->cancelled)+L", осталось "+std::to_wstring(batch->remaining);
            state.jobs.push_back({job.id, job.input.filename().wstring(), label(job.format), status(job.state), std::move(detail), !pending && !running, pending, job.state == JobState::Succeeded});
        };
        for (const auto& job : queue_.items()) if (job.state == JobState::Running || job.state == JobState::Queued) add(job);
        for (auto it = queue_.items().rbegin(); it != queue_.items().rend(); ++it) if (it->state != JobState::Running && it->state != JobState::Queued) add(*it);
        panel_.update(state);
    }
    void scan_modules(){
        module_rows_.clear();
        for(auto& spec:module_catalog()){
            try{auto status=module_status(executable_.parent_path(),spec);module_status_cache_[spec.id]=status;module_rows_.push_back(spec.name+L" — "+(status.installed?status.version:L"не установлен"));}
            catch(...){module_status_cache_[spec.id]={};module_rows_.push_back(spec.name+L" — ошибка; повторите установку");}
        }
    }
    void change_module(size_t index,bool remove){
        if(modules_busy_||worker_.busy()||queue_.pending()||install_requested_)throw std::runtime_error("Сначала завершите или очистите очередь.");
        if(index>=module_catalog().size())return;
        if(module_thread_.joinable())module_thread_.join();
        auto spec=module_catalog()[index];auto root=executable_.parent_path();
        // A held lease in a separate CLI must also prevent expensive downloads.
        {ModuleLease available(root,true);}
        modules_busy_=true;
        {std::lock_guard lock(module_mutex_);module_notice_=remove?L"Удаление…":L"Подготовка загрузки…";}
        module_thread_=std::jthread([this,spec,root,remove](std::stop_token stop){
            auto notice=[this](std::wstring text){std::lock_guard lock(module_mutex_);module_notice_=std::move(text);PostMessageW(window_,wm_modules,1,0);};
            try{
                if(remove)remove_module(root,spec);
                else {
                    bool retained_download=false;
                    auto install=[&](const ModuleSpec& item){
                        ULONGLONG last=0;
                        auto result=install_module(root,item,stop,[&](auto got,auto total){auto now=GetTickCount64();if(now-last<200&&got!=total)return;last=now;notice(item.name+L": загрузка "+std::to_wstring(total?got*100/total:0)+L"%. Затем проверка и распаковка…");});
                        retained_download=retained_download||result.cache_retained;
                    };
                    if(!spec.dependency.empty())for(auto& d:module_catalog())if(d.id==spec.dependency&&!module_status(root,d).installed)install(d);
                    install(spec);
                    if(retained_download){notice(L"Модуль установлен и проверен. Временный CAB занят другой программой; повторим очистку при следующей установке модуля.");PostMessageW(window_,wm_modules,0,0);return;}
                }
                notice(remove?L"Модуль отключён. Неизвестные и старые офлайн-файлы сохранены в modules.":L"Модуль установлен и проверен. Можно конвертировать файлы.");
            }catch(const std::exception& e){notice(from_utf8(e.what()));}
            PostMessageW(window_,wm_modules,0,0);
        });refresh();
    }
    static void reveal(const std::filesystem::path& path) {
        validate_local_path(path,false);
        if (!std::filesystem::exists(path)) throw std::runtime_error("Файл результата больше не существует.");
        PIDLIST_ABSOLUTE item = ILCreateFromPathW(path.c_str());
        if (!item) throw std::runtime_error("Не удалось найти результат в Проводнике.");
        const HRESULT result = SHOpenFolderAndSelectItems(item, 0, nullptr, 0); ILFree(item);
        if (FAILED(result)) throw std::runtime_error("Не удалось открыть Проводник.");
    }
    void add_tray() {
        NOTIFYICONDATAW icon{sizeof(icon)}; icon.hWnd = window_; icon.uID = 1;
        icon.uFlags = NIF_MESSAGE | NIF_ICON | tray_tooltip_flags; icon.uCallbackMessage = wm_tray;
        icon.hIcon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_SHIFTWHEEL),
            IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED));
        wcscpy_s(icon.szTip, L"ShiftWheel · перетащи файл и зажми Shift");
        tray_ = Shell_NotifyIconW(NIM_ADD, &icon) != FALSE;
        if (tray_) { icon.uVersion = NOTIFYICON_VERSION_4; Shell_NotifyIconW(NIM_SETVERSION, &icon); }
    }
    void tooltip(const wchar_t* text) {
        NOTIFYICONDATAW icon{sizeof(icon)}; icon.hWnd = window_; icon.uID = 1; icon.uFlags = tray_tooltip_flags;
        wcsncpy_s(icon.szTip, text, _TRUNCATE); Shell_NotifyIconW(NIM_MODIFY, &icon);
    }
    void balloon(const std::wstring& title, const std::wstring& message, DWORD type, const std::filesystem::path& target = {}) {
        notifications_.push({title,message,type,target});
        PostMessageW(window_,wm_next_notification,0,0);
    }
    void next_notification() {
        if(!tray_) return;
        while(notifications_.begin()) {
            const auto& next=*notifications_.current();
            NOTIFYICONDATAW icon{sizeof(icon)}; icon.hWnd=window_; icon.uID=1; icon.uFlags=NIF_INFO; icon.dwInfoFlags=next.type;
            wcsncpy_s(icon.szInfoTitle,next.title.c_str(),_TRUNCATE); wcsncpy_s(icon.szInfo,next.message.c_str(),_TRUNCATE);
            if(Shell_NotifyIconW(NIM_MODIFY,&icon)) return;
            notifications_.finish();
        }
    }
    void check_updates(bool manual) {
        if(checking_)return;
        if(!pending_setup_.empty()){update_status_=L"Обновление загружено. Выберите «Установить сейчас» или «Позже».";refresh();return;}
        if(settings_.update_channel.empty()){if(manual){update_status_=L"Задайте HTTPS-канал и сохраните настройки.";refresh();}return;}
        if(!manual&&(!settings_.automatic_updates||!update_policy_.check_allowed(update_now(),false)))return;
        if(update_thread_.joinable())update_thread_.join();
        checking_=true;update_status_=L"Проверяем обновления…";download_got_=0;download_total_=0;
        {std::lock_guard lock(update_mutex_);available_release_={};}
        refresh();
        const auto channel=settings_.update_channel;const auto installed=installed_copy(executable_);const auto generation=update_generation_;const auto highest=update_policy_.highest_version;
        update_thread_=std::jthread([this,channel,installed,manual,generation,highest](std::stop_token stop){
            std::wstring notice;std::filesystem::path setup;std::wstring hash;bool success=false;
            try{
                auto release=fetch_update(channel,stop);
                if(!highest.empty()&&newer_version(highest,release.version))throw std::runtime_error("Канал предлагает устаревшую версию. Обновление отклонено.");
                {std::lock_guard lock(update_mutex_);available_release_=release;}
                if(!newer_version(release.version,app_version))notice=L"Установлена актуальная версия.";
                else if(!installed)notice=L"Доступна версия "+release.version+L". Переносимую копию обновите вручную через страницу выпусков GitHub.";
                else{
                    download_total_=release.size;PostMessageW(window_,wm_update_progress,generation,0);ULONGLONG tick=0;
                    setup=download_update(release,stop,{},[this,&tick,generation](auto got,auto total){download_got_=got;download_total_=total;auto now=GetTickCount64();if(now-tick>=250||got==total){tick=now;PostMessageW(window_,wm_update_progress,generation,0);}});
                    hash=release.sha256;notice=L"Версия "+release.version+L" загружена и проверена. Выберите «Установить сейчас» или «Позже».";
                }
                success=true;
            }catch(const std::exception& e){notice=from_utf8(e.what());}
            update_result_generation_=generation;update_notice_=std::move(notice);update_setup_=std::move(setup);update_hash_=std::move(hash);update_manual_=manual;update_success_=success;
            PostMessageW(window_,wm_update,0,0);
        });
    }
    void menu() {
        POINT point{}; GetCursorPos(&point);
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, menu_tasks, L"Задачи и история");
        AppendMenuW(menu, MF_STRING, menu_settings, L"Настройки");
        AppendMenuW(menu, MF_STRING, menu_preview, L"Посмотреть колесо");
        AppendMenuW(menu, MF_STRING | (worker_.busy() ? 0 : MF_GRAYED), menu_cancel, L"Отменить конвертацию");
        AppendMenuW(menu, MF_STRING | (last_output_.empty() ? MF_GRAYED : 0), menu_open, L"Открыть папку результата");
        AppendMenuW(menu, MF_STRING, menu_update, L"Проверить обновления");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, menu_exit, L"Выход");
        SetForegroundWindow(window_);
        UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, window_, nullptr);
        DestroyMenu(menu); PostMessageW(window_, WM_NULL, 0, 0);
        switch (command) {
        case menu_preview: wheel_->show(point, true); break;
        case menu_cancel: cancel(); break;
        case menu_tasks: refresh(); panel_.show(); break;
        case menu_settings: panel_.show(true); break;
        case menu_open: open_output(); break;
        case menu_update: check_updates(true); break;
        case menu_exit: PostMessageW(window_, WM_CLOSE, 0, 0); break;
        }
    }
    void open_output() {
        if (!last_output_.empty()) reveal(last_output_);
    }
    static bool explorer_at(POINT point) {
        HWND source = GetAncestor(WindowFromPoint(point), GA_ROOT);
        wchar_t name[128]{}; GetClassNameW(source, name, 128);
        return wcscmp(name, L"CabinetWClass") == 0 || wcscmp(name, L"ExploreWClass") == 0 || wcscmp(name, L"Progman") == 0 || wcscmp(name, L"WorkerW") == 0;
    }
    void maybe_trigger() {
        if (gesture_.move(pointer_.x, pointer_.y, left_shift_ || right_shift_, GetSystemMetrics(SM_CXDRAG), GetSystemMetrics(SM_CYDRAG)))
            PostMessageW(window_, wm_trigger, 0, 0);
    }
    static LRESULT CALLBACK mouse_proc(int code, WPARAM message, LPARAM data) {
        auto self = current_;
        if (code == HC_ACTION && self) {
            const auto event = reinterpret_cast<MSLLHOOKSTRUCT*>(data);
            self->pointer_ = event->pt;
            if (message == WM_LBUTTONDOWN) self->gesture_.begin(event->pt.x, event->pt.y, explorer_at(event->pt));
            else if (message == WM_MOUSEMOVE && self->gesture_.active()) self->maybe_trigger();
            else if (message == WM_LBUTTONUP) {
                self->gesture_.end();
                // OLE in the source process must receive mouse-up before we hide.
                PostMessageW(self->window_, wm_mouse_up, 0, 0);
            }
        }
        return CallNextHookEx(nullptr, code, message, data);
    }
    static LRESULT CALLBACK keyboard_proc(int code, WPARAM message, LPARAM data) {
        auto self = current_;
        if (code == HC_ACTION && self) {
            const auto key = reinterpret_cast<KBDLLHOOKSTRUCT*>(data)->vkCode;
            const bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
            const bool up = message == WM_KEYUP || message == WM_SYSKEYUP;
            if ((down || up) && (key == VK_LSHIFT || key == VK_RSHIFT || key == VK_SHIFT)) {
                if (key == VK_RSHIFT) self->right_shift_ = down; else self->left_shift_ = down;
                if (self->left_shift_ || self->right_shift_) self->maybe_trigger();
                else { self->gesture_.shift_released(); PostMessageW(self->window_, wm_hide, 0, 0); }
            }
            if (down && key == VK_ESCAPE) PostMessageW(self->window_, wm_hide, 0, 0);
        }
        return CallNextHookEx(nullptr, code, message, data);
    }
    static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
        auto self = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) { self = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams); SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)); }
        if (!self) return DefWindowProcW(hwnd, message, w, l);
        if (self->taskbar_created_ && message == self->taskbar_created_) {
            self->notifications_.restart(); self->add_tray(); PostMessageW(hwnd,wm_next_notification,0,0); return 0;
        }
        try { switch (message) {
        case wm_tray:
            if (LOWORD(l) == WM_CONTEXTMENU || LOWORD(l) == WM_RBUTTONUP) self->menu();
            if (LOWORD(l) == WM_LBUTTONDBLCLK) { self->refresh(); self->panel_.show(); }
            if(LOWORD(l)==NIN_BALLOONUSERCLICK || LOWORD(l)==NIN_BALLOONHIDE || LOWORD(l)==NIN_BALLOONTIMEOUT) {
                const auto target=self->notifications_.finish();
                PostMessageW(hwnd,wm_next_notification,0,0);
                if(LOWORD(l)==NIN_BALLOONUSERCLICK && !target.empty()) self->reveal(target);
            }
            return 0;
        case wm_next_notification: self->next_notification(); return 0;
        case wm_trigger:
            if (self->queue_.pending() < 256 && (GetAsyncKeyState(VK_LBUTTON) & 0x8000) && (GetAsyncKeyState(VK_SHIFT) & 0x8000)) {
                POINT point{}; GetCursorPos(&point); self->wheel_->show(point);
            }
            return 0;
        case wm_hide: if (self->wheel_) self->wheel_->hide(); return 0;
        case wm_mouse_up:
            if (self->wheel_ && self->wheel_->visible()) SetTimer(hwnd, 1, 250, nullptr);
            return 0;
        case wm_modules:
            if(!w){if(self->module_thread_.joinable())self->module_thread_.join();self->modules_busy_=false;self->scan_modules();PostMessageW(hwnd,wm_start_next,0,0);}
            self->refresh();return 0;
        case wm_update_progress:
            if(static_cast<unsigned>(w)!=self->update_generation_)return 0;
            if(self->checking_&&self->download_total_){
                std::lock_guard lock(self->update_mutex_);
                self->update_status_=L"Версия "+self->available_release_.version+L": загрузка "+std::to_wstring(self->download_got_*100/self->download_total_)+L"%\r\n"+self->available_release_.notes;
            }
            self->refresh();return 0;
        case wm_update:
            if(self->update_thread_.joinable())self->update_thread_.join();self->checking_=false;
            if(self->update_result_generation_!=self->update_generation_){discard_update_download(self->update_setup_);self->update_setup_.clear();self->refresh();return 0;}
            if(!self->update_setup_.empty()){self->pending_setup_=std::move(self->update_setup_);self->pending_hash_=self->update_hash_;}
            self->update_status_=self->update_notice_;
            if(self->update_success_){
                self->update_policy_.success();
                std::lock_guard lock(self->update_mutex_);
                self->update_policy_.highest_version=self->available_release_.version;
                if(!self->available_release_.notes.empty())self->update_status_+=L"\r\n"+self->available_release_.notes;
            }else self->update_policy_.failure(update_now());
            save_update_policy(self->update_policy_path_,self->update_policy_);
            if(self->update_manual_||!self->pending_setup_.empty())self->balloon(L"Обновления ShiftWheel",self->update_notice_,self->update_success_?NIIF_INFO:NIIF_ERROR);
            self->refresh();return 0;
        case WM_TIMER:
            if (w == 2) { SetTimer(hwnd, 2, 24 * 60 * 60 * 1000, nullptr); self->check_updates(false); return 0; }
            KillTimer(hwnd, w);
            if (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000) && self->wheel_ && !self->wheel_->has_ole_drag()) self->wheel_->hide();
            return 0;
        case wm_worker_done:
            if (self->worker_.busy()) {
                auto result = self->worker_.finish();
                self->diagnostic_->append(result.success ? "conversion-succeeded" : result.cancelled ? "conversion-cancelled" : "conversion-failed",self->queue_.active() ? self->queue_.active()->id : 0);
                if (auto job = self->queue_.active()) self->queue_.complete(job->id, result.success ? JobState::Succeeded : result.cancelled ? JobState::Cancelled : JobState::Failed, result.success ? result.output : std::filesystem::path{}, result.message);
                if (result.success) self->last_output_ = result.output;
                self->notify_finished_batches();
                self->tooltip(L"ShiftWheel · перетащи файл и зажми Shift");
                self->refresh(); PostMessageW(hwnd, wm_start_next, 0, 0);
            }
            return 0;
        case wm_worker_progress:
            self->refresh();
            if(GetTickCount64()-self->tray_progress_tick_ >= 1000) {
                const auto progress=self->worker_.progress();
                auto text=L"ShiftWheel · "+(progress.percent >= 0 ? std::to_wstring(progress.percent)+L"% · " : std::wstring{})+progress.stage;
                self->tooltip(text.c_str());self->tray_progress_tick_=GetTickCount64();
            }
            return 0;
        case wm_start_next: self->start_next(w != 0); return 0;
        case wm_show_panel: self->refresh(); self->panel_.show(w != 0); return 0;
        case WM_CLOSE: PostQuitMessage(0); return 0;
        } } catch (const std::exception& error) { self->balloon(L"Ошибка", from_utf8(error.what()), NIIF_ERROR); return 0; }
        return DefWindowProcW(hwnd, message, w, l);
    }
    static App* current_;
    std::filesystem::path executable_, pending_setup_, update_setup_,update_policy_path_;
    UpdatePolicy update_policy_;
    std::mutex update_mutex_;
    UpdateRelease available_release_;
    std::wstring update_status_;
    std::atomic<unsigned long long> download_got_{0},download_total_{0};
    bool install_requested_=false,update_success_=false;
    std::wstring pending_hash_, update_hash_, update_notice_;
    std::jthread update_thread_,module_thread_;
    std::mutex module_mutex_;
    std::wstring module_notice_=L"Установка — по выбранному компоненту. JPEG, PNG, BMP и TIFF доступны без загрузки. Проверка SHA-256 встроена; подпись пакетов пока не подключена.";
    std::vector<std::wstring> module_rows_;
    std::map<std::string,ModuleStatus> module_status_cache_;
    size_t selected_module_=0;
    bool modules_busy_=false;
    std::atomic<bool> checking_{false};
    bool update_manual_ = false, update_failed_ = false;
    unsigned update_generation_ = 0, update_result_generation_ = 0;
    HWND window_ = nullptr;
    HHOOK mouse_ = nullptr, keyboard_ = nullptr;
    bool tray_ = false, left_shift_ = false, right_shift_ = false;
    UINT taskbar_created_ = 0;
    POINT pointer_{};
    Gesture gesture_;
    Worker worker_;
    ControlPanel panel_;
    Settings settings_;
    std::unique_ptr<DiagnosticLog> diagnostic_;
    JobQueue queue_;
    std::filesystem::path queue_path_;
    uint64_t saved_revision_=0;
    bool recovery_pending_=false;
    std::filesystem::path settings_path_;
    std::wstring startup_notice_;
    bool cancelling_ = false;
    ULONGLONG started_ = 0;
    ComPtr<Wheel> wheel_;
    std::filesystem::path last_output_;
    TrayNotifications notifications_;
    ULONGLONG tray_progress_tick_ = 0;
};
App* App::current_ = nullptr;
}
int run_app(HINSTANCE instance, bool preview, bool panel, bool settings, bool startup, bool update_failed) {
    HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\ShiftWheel.Tray");
    if (!mutex) return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (!startup) if (auto window = FindWindowW(L"ShiftWheel.Controller", L"ShiftWheel")) PostMessageW(window, wm_show_panel, settings ? 1 : 0, 0);
        CloseHandle(mutex); return 0;
    }
    int result = 0;
    try {
        App app; app.initialize(instance, preview, panel, settings, update_failed);
        MSG message{}; BOOL status = 0;
        while ((status = GetMessageW(&message, nullptr, 0, 0)) > 0) { if (!app.pretranslate(message)) { TranslateMessage(&message); DispatchMessageW(&message); } }
        if (status == -1) result = 1;
    } catch (const std::exception& error) {
        MessageBoxW(nullptr, from_utf8(error.what()).c_str(), L"ShiftWheel", MB_OK | MB_ICONERROR);
        result = 1;
    }
    CloseHandle(mutex); return result;
}
}
