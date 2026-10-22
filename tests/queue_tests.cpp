#include "job_queue.h"
#include <iostream>
#include <stdexcept>
using namespace shiftwheel;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main() {
    try {
        JobQueue queue;
        ConversionOptions compact; compact.quality = QualityPreset::Compact;
        queue.enqueue({L"first.png", L"second.webp"}, Format::Jpeg, compact, L"C:\\results");
        compact.quality = QualityPreset::High;
        auto first = queue.next(); check(first && first->input == L"first.png", "FIFO first");
        check(first->options.quality == QualityPreset::Compact && first->output_directory == L"C:\\results", "immutable settings snapshot");
        const auto first_id = first->id; queue.start(first_id);
        check(!queue.next(), "one active task");
        queue.complete(first_id, JobState::Failed, {}, L"broken input");
        auto second = queue.next(); check(second && second->input == L"second.webp", "failure allows next task");
        const auto second_id = second->id;
        queue.set_paused(true); check(!queue.next(), "pause affects pending jobs");
        queue.set_paused(false); queue.start(second_id);
        check(!queue.remove(second_id), "cannot remove active job");
        queue.enqueue({L"third.png"}, Format::Png, {}, {});
        queue.clear_pending(); check(queue.pending() == 0 && queue.active(), "clear does not cancel active");
        queue.complete(second_id, JobState::Cancelled, {}, L"cancelled");
        check(!queue.active(), "cancel finished");
        const auto count = queue.items().size();
        try { queue.enqueue({L"okay.png", L"bad.docx"}, Format::Png, {}, {}); check(false, "mixed incompatible batch accepted"); }
        catch (const std::runtime_error&) {}
        check(queue.items().size() == count, "batch admission atomic");
        std::vector<std::filesystem::path> too_many(257, L"photo.png");
        bool rejected = false; try { queue.enqueue(too_many, Format::Png, {}, {}); } catch (...) { rejected = true; }
        check(rejected && queue.pending() == 0, "queue capacity enforced");
        for (int i = 0; i < 120; ++i) {
            queue.enqueue({L"photo.png"}, Format::Png, {}, {});
            const auto id = queue.next()->id; queue.start(id); queue.complete(id, JobState::Succeeded, L"result.png", L"ready");
        }
        check(queue.items().size() == 100, "history is bounded");
        std::cout << "PASS FIFO, snapshots, failure continuation, pause, capacity, atomic batch, bounded history\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
