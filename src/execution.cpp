#include "execution.h"
#include "progress.h"
#include <windows.h>
#include <algorithm>
#include <charconv>
#include <limits>
namespace shiftwheel {
bool valid_execution_facts(const ExecutionFacts&f){
 static const std::vector<std::string> stages{"wic","libavif","libheif","libwebp","ffmpeg","libreoffice","poppler","pdfium","tesseract"};
 static const std::vector<std::string> encoders{"","libx264","libx265","libaom-av1","libvpx-vp9","libmp3lame","pcm_s16le","flac","aac","libvorbis","libopus","h264_nvenc","hevc_nvenc","av1_nvenc","h264_qsv","hevc_qsv","av1_qsv","h264_amf","hevc_amf","av1_amf"};
 if(f.stages.size()>16||std::find(encoders.begin(),encoders.end(),f.encoder)==encoders.end())return false;
 for(auto&s:f.stages)if(std::find(stages.begin(),stages.end(),s)==stages.end())return false;
 if(f.backend!=""&&f.backend!="cpu"&&f.backend!="nvidia"&&f.backend!="intel"&&f.backend!="amd")return false;
 if(f.encoder.empty())return f.backend.empty()&&!f.fallback;
 auto b=f.encoder.ends_with("_nvenc")?"nvidia":f.encoder.ends_with("_qsv")?"intel":f.encoder.ends_with("_amf")?"amd":"cpu";
 return f.backend==b;
}
bool parse_execution(std::string_view line,ExecutionFacts&facts){
 if(line.size()>progress_line_limit||!line.starts_with("SWE1\t"))return false;
 std::vector<std::string_view> fields;size_t start=5;for(;;){auto end=line.find('\t',start);fields.push_back(line.substr(start,end==std::string_view::npos?end:end-start));if(end==std::string_view::npos)break;start=end+1;}
 auto f=facts;
 if(fields.size()==2&&fields[0]=="stage"){if(f.stages.empty()||f.stages.back()!=fields[1])f.stages.emplace_back(fields[1]);}
 else if(fields.size()==4&&fields[0]=="encoder"){f.encoder=fields[1];f.backend=fields[2];if(fields[3]!="0"&&fields[3]!="1")return false;f.fallback=fields[3]=="1";}
 else if(fields.size()==2&&fields[0]=="bytes"){uint64_t n=0;auto v=fields[1];auto p=std::from_chars(v.data(),v.data()+v.size(),n);if(v.empty()||p.ec!=std::errc{}||p.ptr!=v.data()+v.size())return false;f.output_bytes=n;}
 else return false;
 if(!valid_execution_facts(f))return false;facts=std::move(f);return true;
}
void report_engine(std::string_view stage) noexcept{try{ExecutionFacts f;if(parse_execution("SWE1\tstage\t"+std::string(stage),f))write_worker_event("SWE1\tstage\t"+std::string(stage)+"\n");}catch(...){}}
void report_encoder(std::string_view encoder,std::string_view backend,bool fallback) noexcept{try{auto event="SWE1\tencoder\t"+std::string(encoder)+"\t"+std::string(backend)+"\t"+(fallback?"1":"0");ExecutionFacts f;if(parse_execution(event,f))write_worker_event(event+"\n");}catch(...){}}
void report_output_size(const std::filesystem::path&root) noexcept{try{
 auto attrs=GetFileAttributesW(root.c_str());if(attrs==INVALID_FILE_ATTRIBUTES||(attrs&FILE_ATTRIBUTE_REPARSE_POINT))return;uint64_t total=0;
 if(!(attrs&FILE_ATTRIBUTE_DIRECTORY))total=std::filesystem::file_size(root);
 else{size_t count=0;for(auto&item:std::filesystem::directory_iterator(root)){auto a=GetFileAttributesW(item.path().c_str());if(++count>10000||a==INVALID_FILE_ATTRIBUTES||(a&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY)))return;auto n=item.file_size();if(n>std::numeric_limits<uint64_t>::max()-total)return;total+=n;}}
 write_worker_event("SWE1\tbytes\t"+std::to_string(total)+"\n");
}catch(...){}}
std::wstring execution_detail(const ExecutionFacts*f,uint64_t input,bool known){
 auto wide=[](const std::string&s){return std::wstring(s.begin(),s.end());};
 std::wstring t=L"\r\nИсходный размер: "+(known?std::to_wstring(input)+L" байт":L"нет данных");
 t+=L"\r\nВремя выполнения: "+(f&&f->elapsed_ms?std::to_wstring(*f->elapsed_ms)+L" мс":L"нет данных / не запускалась");
 t+=L"\r\nРазмер результата: "+(f&&f->output_bytes?std::to_wstring(*f->output_bytes)+L" байт":L"нет данных");
 t+=L"\r\nДвижок: ";if(!f||f->stages.empty())t+=L"нет данных";else for(size_t i=0;i<f->stages.size();++i){if(i)t+=L" → ";t+=wide(f->stages[i]);}
 if(f&&!f->encoder.empty())t+=L"\r\nКодировщик: "+wide(f->encoder)+L" ("+wide(f->backend)+L")"+(f->fallback?L"; выполнен fallback":L"");return t;
}
}
