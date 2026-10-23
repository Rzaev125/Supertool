#pragma once
#include "engines.h"
namespace shiftwheel {
std::filesystem::path application_directory();
std::filesystem::path module_directory(const std::filesystem::path& app,const std::string& id);
std::filesystem::path module_directory(const std::string& id);
class ModuleLease {
    WinHandle handle_;
public:
    ModuleLease(const std::filesystem::path& app,bool exclusive);
};
}
