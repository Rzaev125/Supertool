#include "job_store.h"
#include <fstream>
#include <iostream>
using namespace shiftwheel;
void check(bool b,const char* s){if(!b)throw std::runtime_error(s);}
template<class F>void reject(F f){bool failed=false;try{f();}catch(...){failed=true;}check(failed,"corruption accepted");}
int main(){try{
 auto dir=std::filesystem::current_path()/L"test-output"/(L"store-"+std::to_wstring(GetCurrentProcessId()));std::filesystem::create_directories(dir);
 auto input=dir/L"Исходник ə.png",path=dir/L"queue.dat";
 {std::ofstream f(input);f<<"source";}
 JobQueue queue;ConversionOptions options;options.pdf_dpi=300;options.ocr_languages="rus+aze";options.video_codec=VideoCodec::Av1;
 queue.enqueue({input,input},Format::Jpeg,options,dir);queue.capture_inputs();auto id=queue.next()->id;queue.start(id);
 save_queue(path,queue.snapshot());auto snapshot=load_queue(path);check(snapshot.has_value(),"snapshot missing");
 check(snapshot->jobs[1].options.video_codec==VideoCodec::Av1,"durable codec lost");
 auto legacy=*load_local_state(path);check(legacy.starts_with("SWQ2"),"new queue format not versioned");legacy.replace(0,4,"SWQ1");
 for(size_t p=0;(p=legacy.find(" 0 3 \"rus+aze\"",p))!=std::string::npos;)legacy.replace(p,14," 0 \"rus+aze\"");
 save_local_state(dir/L"legacy.dat",legacy);auto old=load_queue(dir/L"legacy.dat");
 check(old->jobs[1].options.video_codec==VideoCodec::Default&&old->jobs[1].options.ocr_languages=="rus+aze","SWQ1 legacy defaults lost");std::filesystem::remove(dir/L"legacy.dat");
 JobQueue restored;restored.restore(*snapshot);
 check(restored.paused()&&!restored.next(),"recovered queue automatically starts");
 check(restored.items()[0].state==JobState::Interrupted,"active job replayed");
 check(restored.items()[1].options.pdf_dpi==300&&restored.items()[1].options.ocr_languages=="rus+aze","options lost");
 check(source_unchanged(restored.items()[1]),"source identity lost");
 {std::ofstream f(input,std::ios::app);f<<"changed";}
 check(!source_unchanged(restored.items()[1]),"changed source accepted");
 restored.set_paused(false);auto second=restored.next()->id;restored.start(second);restored.complete(second,JobState::Failed,{},L"error");
 auto outcomes=restored.take_batch_outcomes();check(outcomes.size()==1&&outcomes[0].failed==1&&outcomes[0].cancelled==1,"restored group accounting");
 save_queue(path,restored.snapshot());check(load_queue(path)->jobs.size()==2,"history missing");
 JobQueue history_only;history_only.restore(restored.snapshot());check(!history_only.paused(),"history-only restart silently pauses fresh admissions");
 auto unavailable=restored.snapshot();unavailable.jobs[0].input=L"Z:\\disconnected\\input.png";
 save_queue(dir/L"unavailable.dat",unavailable);check(load_queue(dir/L"unavailable.dat")->jobs.size()==2,"unavailable historical path destroyed store");std::filesystem::remove(dir/L"unavailable.dat");
 auto bad=restored.snapshot();bad.jobs.push_back(bad.jobs[0]);reject([&]{save_queue(path,bad);});check(load_queue(path)->jobs.size()==2,"failed save replaced old state");
 auto invalid_codec=restored.snapshot();invalid_codec.jobs[0].options.video_codec=static_cast<VideoCodec>(99);
 reject([&]{save_queue(path,invalid_codec);});check(load_queue(path)->jobs[0].options.video_codec==VideoCodec::Av1,"invalid codec replaced good snapshot");
 auto group_bad=restored.snapshot();group_bad.completed_batches[0].last_output=L"\\\\server\\share\\result.jpg";reject([&]{save_queue(path,group_bad);});
 auto lock=CreateFileW(path.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);check(lock!=INVALID_HANDLE_VALUE,"fixture lock");
 reject([&]{save_queue(path,restored.snapshot());});CloseHandle(lock);check(load_queue(path)->jobs.size()==2,"locked save destroyed previous state");
 JobQueue full;full.enqueue(std::vector<std::filesystem::path>(101,input),Format::Jpeg,{},{});
 for(int i=0;i<100;++i){auto job=full.next()->id;full.start(job);full.complete(job,JobState::Succeeded,dir/L"out.jpg",L"ok");}
 full.start(full.next()->id);JobQueue recovered_full;recovered_full.restore(full.snapshot());save_queue(dir/L"full.dat",recovered_full.snapshot());
 check(recovered_full.items().size()==100,"interrupted job broke history bound");std::filesystem::remove(dir/L"full.dat");
 auto rev=restored.revision();restored.clear_history();check(restored.items().empty()&&restored.revision()>rev,"clear history");check(std::filesystem::exists(input),"clear history deleted source");
 JobQueue empty;empty.restore(restored.snapshot());check(!empty.paused(),"empty store pauses new drops");
 {std::ofstream f(path,std::ios::trunc);f<<"SWQ1\ncorrupt";}reject([&]{load_queue(path);});
 {std::ofstream f(path,std::ios::trunc);f<<std::string(4*1024*1024+1,'x');}reject([&]{load_queue(path);});
 check(!load_queue(dir/L"absent").has_value(),"missing state is error");
 std::filesystem::remove(input);std::filesystem::remove(path);std::filesystem::remove(dir);
 std::cout<<"PASS durable queue, interrupted/recovery, immutable options, identity, corruption and history\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
