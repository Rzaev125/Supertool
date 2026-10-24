#include "job_store.h"
#include "file_policy.h"
#include "profiles.h"
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <limits>
namespace shiftwheel {
namespace fs=std::filesystem;
namespace {
[[noreturn]]void invalid(){throw std::runtime_error("Сохранённая очередь повреждена или недоступна. Исходные файлы не изменены.");}
struct Handle{HANDLE h=INVALID_HANDLE_VALUE;~Handle(){if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);}};
std::string utf8(const std::wstring& s){
 if(s.size()>32768)invalid();if(s.empty())return {};
 int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);if(!n)invalid();
 std::string v(n,'\0');WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),v.data(),n,nullptr,nullptr);return v;
}
std::wstring wide(const std::string& s){
 if(s.size()>131072||s.find('\0')!=std::string::npos)invalid();if(s.empty())return {};
 int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);if(!n)invalid();
 std::wstring v(n,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),v.data(),n);return v;
}
void check_id(uint64_t id){if(!id||id>=(1ULL<<60))invalid();}
// Stored history can refer to a removed drive or a no-longer-existing folder.
// Reject unsafe syntax here; check the live filesystem only when using a job.
void stored_path(const fs::path& path){
 auto drive=path.root_path().wstring();auto relative=path.relative_path().wstring();auto text=path.wstring();
 if(!path.is_absolute()||drive.size()!=3||drive[1]!=L':'||!((drive[0]>=L'A'&&drive[0]<=L'Z')||(drive[0]>=L'a'&&drive[0]<=L'z'))||relative.find(L':')!=std::wstring::npos||text.find(L'\0')!=std::wstring::npos)invalid();
 for(auto c:text)if(c<32)invalid();
}
void validate(const QueueSnapshot& s){
 if(s.jobs.size()>357||s.batches.size()>357||s.completed_batches.size()>100)invalid();
 std::set<uint64_t> ids,groups;size_t pending=0,active=0,finished=0;
 auto validate_batch=[&](const BatchOutcome& b){
  check_id(b.id);if(!groups.insert(b.id).second||!b.total||b.total>256||b.remaining>b.total||b.succeeded>b.total||b.failed>b.total||b.cancelled>b.total||b.remaining+b.succeeded+b.failed+b.cancelled!=b.total)invalid();
  if(!b.last_output.empty())stored_path(b.last_output);
 };
 for(const auto& b:s.batches)validate_batch(b);
 for(const auto& b:s.completed_batches){validate_batch(b);if(b.remaining)invalid();}
 for(const auto& j:s.jobs){
  check_id(j.id);check_id(j.batch_id);if(!ids.insert(j.id).second||j.state<JobState::Queued||j.state>JobState::Interrupted||!supports_conversion(j.input,j.format))invalid();
  validate_options(j.options);stored_path(j.input);
  if(j.profile_id.empty()!=j.profile_name.empty()||(!j.profile_id.empty()&&(!valid_profile_id(j.profile_id)||!valid_profile_name(j.profile_name))))invalid();
  if(j.facts&&(!valid_execution_facts(*j.facts)||(j.state==JobState::Queued)|| (j.state!=JobState::Succeeded&&j.facts->output_bytes)))invalid();
  if(!j.output_directory.empty())stored_path(j.output_directory);if(!j.output.empty())stored_path(j.output);
  if(j.state==JobState::Queued)++pending;else if(j.state==JobState::Running)++active;else ++finished;
 }
 if(pending>256||active>1||finished>100)invalid();
 for(const auto& b:s.batches){
  auto count=std::count_if(s.jobs.begin(),s.jobs.end(),[&](const auto& j){return j.batch_id==b.id&&(j.state==JobState::Queued||j.state==JobState::Running);});
  if(static_cast<size_t>(count)!=b.remaining)invalid();
 }
 for(const auto& j:s.jobs)if((j.state==JobState::Queued||j.state==JobState::Running)&&std::none_of(s.batches.begin(),s.batches.end(),[&](const auto& b){return b.id==j.batch_id;}))invalid();
}
void write_batch(std::ostream& o,const BatchOutcome& b){o<<b.id<<' '<<b.total<<' '<<b.remaining<<' '<<b.succeeded<<' '<<b.failed<<' '<<b.cancelled<<' '<<std::quoted(utf8(b.last_output.wstring()))<<' '<<std::quoted(utf8(b.last_message))<<'\n';}
BatchOutcome read_batch(std::istream& in){BatchOutcome b;std::string path,msg;if(!(in>>b.id>>b.total>>b.remaining>>b.succeeded>>b.failed>>b.cancelled>>std::quoted(path)>>std::quoted(msg)))invalid();b.last_output=wide(path);b.last_message=wide(msg);return b;}
}
void save_local_state(const fs::path& path,const std::string& text){
 if(text.size()>4*1024*1024)invalid();validate_local_path(path,false);
 const auto temp=fs::path(path.wstring()+L".tmp-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
 Handle file{CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr)};if(file.h==INVALID_HANDLE_VALUE)invalid();
 try{DWORD written=0;if(!WriteFile(file.h,text.data(),static_cast<DWORD>(text.size()),&written,nullptr)||written!=text.size()||!FlushFileBuffers(file.h))invalid();
  CloseHandle(file.h);file.h=INVALID_HANDLE_VALUE;validate_local_path(path,false);
  if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))invalid();
 }catch(...){if(file.h!=INVALID_HANDLE_VALUE){CloseHandle(file.h);file.h=INVALID_HANDLE_VALUE;}DeleteFileW(temp.c_str());throw;}
}
std::optional<std::string> load_local_state(const fs::path& path,size_t limit){
 validate_local_path(path,false);Handle file{CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr)};
 if(file.h==INVALID_HANDLE_VALUE){auto e=GetLastError();if(e==ERROR_FILE_NOT_FOUND||e==ERROR_PATH_NOT_FOUND)return {};invalid();}
 BY_HANDLE_FILE_INFORMATION info{};if(!GetFileInformationByHandle(file.h,&info)||info.nFileSizeHigh||info.nFileSizeLow>limit||(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)))invalid();
 std::string text(info.nFileSizeLow,'\0');DWORD got=0;if(!ReadFile(file.h,text.data(),static_cast<DWORD>(text.size()),&got,nullptr)||got!=text.size())invalid();return text;
}
void save_queue(const fs::path& path,const QueueSnapshot& s){
 validate(s);std::ostringstream o;o<<"SWQ4\n"<<s.jobs.size()<<' '<<s.batches.size()<<' '<<s.completed_batches.size()<<'\n';
 for(const auto& j:s.jobs){const auto& a=j.options;
  o<<j.id<<' '<<j.batch_id<<' '<<static_cast<int>(j.state)<<' '<<static_cast<int>(j.format)<<' '<<j.input_identity<<' '<<j.input_size<<' '<<j.input_modified<<' '
   <<static_cast<int>(a.quality)<<' '<<static_cast<int>(a.resources)<<' '<<a.image_max_dimension<<' '<<a.video_max_height<<' '<<a.audio_bitrate_kbps<<' '<<a.pdf_dpi<<' '<<a.pdf_ocr<<' '<<static_cast<int>(a.video_acceleration)<<' '<<static_cast<int>(a.video_codec)<<' '<<static_cast<int>(a.image_metadata)<<' '<<std::quoted(a.ocr_languages)<<' '
   <<std::quoted(utf8(j.input.wstring()))<<' '<<std::quoted(utf8(j.output_directory.wstring()))<<' '<<std::quoted(utf8(j.output.wstring()))<<' '<<std::quoted(utf8(j.message))<<' '
   <<std::quoted(j.profile_id)<<' '<<std::quoted(utf8(j.profile_name))<<' '<<j.facts.has_value();
  if(j.facts){auto&f=*j.facts;o<<' '<<f.elapsed_ms.has_value()<<' '<<f.elapsed_ms.value_or(0)<<' '<<f.output_bytes.has_value()<<' '<<f.output_bytes.value_or(0)<<' '<<std::quoted(f.encoder)<<' '<<std::quoted(f.backend)<<' '<<f.fallback<<' '<<f.stages.size();for(auto&v:f.stages)o<<' '<<std::quoted(v);}o<<'\n';
 }
 for(const auto& b:s.batches)write_batch(o,b);for(const auto& b:s.completed_batches)write_batch(o,b);
 save_local_state(path,o.str());
}
std::optional<QueueSnapshot> load_queue(const fs::path& path){
 auto text=load_local_state(path);if(!text)return {};std::istringstream in(*text);std::string magic;size_t jobs=0,batches=0,completed=0;
 if(!(in>>magic>>jobs>>batches>>completed)||(magic!="SWQ1"&&magic!="SWQ2"&&magic!="SWQ3"&&magic!="SWQ4")||jobs>357||batches>357||completed>100)invalid();QueueSnapshot s;
 for(size_t i=0;i<jobs;++i){ConversionJob j;auto& a=j.options;int state=0,format=0,q=0,r=0,gpu=0,codec=0,metadata=0;std::string input,dir,output,msg;
  if(!(in>>j.id>>j.batch_id>>state>>format>>j.input_identity>>j.input_size>>j.input_modified>>q>>r>>a.image_max_dimension>>a.video_max_height>>a.audio_bitrate_kbps>>a.pdf_dpi>>a.pdf_ocr>>gpu))invalid();
  if(magic!="SWQ1"&&!(in>>codec))invalid();
  if((magic=="SWQ3"||magic=="SWQ4")&&!(in>>metadata))invalid();
  if(!(in>>std::quoted(a.ocr_languages)>>std::quoted(input)>>std::quoted(dir)>>std::quoted(output)>>std::quoted(msg)))invalid();
  j.state=static_cast<JobState>(state);j.format=static_cast<Format>(format);a.quality=static_cast<QualityPreset>(q);a.resources=static_cast<ResourceMode>(r);a.video_acceleration=static_cast<VideoAcceleration>(gpu);a.video_codec=static_cast<VideoCodec>(codec);a.image_metadata=static_cast<ImageMetadata>(metadata);
  j.input=wide(input);j.output_directory=wide(dir);j.output=wide(output);j.message=wide(msg);
  if(magic=="SWQ4"){bool has=false;std::string name;if(!(in>>std::quoted(j.profile_id)>>std::quoted(name)>>has))invalid();j.profile_name=wide(name);if(has){ExecutionFacts f;bool elapsed=false,bytes=false;uint64_t ms=0,size=0;size_t n=0;if(!(in>>elapsed>>ms>>bytes>>size>>std::quoted(f.encoder)>>std::quoted(f.backend)>>f.fallback>>n)||n>16)invalid();if(elapsed)f.elapsed_ms=ms;if(bytes)f.output_bytes=size;for(size_t k=0;k<n;++k){std::string v;if(!(in>>std::quoted(v)))invalid();f.stages.push_back(v);}j.facts=std::move(f);}}
  s.jobs.push_back(std::move(j));
 }
 for(size_t i=0;i<batches;++i)s.batches.push_back(read_batch(in));for(size_t i=0;i<completed;++i)s.completed_batches.push_back(read_batch(in));
 in>>std::ws;if(!in.eof())invalid();validate(s);return s;
}
void JobQueue::capture_inputs(){for(auto& j:jobs_)if(j.state==JobState::Queued&&!j.input_identity){std::error_code e;auto size=fs::file_size(j.input,e);if(e)continue;auto t=fs::last_write_time(j.input,e);if(e)continue;j.input_size=size;j.input_modified=t.time_since_epoch().count();j.input_identity=true;++revision_;}}
bool source_unchanged(const ConversionJob& j)noexcept{try{if(!j.input_identity||!supported_local_file(j.input))return false;return fs::file_size(j.input)==j.input_size&&fs::last_write_time(j.input).time_since_epoch().count()==j.input_modified;}catch(...){return false;}}
bool JobQueue::restore(const QueueSnapshot& s){
 validate(s);JobQueue next;next.jobs_=s.jobs;next.completed_batches_=s.completed_batches;
 bool changed=false;
 for(const auto& b:s.batches){next.batches_.emplace(b.id,b);next.next_batch_=std::max(next.next_batch_,b.id+1);}
 for(const auto& b:s.completed_batches)next.next_batch_=std::max(next.next_batch_,b.id+1);
 for(auto& j:next.jobs_){next.next_id_=std::max(next.next_id_,j.id+1);next.next_batch_=std::max(next.next_batch_,j.batch_id+1);
  if(j.state==JobState::Running){changed=true;next.record(j,JobState::Cancelled,{},L"Прервано при закрытии. Повторите вручную.");j.state=JobState::Interrupted;j.message=L"Прервано при закрытии. Повторите вручную.";}
 }
 auto terminal=next.jobs_.size()-next.pending();
 while(terminal>100){auto it=std::find_if(next.jobs_.begin(),next.jobs_.end(),[](const auto& j){return j.state!=JobState::Queued;});next.jobs_.erase(it);--terminal;}
 next.paused_=next.pending()!=0;next.revision_=revision_+1;*this=std::move(next);return changed;
}
}
