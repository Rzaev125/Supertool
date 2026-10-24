// Automatic load measurement of the production Wheel; no Explorer/input simulation.
#include "wheel.h"
#include <psapi.h>
#include <iostream>
#include <stdexcept>
using namespace shiftwheel;
unsigned long long ticks(const FILETIME& time) { return (static_cast<unsigned long long>(time.dwHighDateTime) << 32) | time.dwLowDateTime; }
double cpu() {
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) throw std::runtime_error("process times unavailable");
    return (ticks(kernel) + ticks(user)) / 10000.0;
}
struct Sample { double wall, cpu; SIZE_T rss, peak; unsigned timers, paints; };
Sample sample(HWND window, DWORD duration) {
    const auto start = GetTickCount64(); const auto before = cpu();
    unsigned timers = 0, paints = 0;
    while (GetTickCount64() - start < duration) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.hwnd == window) { timers += message.message == WM_TIMER; paints += message.message == WM_PAINT; }
            TranslateMessage(&message); DispatchMessageW(&message);
        }
        const auto elapsed = GetTickCount64() - start;
        if (elapsed < duration) MsgWaitForMultipleObjectsEx(0, nullptr, static_cast<DWORD>(duration - elapsed), QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    }
    PROCESS_MEMORY_COUNTERS memory{sizeof(memory)};
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &memory, sizeof(memory))) throw std::runtime_error("process memory unavailable");
    return {static_cast<double>(GetTickCount64() - start), cpu() - before, memory.WorkingSetSize, memory.PeakWorkingSetSize, timers, paints};
}
void print(const char* name, const Sample& s, bool comma = true) {
    std::cout << '"' << name << "\":{\"wall_ms\":" << s.wall << ",\"cpu_ms\":" << s.cpu
        << ",\"rss_end_bytes\":" << s.rss << ",\"process_peak_rss_bytes\":" << s.peak
        << ",\"wheel_timers\":" << s.timers << ",\"wheel_paints\":" << s.paints << '}' << (comma ? "," : "");
}
int main() {
    if (FAILED(OleInitialize(nullptr))) return 1;
    Microsoft::WRL::ComPtr<Wheel> wheel;
    wheel.Attach(new Wheel([] { return false; }, [](auto, auto) {}));
    int result = 0;
    try {
        if (FAILED(wheel->initialize(GetModuleHandleW(nullptr)))) throw std::runtime_error("wheel initialization failed");
        const auto hidden = sample(wheel->handle(), 1000);
        wheel->show({600,400}); const auto opening = sample(wheel->handle(), 350);
        const auto settled = sample(wheel->handle(), 2000);
        wheel->hide(); const auto closing = sample(wheel->handle(), 350);
        const auto closed = sample(wheel->handle(), 1000);
        if (settled.timers || settled.paints || closed.timers || closed.paints) throw std::runtime_error("settled wheel is still polling/repainting");
        std::cout << '{'; print("hidden", hidden); print("opening", opening); print("settled", settled); print("closing", closing); print("closed", closed, false); std::cout << "}\n";
    } catch (const std::exception& error) { std::cerr << error.what(); result = 1; }
    wheel->shutdown(); wheel.Reset(); OleUninitialize(); return result;
}
