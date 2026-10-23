#include "job_queue.h"
#include "file_policy.h"
#include "diagnostics.h"
#include <windows.h>

namespace shiftwheel {
namespace fs = std::filesystem;
namespace {
fs::path create_batch_folder(const fs::path& parent, Format format) {
    if (!parent.is_absolute()) throw std::runtime_error("Папка результата должна иметь полный локальный путь.");
    validate_local_path(parent, false);
    const auto attributes = GetFileAttributesW(parent.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY))
        throw std::runtime_error("Выбранная папка результата недоступна.");
    const auto name = std::wstring(L"Конвертация ") + label(format);
    for (unsigned suffix = 0; suffix < 10000; ++suffix) {
        auto folder = parent / (name + (suffix ? L" (" + std::to_wstring(suffix) + L")" : L""));
        // Reserve atomically. Never reuse a previous batch's folder or follow an existing link.
        if (CreateDirectoryW(folder.c_str(), nullptr)) return folder;
        const auto error = GetLastError();
        if (error != ERROR_ALREADY_EXISTS && error != ERROR_FILE_EXISTS)
            throw std::runtime_error(file_error(error, "Не удалось создать папку результатов группы."));
    }
    throw std::runtime_error("Не удалось подобрать свободное имя папки результатов группы.");
}
}
void JobQueue::enqueue_grouped(const std::vector<std::filesystem::path>& files, Format format,
                               const ConversionOptions& options, const std::filesystem::path& output_directory) {
    if (files.size() <= 1) { enqueue(files, format, options, output_directory); return; }
    // The queue is bounded. Prepare admission before touching the filesystem so a
    // rejected drop cannot leave an empty folder or a partially changed queue.
    auto prepared = *this;
    prepared.enqueue(files, format, options, output_directory);
    for (const auto& input : files) if (!supported_local_file(input))
        throw std::runtime_error("Один из исходных файлов недоступен.");
    const auto parent = (output_directory.empty() ? files.front().parent_path() : output_directory).lexically_normal();
    const auto folder = create_batch_folder(parent, format);
    try {
        for (size_t i = prepared.jobs_.size() - files.size(); i < prepared.jobs_.size(); ++i)
            prepared.jobs_[i].output_directory = folder;
        *this = std::move(prepared);
    } catch (...) {
        // Only our newly created, still empty directory can be removed here.
        RemoveDirectoryW(folder.c_str()); throw;
    }
}
}
