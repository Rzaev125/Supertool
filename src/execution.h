#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
namespace shiftwheel {
struct ExecutionFacts {
    std::optional<uint64_t> elapsed_ms, output_bytes;
    std::vector<std::string> stages;
    std::string encoder, backend;
    bool fallback = false;
};
bool valid_execution_facts(const ExecutionFacts& facts);
std::wstring execution_detail(const ExecutionFacts* facts, uint64_t input_size, bool input_known);
}
