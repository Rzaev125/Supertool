#include "animation_converter.h"
#include "engines.h"
#include "image_converter.h"
#include "webp_converter.h"
#include "avif_converter.h"
#include "heic_converter.h"
#include "progress.h"
#include <fstream>
#include <array>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <algorithm>
namespace shiftwheel {
namespace fs=std::filesystem;
namespace {
[[noreturn]] void fail(){throw std::runtime_error("Не удалось сохранить кадры и временную шкалу анимации.");}
std::wstring frame_name(size_t i,const wchar_t* ext){std::wostringstream s;s<<L"frame-"<<std::setfill(L'0')<<std::setw(4)<<i+1<<ext;return s.str();}
std::vector<std::wstring> arguments(const ConversionOptions& o){auto t=std::to_wstring(resource_threads(o.resources));return {L"-hide_banner",L"-nostdin",L"-loglevel",L"error",L"-xerror",L"-max_error_rate",L"0",L"-n",L"-threads",t,L"-filter_threads",t,L"-filter_complex_threads",t,L"-protocol_whitelist",L"file,pipe"};}
void run(std::vector<std::wstring> args,const fs::path& log,const ConversionOptions& o){args.insert(args.end()-1,{L"-threads",std::to_wstring(resource_threads(o.resources))});run_engine(module_path(Category::Video),args,log,300000);}
unsigned le(const unsigned char* p,unsigned n){unsigned v=0;for(unsigned i=0;i<n;++i)v|=unsigned(p[i])<<(8*i);return v;}
void number(std::ostream& f,uint64_t v,unsigned n){for(unsigned i=0;i<n;++i){f.put(static_cast<char>(v&255));v>>=8;}}
void chunk(std::ostream& out,const char* tag,const std::string& data){out.write(tag,4);number(out,data.size(),4);out.write(data.data(),static_cast<std::streamsize>(data.size()));if(data.size()%2)out.put(0);}
// Compose full-canvas frames ourselves: avoids encoder frame merging and last-frame duration loss.
void assemble_webp(const fs::path& output,const fs::path& work,const AnimationInfo& info){
 if(info.plays>65535)throw std::runtime_error("WebP допускает не более 65535 повторений; число повторений GIF слишком велико.");
 std::ofstream out(output,std::ios::binary);if(!out)fail();out.write("RIFF",4);number(out,0,4);out.write("WEBP",4);
 std::ostringstream header(std::ios::binary);header.put(2);number(header,0,3);number(header,info.width-1,3);number(header,info.height-1,3);chunk(out,"VP8X",header.str());
 std::ostringstream control(std::ios::binary);number(control,0,4);number(control,info.plays,2);chunk(out,"ANIM",control.str());bool alpha=false;
 for(size_t i=0;i<info.delays.size();++i){
  auto path=work/frame_name(i,L".webp");if(fs::file_size(path)>256ULL*1024*1024)fail();std::ifstream file(path,std::ios::binary);std::string raw((std::istreambuf_iterator<char>(file)),{});
  if(raw.size()<20||memcmp(raw.data(),"RIFF",4)||memcmp(raw.data()+8,"WEBP",4))fail();
  std::string pixels;bool image=false;
  for(size_t p=12;p<raw.size();){if(raw.size()-p<8)fail();auto n=le(reinterpret_cast<const unsigned char*>(raw.data()+p+4),4);auto next=p+8+size_t(n)+(n&1);if(next>raw.size())fail();
   auto tag=raw.substr(p,4);
   if(tag=="VP8X"){if(n!=10)fail();alpha=alpha||(static_cast<unsigned char>(raw[p+8])&16)!=0;}
   else if(tag=="ALPH"||tag=="VP8 "||tag=="VP8L"){
    if(tag!="ALPH"){if(image)fail();image=true;}
    if(tag=="VP8L"){if(n<5)fail();alpha=alpha||(static_cast<unsigned char>(raw[p+12])&16)!=0;}
    pixels.append(raw,p,next-p);
   }else fail();p=next;
  }if(!image)fail();
  std::ostringstream payload(std::ios::binary);number(payload,0,6);number(payload,info.width-1,3);number(payload,info.height-1,3);number(payload,info.delays[i],3);payload.put(2);payload.write(pixels.data(),static_cast<std::streamsize>(pixels.size()));chunk(out,"ANMF",payload.str());
  if(out.tellp()>256LL*1024*1024)throw std::runtime_error("Лимит выходной анимации: 256 МиБ.");if(!out)fail();
 }
 auto end=out.tellp();out.seekp(4);number(out,static_cast<uint64_t>(end)-8,4);out.seekp(20);out.put(alpha?18:2);out.flush();if(!out)fail();out.close();
 auto actual=inspect_animation(output);if(actual.width!=info.width||actual.height!=info.height||actual.delays!=info.delays||actual.plays!=info.plays)fail();
}
}
void convert_animation(const fs::path& input,const fs::path& output,Format format,const fs::path& work,const ConversionOptions& options){
 validate_options(options);require_engine(Category::Video);bool animated=animation_input(input);AnimationInfo info;
 if(animated)info=inspect_animation(input);
 auto source=parse_format(input.extension().wstring());auto frames=work/L"animation-frames";if(!fs::create_directory(frames))fail();
 bool keep_sequence=format==Format::Gif||format==Format::Webp;
 if(animated){
  report_progress(-1,keep_sequence?L"Декодирование кадров анимации":L"Извлечение первого кадра анимации");auto args=arguments(options);
  args.insert(args.end(),{L"-err_detect",L"explode",L"-ignore_loop",L"1",L"-min_delay",L"0",L"-i",input.wstring(),L"-map",L"0:v:0",L"-an",L"-sn",L"-dn",L"-frames:v",std::to_wstring(keep_sequence?info.delays.size():1),L"-fps_mode",L"passthrough",L"-c:v",L"png",L"-pix_fmt",L"rgba"});
  if(options.image_max_dimension&&std::max(info.width,info.height)>options.image_max_dimension){auto limit=options.image_max_dimension;auto width=std::max(1U,static_cast<unsigned>(uint64_t(info.width)*limit/std::max(info.width,info.height)));auto height=std::max(1U,static_cast<unsigned>(uint64_t(info.height)*limit/std::max(info.width,info.height)));info.width=width;info.height=height;args.insert(args.end(),{L"-vf",L"scale="+std::to_wstring(width)+L":"+std::to_wstring(height)+L":flags=lanczos"});}
  args.push_back((frames/L"frame-%04d.png").wstring());run(args,work/L"animation-decode.log",options);
 }else{
  auto first=work/L"animation-still.png";
  if(source==Format::Webp)convert_webp(input,first,Format::Png,work,options);
  else if(source==Format::Avif)convert_avif(input,first,Format::Png,work,options);
  else if(source==Format::Heic)convert_heic(input,first,Format::Png,work,options);
  else convert_image(input,first,work/L"animation-still.tmp",ImageColor::ToSrgb,options);
  if(!MoveFileExW(first.c_str(),(frames/frame_name(0,L".png")).c_str(),MOVEFILE_WRITE_THROUGH))fail();
  info.delays={100};info.plays=1;
 }
 auto count=keep_sequence?info.delays.size():1;size_t files=0;for(auto& entry:fs::directory_iterator(frames))if(entry.path().extension()==L".png")++files;if(files!=count)fail();for(size_t i=0;i<count;++i)if(!fs::is_regular_file(frames/frame_name(i,L".png")))fail();
 auto first=frames/frame_name(0,L".png");
 if(!keep_sequence){
  if(format==Format::Avif)convert_avif(first,output,format,work,options);
  else if(format==Format::Heic)convert_heic(first,output,format,work,options);
  else convert_image(first,output,work/L"animation-first.tmp",ImageColor::ToSrgb,options);return;
 }
 report_progress(-1,L"Кодирование анимации");auto args=arguments(options);
 if(format==Format::Webp){
  args.insert(args.end(),{L"-framerate",L"1",L"-i",(frames/L"frame-%04d.png").wstring(),L"-frames:v",std::to_wstring(count),L"-c:v",L"libwebp",L"-lossless",options.quality==QualityPreset::Compact?L"0":L"1",L"-quality",options.quality==QualityPreset::High?L"100":L"75",L"-compression_level",L"4",L"-pix_fmt",L"bgra",L"-f",L"image2",(frames/L"frame-%04d.webp").wstring()});
  run(args,work/L"animation-webp.log",options);assemble_webp(output,frames,info);
 }else{
  if(!animated){auto args2=arguments(options);args2.insert(args2.end(),{L"-i",first.wstring(),L"-frames:v",L"1",L"-vf",L"split[a][b];[a]palettegen=stats_mode=single[p];[b][p]paletteuse=new=1",L"-loop",L"-1",output.wstring()});run(args2,work/L"animation-gif.log",options);return;}
  auto timeline=frames/L"timeline.ffconcat";std::ofstream list(timeline,std::ios::binary);list<<"ffconcat version 1.0\n";
  for(size_t i=0;i<count;++i){auto d=std::max(10U,((info.delays[i]+5)/10)*10);info.delays[i]=d;list<<"file '"<<utf8(frame_name(i,L".png"))<<"'\noption framerate 1000\nduration "<<d/1000<<'.'<<std::setfill('0')<<std::setw(3)<<d%1000<<"\n";}list.close();if(!list)fail();
  args.insert(args.end(),{L"-f",L"concat",L"-safe",L"0",L"-i",timeline.wstring(),L"-frames:v",std::to_wstring(count),L"-fps_mode",L"passthrough",L"-vf",L"split[a][b];[a]palettegen=stats_mode=single:reserve_transparent=1[p];[b][p]paletteuse=new=1:alpha_threshold=128:dither=sierra2_4a",L"-gifflags",L"0",L"-loop",info.plays==0?L"0":info.plays==1?L"-1":std::to_wstring(info.plays-1),L"-final_delay",std::to_wstring(info.delays.back()/10),output.wstring()});
  run(args,work/L"animation-gif.log",options);auto actual=inspect_animation(output);if(actual.delays!=info.delays||actual.plays!=info.plays)fail();
 }
}
}
