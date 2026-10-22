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
        std::cout << "PASS tray tooltip and serialized notification targets\n";
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
