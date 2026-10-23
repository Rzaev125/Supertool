#include "job_queue.h"
#include "job_store.h"
#include "engines.h"
#include <fstream>
#include <iostream>

using namespace shiftwheel;
namespace fs = std::filesystem;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void reject(F action) {
    bool failed = false; try { action(); } catch (const std::exception&) { failed = true; }
    check(failed, "invalid batch accepted");
}
size_t entries(const fs::path& folder) { return static_cast<size_t>(std::distance(fs::directory_iterator(folder), fs::directory_iterator{})); }
int main() {
    const auto root = fs::current_path() / L"test-output" / (L"batch-output-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    fs::create_directories(root / L"Исходные"); fs::create_directories(root / L"Другие"); fs::create_directory(root / L"Результаты");
    struct Cleanup { fs::path root; ~Cleanup() { remove_owned_tree(root); } } cleanup{root};
    try {
        const auto a = root / L"Исходные" / L"Фото.png", b = root / L"Другие" / L"Фото.png";
        for (auto file : {a, b}) { std::ofstream f(file); f << "original"; }
        const auto source = a.parent_path(), destination = root / L"Результаты";
        JobQueue queue;
        queue.enqueue_grouped({a, b}, Format::Jpeg, {}, {});
        const auto folder = queue.items()[0].output_directory;
        check(folder.parent_path() == source && folder != source && fs::is_directory(folder), "multi-file drop must create a folder beside first source");
        check(queue.items()[1].output_directory == folder && queue.items()[0].batch_id == queue.items()[1].batch_id, "different source folders must share one result folder");
        { std::ofstream f(folder / L"keep.txt"); f << "existing result"; }
        queue.enqueue_grouped({a, b}, Format::Jpeg, {}, {});
        const auto repeated = queue.items()[2].output_directory;
        check(repeated != folder && repeated.parent_path() == source && fs::is_empty(repeated), "repeated drops must use a fresh numbered folder");
        check(fs::file_size(folder / L"keep.txt") == 15, "existing folder contents must remain intact");
        { std::ofstream f(destination / folder.filename()); f << "existing file"; }
        queue.enqueue_grouped({a, b}, Format::Jpeg, {}, destination);
        const auto custom = queue.items()[4].output_directory;
        check(custom.parent_path() == destination && custom.filename() != folder.filename() && fs::is_directory(custom), "custom output root and file-name collisions respected");
        const auto before_single = entries(destination);
        queue.enqueue_grouped({a}, Format::Jpeg, {}, destination);
        check(queue.items().back().output_directory == destination && entries(destination) == before_single, "single custom output must not create a subfolder");
        queue.enqueue_grouped({a}, Format::Jpeg, {}, {});
        check(queue.items().back().output_directory.empty(), "single default output remains beside source");
        queue.capture_inputs(); save_queue(root / L"queue.dat", queue.snapshot());
        JobQueue restored; restored.restore(*load_queue(root / L"queue.dat"));
        check(restored.items()[0].output_directory == folder && restored.items()[1].output_directory == folder, "recovery must reuse original batch folder");
        const auto before = queue.items().size(), source_entries = entries(source), destination_entries = entries(destination);
        reject([&] { queue.enqueue_grouped({a, root / L"bad.docx"}, Format::Jpeg, {}, destination); });
        reject([&] { queue.enqueue_grouped({a, root / L"missing.png"}, Format::Jpeg, {}, destination); });
        reject([&] { queue.enqueue_grouped({a, b}, Format::Jpeg, {}, L"relative"); });
        reject([&] { queue.enqueue_grouped({a, b}, Format::Jpeg, {}, root / L"missing-parent"); });
        reject([&] { queue.enqueue_grouped({a, b}, Format::Jpeg, {}, a); });
        check(queue.items().size() == before && entries(source) == source_entries && entries(destination) == destination_entries, "rejected batch must create no folders or partial jobs");
        JobQueue full; full.enqueue(std::vector<fs::path>(256, a), Format::Jpeg, {}, {});
        reject([&] { full.enqueue_grouped({a, b}, Format::Jpeg, {}, destination); });
        check(full.pending() == 256 && entries(destination) == destination_entries, "full queue must not create an empty result folder");
        const auto pdf1 = source / L"one.pdf", pdf2 = source / L"two.pdf";
        for (auto file : {pdf1, pdf2}) { std::ofstream f(file); f << "fixture"; }
        JobQueue pages; pages.enqueue_grouped({pdf1, pdf2}, Format::Png, {}, destination);
        check(pages.items()[0].output_directory == pages.items()[1].output_directory && fs::is_directory(pages.items()[0].output_directory), "page-producing formats share the batch root too");
        check(folder_output(pdf1, Format::Png), "PDF page subfolders remain enabled inside batch folder");
        check(fs::file_size(a) == 8 && fs::file_size(b) == 8, "source files must remain unchanged");
        std::cout << "PASS grouped folders, numbering, custom root, single jobs, recovery and atomic rejection\n";
    } catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
