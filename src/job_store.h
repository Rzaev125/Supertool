#pragma once
#include "job_queue.h"
#include <windows.h>
#include <optional>
namespace shiftwheel {
void save_queue(const std::filesystem::path& path,const QueueSnapshot& snapshot);
std::optional<QueueSnapshot> load_queue(const std::filesystem::path& path);
// Shared bounded local state I/O: atomic replacement and no reparse traversal.
void save_local_state(const std::filesystem::path& path,const std::string& text);
std::optional<std::string> load_local_state(const std::filesystem::path& path,size_t limit=4*1024*1024);
}
