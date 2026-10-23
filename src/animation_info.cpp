#include "animation_converter.h"
#include "engines.h"
#include <cstring>
#include <cstdint>
#include <algorithm>
#include "file_policy.h"
#include <fstream>
namespace shiftwheel {
namespace fs=std::filesystem;
namespace {
[[noreturn]] void invalid(){throw std::runtime_error("Повреждённая или неподдерживаемая анимация GIF/WebP.");}
struct Bytes {
 WinHandle file,map;const unsigned char* data=nullptr;size_t size=0;
 explicit Bytes(const fs::path& p):file(CreateFileW(p.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr)){
  if(file.value==INVALID_HANDLE_VALUE)invalid();LARGE_INTEGER n{};if(!GetFileSizeEx(file.value,&n)||n.QuadPart<1||n.QuadPart>256LL*1024*1024)throw std::runtime_error("Лимит файла анимации: 256 МиБ.");size=static_cast<size_t>(n.QuadPart);
  map.value=CreateFileMappingW(file.value,nullptr,PAGE_READONLY,0,0,nullptr);if(!map.value)invalid();data=static_cast<const unsigned char*>(MapViewOfFile(map.value,FILE_MAP_READ,0,0,0));if(!data)invalid();
 }
 ~Bytes(){if(data)UnmapViewOfFile(data);}
 const unsigned char* at(size_t p,size_t n)const{if(p>size||n>size-p)invalid();return data+p;}
 unsigned le(size_t p,unsigned n=1)const{auto b=at(p,n);unsigned value=0;for(unsigned i=0;i<n;++i)value|=unsigned(b[i])<<(8*i);return value;}
 bool tag(size_t p,const char* s,size_t n=4)const{return !memcmp(at(p,n),s,n);}
 size_t blocks(size_t p)const{for(;;){auto n=le(p++);if(!n)return p;at(p,n);p+=n;}}
};
void canvas(AnimationInfo& a){if(!a.width||!a.height||a.width>32768||a.height>32768||uint64_t(a.width)*a.height>40000000)throw std::runtime_error("Лимит холста анимации: 40 млн пикселей.");}
void frame(AnimationInfo& a,unsigned delay){
 if(a.delays.size()>=1000||uint64_t(a.width)*a.height*(a.delays.size()+1)>200000000)throw std::runtime_error("Лимит анимации: 1000 кадров и 200 млн пикселей суммарно.");
 if(!delay)delay=100;a.delays.push_back(delay);uint64_t total=0;for(auto d:a.delays)total+=d;if(total>600000)throw std::runtime_error("Лимит длительности анимации: 10 минут.");
}
AnimationInfo gif(const Bytes& b){
 if(!b.tag(0,"GIF87a",6)&&!b.tag(0,"GIF89a",6))invalid();b.at(0,13);AnimationInfo a{b.le(6,2),b.le(8,2)};canvas(a);
 size_t p=13;if(b.le(10)&128)p+=3ULL*(2ULL<<(b.le(10)&7));b.at(p,0);unsigned delay=100;bool gce=false,loop=false;
 while(p<b.size){auto type=b.le(p++);
  if(type==0x3b){if(p!=b.size||a.delays.empty()||gce)invalid();return a;}
  if(type==0x21){auto label=b.le(p++);
   if(label==0xf9){if(gce||b.le(p)!=4||(b.le(p+1)&0xe0)||((b.le(p+1)>>2)&7)>3||b.le(p+5))invalid();delay=b.le(p+2,2)*10;p+=6;gce=true;}
   else if(label==0xff){auto len=b.le(p++);if(len!=11)invalid();b.at(p,11);bool known=b.tag(p,"NETSCAPE2.0",11)||b.tag(p,"ANIMEXTS1.0",11);p+=11;
    if(known){if(loop||b.le(p)!=3||b.le(p+1)!=1||b.le(p+4))invalid();auto repeats=b.le(p+2,2);a.plays=repeats?repeats+1:0;loop=true;}p=b.blocks(p);
   }else if(label==0xfe)p=b.blocks(p);else invalid();
  }else if(type==0x2c){auto x=b.le(p,2),y=b.le(p+2,2),w=b.le(p+4,2),h=b.le(p+6,2),flags=b.le(p+8);p+=9;
   if(!w||!h||x+w>a.width||y+h>a.height||(flags&0x18))invalid();if(flags&128)p+=3ULL*(2ULL<<(flags&7));auto code=b.le(p++);if(code<2||code>8)invalid();p=b.blocks(p);frame(a,delay);delay=100;gce=false;
  }else invalid();
 }invalid();
}
void bitstream(const Bytes& b,size_t p,size_t end,unsigned width,unsigned height){
 bool pixels=false,alpha=false;
 while(p<end){b.at(p,8);auto n=b.le(p+4,4);size_t next=p+8+size_t(n)+(n&1U);if(next>end)invalid();
  if(b.tag(p,"ALPH")){if(alpha||pixels||!n)invalid();alpha=true;}
  else if(b.tag(p,"VP8L")){if(pixels||alpha||n<5||b.le(p+8)!=0x2f||(b.le(p+12)&0xe0))invalid();auto bits=b.le(p+9,4);if((bits&0x3fff)+1!=width||((bits>>14)&0x3fff)+1!=height)invalid();pixels=true;}
  else if(b.tag(p,"VP8 ")){if(pixels||n<10||(b.le(p+8)&1)||!b.tag(p+11,"\x9d\x01\x2a",3)||(b.le(p+14,2)&0x3fff)!=width||(b.le(p+16,2)&0x3fff)!=height)invalid();pixels=true;}
  else invalid();if((n&1)&&b.le(next-1))invalid();p=next;
 }if(!pixels||p!=end)invalid();
}
AnimationInfo webp(const Bytes& b){
 if(!b.tag(0,"RIFF")||!b.tag(8,"WEBP")||uint64_t(b.le(4,4))+8!=b.size||!b.tag(12,"VP8X")||b.le(16,4)!=10)invalid();
 auto flags=b.le(20);if(!(flags&2)||(flags&0xc1)||b.le(21,3))invalid();
 if(flags&0x28)throw std::runtime_error("Анимированный WebP с ICC/EXIF: сначала примените цветовой профиль и поворот в редакторе.");
 AnimationInfo a{b.le(24,3)+1,b.le(27,3)+1};canvas(a);bool control=false;size_t p=30;unsigned chunks=0;
 while(p<b.size){b.at(p,8);auto n=b.le(p+4,4);size_t next=p+8+size_t(n)+(n&1U);if(next>b.size||++chunks>65536)invalid();
  if(b.tag(p,"ANIM")){if(control||!a.delays.empty()||n!=6)invalid();a.plays=b.le(p+12,2);control=true;}
  else if(b.tag(p,"ANMF")){if(!control||n<24)invalid();auto x=b.le(p+8,3)*2,y=b.le(p+11,3)*2,w=b.le(p+14,3)+1,h=b.le(p+17,3)+1;if(uint64_t(x)+w>a.width||uint64_t(y)+h>a.height||(b.le(p+23)&0xfc))invalid();bitstream(b,p+24,p+8+n,w,h);frame(a,b.le(p+20,3));}
  else if(b.tag(p,"ICCP")||b.tag(p,"EXIF")||b.tag(p,"VP8X")||b.tag(p,"VP8 ")||b.tag(p,"VP8L")||b.tag(p,"ALPH"))invalid();
  if((n&1)&&b.le(next-1))invalid();p=next;
 }if(!control||a.delays.empty())invalid();return a;
}
}
bool animation_input(const fs::path& path){
 auto f=parse_format(path.extension().wstring());if(f==Format::Gif)return true;if(f!=Format::Webp)return false;
 validate_local_path(path,true);std::ifstream in(path,std::ios::binary);unsigned char header[30]{};in.read(reinterpret_cast<char*>(header),30);
 return in.gcount()==30&&!memcmp(header,"RIFF",4)&&!memcmp(header+8,"WEBP",4)&&!memcmp(header+12,"VP8X",4)&&(header[20]&2);
}
AnimationInfo inspect_animation(const fs::path& path){Bytes b(path);return parse_format(path.extension().wstring())==Format::Gif?gif(b):webp(b);}
}
