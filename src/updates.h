#pragma once
#include <windows.h>
#include <filesystem>
#include <string>
#include <stop_token>
#include <functional>
#include <ostream>
namespace shiftwheel {
inline constexpr wchar_t app_version[] = L"0.13.0";
struct UpdateRelease { std::wstring version, url, sha256; unsigned long long size = 0; std::wstring notes; };
bool https_url(const std::wstring& url);
bool newer_version(const std::wstring& candidate, const std::wstring& current);
UpdateRelease parse_update_manifest(const std::string& text);
std::wstring file_sha256(const std::filesystem::path& path);
UpdateRelease fetch_update(const std::wstring& channel, std::stop_token stop);
using UpdateTransport = std::function<void(const std::wstring&, unsigned long long, std::ostream&, std::stop_token)>;
using UpdateProgress = std::function<void(unsigned long long,unsigned long long)>;
void download_asset(const std::wstring& url,unsigned long long bytes,std::ostream& output,std::stop_token stop,const UpdateProgress& progress={});
std::filesystem::path download_update(const UpdateRelease& release, std::stop_token stop, const UpdateTransport& transport = {},const UpdateProgress& progress = {});
bool installed_copy(const std::filesystem::path& executable);
bool startup_enabled(const std::filesystem::path& executable);
void set_startup(const std::filesystem::path& executable, bool enabled);
void launch_update_helper(const std::filesystem::path& installer, const std::filesystem::path& executable, const std::wstring& hash);
int apply_update(int argc, wchar_t** argv);
}
