#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <filesystem>
namespace shiftwheel {
struct ExecutionFacts {
    std::optional<uint64_t> elapsed_ms, output_bytes;
    std::vector<std::string> stages;
    std::string encoder, backend;
    bool fallback = false;
};
bool valid_execution_facts(const ExecutionFacts& facts);
bool parse_execution(std::string_view line,ExecutionFacts& facts);
void report_engine(std::string_view stage) noexcept;
void report_encoder(std::string_view encoder,std::string_view backend,bool fallback) noexcept;
// Only a result created by this worker; directory outputs are flat and never traversed recursively.
void report_output_size(const std::filesystem::path& owned_result) noexcept;
std::wstring execution_detail(const ExecutionFacts* facts, uint64_t input_size, bool input_known);
}
