#include "tray_notifications.h"
#include <iostream>
#include <stdexcept>
using namespace shiftwheel;
void check(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
int main() {
    try {
        check((tray_tooltip_flags & (NIF_TIP|NIF_SHOWTIP)) == (NIF_TIP|NIF_SHOWTIP),"v4 tooltip hidden");
        TrayNotifications queue;
        queue.push({L"A",L"ready",NIIF_INFO,L"a.jpg"});
        check(queue.begin() && queue.current()->target == L"a.jpg","first notification");
        queue.push({L"update",L"notice",NIIF_INFO,{}});
        queue.push({L"B",L"ready",NIIF_INFO,L"b.jpg"});
        check(!queue.begin() && queue.current()->target == L"a.jpg","pending notification overwrote shown target");
        check(queue.finish() == L"a.jpg","click must open A");
        check(queue.finish().empty(),"duplicate callback target");
        check(queue.begin() && queue.finish().empty(),"generic notification inherited output");
        check(queue.begin() && queue.current()->target == L"b.jpg","B lost");
        queue.restart();
        check(queue.begin() && queue.finish() == L"b.jpg","Explorer restart lost pending target");
        check(!queue.begin(),"duplicate notification");
        check(completion_folder(L"D:\\batch\\one.jpg",2,false)==L"D:\\batch","batch action must open common result folder");
        check(completion_folder(L"D:\\batch\\pages-png",2,true)==L"D:\\batch","PDF batch action must open common root, not last PDF pages");
        check(completion_folder(L"D:\\photos\\one.jpg",1,false)==L"D:\\photos","single result opens containing folder");
        check(completion_folder(L"D:\\docs\\pages-png",1,true)==L"D:\\docs\\pages-png","single PDF action opens actual page folder");
        check(completion_folder({},2,false).empty(),"failed batch must not offer an output action");
        queue.push({L"first",L"ready",NIIF_INFO,L"D:\\first\\one.jpg",L"D:\\first"});
        queue.push({L"second",L"ready",NIIF_INFO,L"D:\\second\\two.jpg",L"D:\\second"});
        check(queue.begin() && queue.current()->folder==L"D:\\first","first folder bound to first notification");
        check(!queue.begin() && queue.current()->folder==L"D:\\first","later batch cannot overwrite active button folder");
        queue.finish();check(queue.begin() && queue.current()->folder==L"D:\\second","next folder retained");queue.finish();
        std::cout << "PASS tray tooltip and serialized notification targets\n";
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
