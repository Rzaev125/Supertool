#pragma once
#include "module_paths.h"
#include "updates.h"
#include <vector>
#include <span>
namespace shiftwheel {
struct ModuleSpec {
 std::string id;std::wstring name,version,url,sha256;
 unsigned long long bytes=0,unpacked=0;size_t count=0;
 std::string dependency;std::vector<std::string> required;std::wstring inventory_sha256;
};
struct ModuleStatus { bool installed=false;std::wstring version; };
struct ModuleInstallResult { bool cache_retained=false; };
const std::vector<ModuleSpec>& module_catalog();
bool safe_module_path(std::string_view path);
ModuleStatus module_status(const std::filesystem::path& app,const ModuleSpec& spec);
void verify_module_archive(const std::filesystem::path& cab,const ModuleSpec& spec);
void extract_module_cab(const std::filesystem::path& cab,const std::filesystem::path& fresh_directory,const ModuleSpec& spec,std::stop_token stop);
ModuleInstallResult install_module(const std::filesystem::path& app,const ModuleSpec& spec,std::stop_token stop,const UpdateProgress& progress={},const UpdateTransport& transport={});
void cleanup_module_downloads(const std::filesystem::path& app,std::span<const ModuleSpec> catalog=module_catalog()) noexcept;
// Installs no dependent modules implicitly; callers must install advertised dependencies first.
void remove_module(const std::filesystem::path& app,const ModuleSpec& spec);
}
