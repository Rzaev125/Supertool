#include "job_queue.h"
#include <iostream>
#include <stdexcept>
using namespace shiftwheel;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main() {
    try {
        BatchOutcome outcome;outcome.failed=1;
        check(notify_batch(NotificationMode::Errors,outcome) && !notify_batch(NotificationMode::None,outcome),"notification filtering");
        outcome.failed=0;check(!notify_batch(NotificationMode::Errors,outcome) && notify_batch(NotificationMode::All,outcome),"success filtering");
        JobQueue batches;
        batches.enqueue({L"a.png",L"b.png"}, Format::Jpeg, {}, {});
        batches.enqueue({L"c.png"}, Format::Jpeg, {}, {});
        auto id1=batches.next()->id; batches.start(id1); batches.complete(id1,JobState::Succeeded,L"a.jpg",L"ready");
        check(batches.take_batch_outcomes().empty(),"group notified before all files finished");
        auto id2=batches.next()->id;batches.start(id2);batches.complete(id2,JobState::Failed,{},L"broken");
        auto summaries=batches.take_batch_outcomes();
        check(summaries.size()==1 && summaries[0].total==2 && summaries[0].succeeded==1 && summaries[0].failed==1 && summaries[0].last_output==L"a.jpg","separate group outcome");
        check(batches.take_batch_outcomes().empty(),"duplicate summary");
        batches.clear_pending();summaries=batches.take_batch_outcomes();
        check(summaries.size()==1 && summaries[0].cancelled==1,"removed pending group never finishes");
        JobQueue large;std::vector<std::filesystem::path> many(256,L"a.png");large.enqueue(many,Format::Jpeg,{},{});
        for(int i=0;i<256;++i){auto id=large.next()->id;large.start(id);large.complete(id,JobState::Succeeded,L"a.jpg",L"ready");}
        summaries=large.take_batch_outcomes();check(summaries.size()==1 && summaries[0].succeeded==256,"history trimming lost batch outcomes");
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
