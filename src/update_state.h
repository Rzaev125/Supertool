#pragma once
#include <windows.h>
#include <filesystem>
#include <cstdint>
namespace shiftwheel {
struct UpdatePolicy {
    int64_t deferred_until=0,retry_after=0;
    unsigned failures=0;
    std::wstring highest_version;
    bool check_allowed(int64_t now,bool manual)const{return manual||(now>=deferred_until&&now>=retry_after);}
    void later(int64_t now){deferred_until=now+24*60*60;}
    void failure(int64_t now){failures=failures<8?failures+1:8;retry_after=now+(60LL<<failures);}
    void success(){failures=0;retry_after=0;}
};
int64_t update_now();
bool may_install_update(bool requested,bool running,size_t pending);
UpdatePolicy load_update_policy(const std::filesystem::path& path);
void save_update_policy(const std::filesystem::path& path,const UpdatePolicy& policy);
void commit_install_request(const std::filesystem::path& path,UpdatePolicy& policy,bool& requested);
void cleanup_update_cache(const std::filesystem::path& root,const std::filesystem::path& protected_setup) noexcept;
void discard_update_download(const std::filesystem::path& setup) noexcept;
}
