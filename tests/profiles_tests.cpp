#include "profiles.h"
#include "job_store.h"
#include <windows.h>
#include <fstream>
#include <algorithm>
#include <iostream>
using namespace shiftwheel;
void check(bool v,const char* m){if(!v)throw std::runtime_error(m);}
template<class F> void reject(F f){bool bad=false;try{f();}catch(...){bad=true;}check(bad,"invalid profile accepted");}
int main(){try{
 auto root=std::filesystem::temp_directory_path()/(L"sw-profiles-"+std::to_wstring(GetCurrentProcessId()));std::filesystem::create_directory(root);
 auto p=make_profile(Category::Image,{},0);p.name=L"Фото Bakı";
 Settings s;s.conversion.video_max_height=720;s.profiles.push_back(p);s.active_profiles[Category::Image]=p.id;
 validate_profiles(s);save_settings(root/L"settings.ini",s);auto t=load_settings(root/L"settings.ini");
 check(t.profiles.size()==1&&t.profiles[0].name==p.name&&t.active_profiles==s.active_profiles,"Unicode round trip");
 auto r=resolve_profile(L"a.png",Format::Jpeg,t);check(r.options.image_max_dimension==1920&&r.options.video_max_height==720&&r.profile_name==p.name,"category overlay");
 check(resolve_profile(L"a.png",Format::Png,t).profile_id.empty(),"other sector used profile");
 check(resolve_profile(L"a.mp4",Format::Mp4,t).profile_id.empty(),"other category used profile");
 s.wheels[Category::Image]={Format::Png};check(profile_formats(L"a.png",s)==std::vector<Format>{Format::Png},"hidden target resurrected");
 auto a=available_formats(L"a.png",profile_formats(L"a.png",t),[](auto){return false;});check(std::find(a.begin(),a.end(),Format::Heic)==a.end(),"missing module resurrected");
 std::ofstream(root/L"legacy.ini")<<"version=1\nquality=high\n";auto old=load_settings(root/L"legacy.ini");check(old.profiles.empty()&&old.conversion.quality==QualityPreset::High,"legacy reset");
 auto bad=t;bad.profiles[0].name=L"\n";reject([&]{validate_profiles(bad);});
 bad=t;bad.profiles[0].id="x";reject([&]{validate_profiles(bad);});
 bad=t;bad.active_profiles[Category::Video]=p.id;reject([&]{validate_profiles(bad);});
 bad=t;bad.profiles.push_back(p);bad.profiles.back().id=make_profile(Category::Image,{},0).id;reject([&]{validate_profiles(bad);});
 bad={};for(int i=0;i<32;++i){auto x=make_profile(Category::Audio,{},2);x.name=L"audio "+std::to_wstring(i);bad.profiles.push_back(x);}validate_profiles(bad);bad.profiles.push_back(make_profile(Category::Video,{},1));reject([&]{validate_profiles(bad);});
 bad=t;bad.profiles[0].name=std::wstring(65,L'x');reject([&]{validate_profiles(bad);});
 bad={};auto v=make_profile(Category::Video,{},1);v.format=Format::Webm;bad.profiles.push_back(v);reject([&]{validate_profiles(bad);});
 check(profile_consequences(p).find(L"1920")!=std::wstring::npos&&profile_consequences(p).find(L"HDR")!=std::wstring::npos,"missing consequences");
 std::ofstream(root/L"partial.ini")<<"version=1\nprofile."<<p.id<<".name=partial\n";reject([&]{load_settings(root/L"partial.ini");});
 auto input=root/L"a.png";std::ofstream(input)<<"fixture";
 JobQueue q;q.enqueue_grouped({input},Format::Jpeg,t);auto original=*q.next();
 t.profiles.clear();t.active_profiles.clear();check(q.next()->profile_name==p.name&&q.next()->options.image_max_dimension==1920,"admission profile not frozen");
 ExecutionFacts facts;facts.elapsed_ms=123;facts.output_bytes=45;facts.stages={"wic"};
 q.start(original.id);q.complete(original.id,JobState::Succeeded,root/L"a.jpg",L"ok");q.find(original.id)->facts=facts;
 save_queue(root/L"queue.dat",q.snapshot());auto saved=load_queue(root/L"queue.dat");check(saved->jobs[0].facts->output_bytes==45&&saved->jobs[0].profile_name==p.name,"history facts/profile lost");
 q.enqueue_retry(saved->jobs[0]);check(q.next()->profile_name==p.name&&!q.next()->facts&&q.next()->options.image_max_dimension==1920,"retry did not freeze/reset");
 auto count=q.items().size();reject([&]{q.enqueue_grouped({input,root/L"bad.docx"},Format::Jpeg,t);});check(q.items().size()==count,"profile admission not atomic");
 auto invalid=saved.value();invalid.jobs[0].facts->stages={"invented-gpu"};reject([&]{save_queue(root/L"bad-queue.dat",invalid);});
 std::filesystem::remove_all(root);std::cout<<"profile contracts PASS\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
