#pragma once
#include "options.h"
namespace shiftwheel {
struct ResolvedProfile { ConversionOptions options; std::string profile_id; std::wstring profile_name; };
const wchar_t* category_sample(Category value);
const char* profile_category_key(Category value);
Category parse_profile_category(std::string_view key);
void validate_profiles(const Settings& settings);
bool valid_profile_id(std::string_view id);
bool valid_profile_name(std::wstring_view name);
ResolvedProfile resolve_profile(const std::filesystem::path& input, Format target, const Settings& settings);
std::vector<Format> profile_formats(const std::filesystem::path& input, const Settings& settings);
// template_index: -1=current options, 0=photo, 1=video, 2=audio.
ConversionProfile make_profile(Category category, const ConversionOptions& options, int template_index=-1);
std::wstring profile_consequences(const ConversionProfile& profile);
}
