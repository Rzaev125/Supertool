#include "image_exif.h"
#include <iostream>
#include <stdexcept>
using namespace shiftwheel;
namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
using Bytes = std::vector<BYTE>;
void put(Bytes& bytes, size_t at, uint32_t value, unsigned length, bool little = true) {
    for (unsigned i=0; i<length; ++i) bytes[at+(little?i:length-i-1)] = static_cast<BYTE>(value>>(8*i));
}
struct Tag { unsigned id, type; Bytes value; uint32_t count = 0; };
Bytes text(const char* value) { return {value, value+strlen(value)+1}; }
Bytes fixture(const std::vector<Tag>& tags, bool little=true) {
    Bytes bytes(8+2+12*tags.size()+4); bytes[0]=bytes[1]=little?'I':'M';
    put(bytes,2,42,2,little); put(bytes,4,8,4,little); put(bytes,8,static_cast<uint32_t>(tags.size()),2,little);
    for (size_t i=0;i<tags.size();++i) {
        const auto& tag=tags[i]; const auto at=10+i*12;
        put(bytes,at,tag.id,2,little); put(bytes,at+2,tag.type,2,little);
        put(bytes,at+4,tag.count?tag.count:static_cast<uint32_t>(tag.value.size()),4,little);
        if (tag.value.size()<=4) std::copy(tag.value.begin(),tag.value.end(),bytes.begin()+at+8);
        else { put(bytes,at+8,static_cast<uint32_t>(bytes.size()),4,little); bytes.insert(bytes.end(),tag.value.begin(),tag.value.end()); }
    }
    return bytes;
}
template<class F> void rejects(F f) { bool caught=false;try { f(); } catch(const std::runtime_error&) { caught=true; } check(caught,"malformed EXIF accepted"); }
}
int main() { try {
    for (bool little : {false,true}) for (unsigned orientation=1;orientation<=8;++orientation) {
        auto bytes=fixture({{274,3,{0,0},1}},little);put(bytes,18,orientation,2,little);
        check(inspect_tiff_exif(bytes,true).orientation==orientation,"orientation/endian");
    }
    for (auto tags : {std::vector<Tag>{{274,3,{0,0},1}},{{274,3,{9,0},1}},{{274,3,{1,0},1},{274,3,{1,0},1}},{{274,4,{1,0},1}}})
        rejects([&]{inspect_tiff_exif(fixture(tags),true);});
    auto broken=fixture({});put(broken,4,0,4);rejects([&]{inspect_tiff_exif(broken,true);});
    broken=fixture({});put(broken,8,4097,2);rejects([&]{inspect_tiff_exif(broken,true);});
    auto bytes=fixture({{315,2,text("Artist")},{33432,2,text("Copyright")},{40093,1,{'A',0,0,0}}});
    const auto valid=inspect_tiff_exif(bytes,true);
    check(valid.artist=="Artist"&&valid.copyright=="Copyright"&&valid.xp_author.size()==4,"selected fields");
    check(inspect_tiff_exif(bytes,false).artist.empty(),"disabled extraction");
    const auto duplicate=inspect_tiff_exif(fixture({{315,2,text("A")},{315,2,text("B")},{33432,2,text("C")},{33432,2,text("C")},{40093,1,{'A',0,0,0}},{40093,1,{'B',0,0,0}}}),true);
    check(duplicate.artist.empty()&&duplicate.copyright.empty()&&duplicate.xp_author.empty(),"duplicate selected fields");
    for (auto value : {Bytes{'a',0,'b',0},Bytes{'a',1,0},Bytes{'a',127,0},Bytes{'a','b'},Bytes(257,'a')})
        check(inspect_tiff_exif(fixture({{315,2,value}}),true).artist.empty(),"invalid optional text");
    check(inspect_tiff_exif(fixture({{315,1,text("Artist")}}),true).artist.empty(),"wrong optional type");
    check(inspect_tiff_exif(fixture({{315,2,{1,2,3,4},0xffffffff}}),true).artist.empty(),"oversized optional count");
    for (auto value : {Bytes{0,0,'A',0,0,0},Bytes{0,0xd8,0,0},Bytes{0,0xdc,0,0},Bytes{'A',0,0},Bytes{'A',0}})
        check(inspect_tiff_exif(fixture({{40093,1,value}}),true).xp_author.empty(),"invalid Unicode");
    const Bytes unicode{0x3d,0xd8,0,0xde,0,0};
    check(inspect_tiff_exif(fixture({{40093,1,unicode}}),true).xp_author==unicode,"Unicode surrogate pair");
    const Bytes latin{0xa9,' ',0xc4,0};
    check(inspect_tiff_exif(fixture({{33432,2,latin}}),true).copyright==std::string(reinterpret_cast<const char*>(latin.data()),3),"raw text encoding changed");
    bytes=fixture({{315,2,text("Artist")}});put(bytes,18,0xfffffff0,4);rejects([&]{inspect_tiff_exif(bytes,true);});
    for (const auto date : {"2024:02:29 23:59:59","2023:02:29 12:00:00","2026:10:23 24:00:00"}) {
        bytes=fixture({{34665,4,{26,0,0,0},1}});
        auto nested=fixture({{36867,2,text(date)}});
        put(nested,18,static_cast<uint32_t>(bytes.size()+18),4);
        bytes.insert(bytes.end(),nested.begin()+8,nested.end());
        check(inspect_tiff_exif(bytes,true).capture_date.empty()==(date[3]!='4'),"strict capture date");
    }
    std::cout<<"PASS bounded EXIF: orientation/endian, duplicate and optional fields, privacy extraction, Unicode, calendar, offsets\n";
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; } }
