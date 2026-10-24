#include "converter.h"
#include "worker.h"
#include "job_queue.h"
#include "profiles.h"
#include "engines.h"
#include <objbase.h>
#include <fstream>
#include <iomanip>
#include <iostream>
using namespace shiftwheel;
int wmain(int argc,wchar_t**argv){CoInitializeEx(nullptr,COINIT_MULTITHREADED);
 if(argc==8&&std::wstring(argv[1])==L"--convert"){try{convert_file(argv[2],argv[3],argv[4],parse_format(argv[5]),load_options(argv[7]));return 0;}catch(const std::exception&e){std::ofstream(std::wstring(argv[4])+L".error.txt")<<e.what();return 1;}}
 try{
  if(argc<4||argc>5)throw std::runtime_error("usage: probe input format [second-input]");
  std::filesystem::path input=std::filesystem::absolute(argv[1]);auto c=category(input);auto p=make_profile(c,{},c==Category::Image?0:c==Category::Video?1:c==Category::Audio?2:-1);p.name=L"Профиль Bakı";p.format=parse_format(argv[2]);p.options.image_max_dimension=640;p.options.video_max_height=240;p.options.video_acceleration=VideoAcceleration::Cpu;p.options.pdf_dpi=72;p.options.ocr_languages="eng";
  if(c==Category::Video)p.options.audio_bitrate_kbps=160;
  Settings s;s.profiles.push_back(p);s.active_profiles[c]=p.id;s.output_directory=std::filesystem::absolute(argv[3]);std::vector<std::filesystem::path> files{input};if(argc==5)files.push_back(std::filesystem::absolute(argv[4]));JobQueue q;q.enqueue_grouped(files,p.format,s);q.capture_inputs();
  while(auto*j=q.next()){auto snapshot=*j;q.start(snapshot.id);Worker w;w.start(snapshot.input,snapshot.format,nullptr,snapshot.options,snapshot.output_directory);auto r=w.finish();if(!r.success)std::cerr<<utf8(r.message)<<'\n';q.complete(snapshot.id,r.success?JobState::Succeeded:r.cancelled?JobState::Cancelled:JobState::Failed,r.success?r.output:std::filesystem::path{},r.message,r.facts);}
  std::cout<<"[";for(size_t i=0;i<q.items().size();++i){auto&j=q.items()[i];if(i)std::cout<<',';auto&f=*j.facts;std::cout<<"{\"success\":"<<(j.state==JobState::Succeeded?"true":"false")<<",\"output\":"<<std::quoted(utf8(j.output.wstring()))<<",\"profile\":"<<std::quoted(utf8(j.profile_name))<<",\"elapsed_ms\":"<<f.elapsed_ms.value_or(0)<<",\"output_bytes\":"<<(f.output_bytes?std::to_string(*f.output_bytes):"null")<<",\"encoder\":"<<std::quoted(f.encoder)<<",\"backend\":"<<std::quoted(f.backend)<<",\"stages\":[";for(size_t k=0;k<f.stages.size();++k){if(k)std::cout<<',';std::cout<<std::quoted(f.stages[k]);}std::cout<<"]}";}std::cout<<"]\n";CoUninitialize();return 0;
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';CoUninitialize();return 1;}}
