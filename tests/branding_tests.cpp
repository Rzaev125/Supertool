#include "branding.h"
#include "options.h"
#include <windows.h>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace shiftwheel;
namespace fs = std::filesystem;
void check(bool value) { if (!value) throw std::runtime_error("branding state compatibility assertion"); }
int main() {
    const auto root=fs::temp_directory_path()/(L"supertool-branding-"+std::to_wstring(GetCurrentProcessId()));
    try {
        fs::create_directory(root);
        for (auto suffix : {L".ini",L".queue",L".diagnostics.log",L".update-state"}) {
            const auto current=root/(std::wstring(L"Supertool")+suffix), legacy=root/(std::wstring(L"ShiftWheel")+suffix);
            check(application_state_path(root,suffix)==current);
            {std::ofstream f(legacy);f<<"legacy";}
            check(application_state_path(root,suffix)==legacy);
            {std::ofstream f(current);f<<"current";}
            check(application_state_path(root,suffix)==current);
            check(fs::file_size(legacy)==6 && fs::file_size(current)==7);
        }
        Settings original; original.auto_show_progress=false; original.conversion.image_max_dimension=1600;
        save_settings(root/L"ShiftWheel.ini",original);fs::remove(root/L"Supertool.ini");
        const auto restored=load_settings(application_state_path(root,L".ini"));
        check(!restored.auto_show_progress && restored.conversion.image_max_dimension==1600);
        fs::remove_all(root);std::cout<<"PASS Supertool state names, legacy preference preservation, new-name precedence and no destructive migration\n";return 0;
    } catch (const std::exception& error) {
        std::cerr<<error.what()<<'\n';std::error_code ec;fs::remove_all(root,ec);return 1;
    }
}
