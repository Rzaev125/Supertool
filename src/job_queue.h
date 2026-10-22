#pragma once
#include "options.h"
#include <algorithm>
#include <cstdint>
#include <stdexcept>

namespace shiftwheel {
enum class JobState { Queued, Running, Succeeded, Failed, Cancelled };
struct ConversionJob {
    uint64_t id = 0;
    std::filesystem::path input, output_directory, output;
    Format format = Format::Png;
    ConversionOptions options;
    JobState state = JobState::Queued;
    std::wstring message;
    uint64_t batch_id = 0;
};
struct BatchOutcome {
    uint64_t id = 0;
    size_t total = 0, remaining = 0, succeeded = 0, failed = 0, cancelled = 0;
    std::filesystem::path last_output;
    std::wstring last_message;
};
inline bool notify_batch(NotificationMode mode, const BatchOutcome& batch) {
    return mode == NotificationMode::All || (mode == NotificationMode::Errors && batch.failed != 0);
}
class JobQueue {
public:
    void enqueue(const std::vector<std::filesystem::path>& files, Format format,
                 const ConversionOptions& options, const std::filesystem::path& output_directory) {
        validate_options(options);
        if (files.empty() || files.size() > 256 || pending() + files.size() > 256)
            throw std::runtime_error("В очереди может ожидать до 256 файлов.");
        for (const auto& file : files) if (!supports_conversion(file, format))
            throw std::runtime_error("Выбранный формат подходит не для всех файлов.");
        // Prepare first, then reserve/append: admission must be all-or-nothing.
        std::vector<ConversionJob> additions; additions.reserve(files.size());
        auto id = next_id_;
        for (const auto& file : files) additions.push_back({id++, file, output_directory, {}, format, options, JobState::Queued, {}, next_batch_});
        jobs_.reserve(jobs_.size() + additions.size());
        batches_.emplace(next_batch_, BatchOutcome{next_batch_, files.size(), files.size()});
        ++next_batch_;
        for (auto& job : additions) jobs_.push_back(std::move(job));
        next_id_ = id;
    }
    ConversionJob* find(uint64_t id) {
        auto it = std::find_if(jobs_.begin(), jobs_.end(), [id](const auto& j) { return j.id == id; });
        return it == jobs_.end() ? nullptr : &*it;
    }
    ConversionJob* active() {
        auto it = std::find_if(jobs_.begin(), jobs_.end(), [](const auto& j) { return j.state == JobState::Running; });
        return it == jobs_.end() ? nullptr : &*it;
    }
    ConversionJob* next() {
        if (paused_ || active()) return nullptr;
        auto it = std::find_if(jobs_.begin(), jobs_.end(), [](const auto& j) { return j.state == JobState::Queued; });
        return it == jobs_.end() ? nullptr : &*it;
    }
    void start(uint64_t id) {
        auto job = find(id);
        if (!job || job->state != JobState::Queued || active()) throw std::runtime_error("Некорректное состояние очереди.");
        job->state = JobState::Running;
    }
    void complete(uint64_t id, JobState state, const std::filesystem::path& output, std::wstring message) {
        auto job = find(id);
        if (!job || job->state != JobState::Running || state == JobState::Queued || state == JobState::Running)
            throw std::runtime_error("Некорректное завершение задачи.");
        record(*job, state, output, message);
        job->state = state; job->output = output; job->message = std::move(message);
        auto completed = jobs_.size() - pending() - (active() ? 1 : 0);
        while (completed > 100) {
            auto it = std::find_if(jobs_.begin(), jobs_.end(), [](const auto& j) { return j.state != JobState::Queued && j.state != JobState::Running; });
            jobs_.erase(it); --completed;
        }
    }
    size_t pending() const { return static_cast<size_t>(std::count_if(jobs_.begin(), jobs_.end(), [](const auto& j) { return j.state == JobState::Queued; })); }
    bool paused() const { return paused_; }
    void set_paused(bool value) { paused_ = value; }
    bool remove(uint64_t id) {
        auto it = std::find_if(jobs_.begin(), jobs_.end(), [id](const auto& j) { return j.id == id && j.state == JobState::Queued; });
        if (it == jobs_.end()) return false;
        record(*it, JobState::Cancelled, {}, L"Задание удалено из очереди.");
        jobs_.erase(it); return true;
    }
    void clear_pending() {
        for(const auto& job : jobs_) if(job.state == JobState::Queued) record(job, JobState::Cancelled, {}, L"Задание удалено из очереди.");
        std::erase_if(jobs_, [](const auto& j) { return j.state == JobState::Queued; });
    }
    std::vector<BatchOutcome> take_batch_outcomes() {
        std::vector<BatchOutcome> ready;
        for(auto it=batches_.begin();it!=batches_.end();) {
            if(!it->second.remaining) {ready.push_back(std::move(it->second));it=batches_.erase(it);} else ++it;
        }
        return ready;
    }
    const std::vector<ConversionJob>& items() const { return jobs_; }
private:
    void record(const ConversionJob& job, JobState state, const std::filesystem::path& output, const std::wstring& message) {
        auto& group=batches_.at(job.batch_id); --group.remaining;
        if(state == JobState::Succeeded) {++group.succeeded;group.last_output=output;}
        else if(state == JobState::Failed) ++group.failed; else ++group.cancelled;
        group.last_message=message;
    }
    std::map<uint64_t,BatchOutcome> batches_;
    uint64_t next_batch_=1;
    std::vector<ConversionJob> jobs_;
    uint64_t next_id_ = 1;
    bool paused_ = false;
};
}
