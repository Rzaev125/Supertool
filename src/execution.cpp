#include "execution.h"
#include <algorithm>
namespace shiftwheel {
bool valid_execution_facts(const ExecutionFacts&f){
 static const std::vector<std::string> stages{"wic","libavif","libheif","libwebp","ffmpeg","libreoffice","poppler","pdfium","tesseract"};
 static const std::vector<std::string> encoders{"","libx264","libx265","libaom-av1","libvpx-vp9","h264_nvenc","hevc_nvenc","av1_nvenc","h264_qsv","hevc_qsv","av1_qsv","h264_amf","hevc_amf","av1_amf"};
 if(f.stages.size()>16||std::find(encoders.begin(),encoders.end(),f.encoder)==encoders.end())return false;
 for(auto&s:f.stages)if(std::find(stages.begin(),stages.end(),s)==stages.end())return false;
 if(f.backend!=""&&f.backend!="cpu"&&f.backend!="nvidia"&&f.backend!="intel"&&f.backend!="amd")return false;
 if(f.encoder.empty())return f.backend.empty()&&!f.fallback;
 auto b=f.encoder.ends_with("_nvenc")?"nvidia":f.encoder.ends_with("_qsv")?"intel":f.encoder.ends_with("_amf")?"amd":"cpu";
 return f.backend==b;
}
std::wstring execution_detail(const ExecutionFacts*f,uint64_t input,bool known){
 auto wide=[](const std::string&s){return std::wstring(s.begin(),s.end());};
 std::wstring t=L"\r\nИсходный размер: "+(known?std::to_wstring(input)+L" байт":L"нет данных");
 t+=L"\r\nВремя выполнения: "+(f&&f->elapsed_ms?std::to_wstring(*f->elapsed_ms)+L" мс":L"нет данных / не запускалась");
 t+=L"\r\nРазмер результата: "+(f&&f->output_bytes?std::to_wstring(*f->output_bytes)+L" байт":L"нет данных");
 t+=L"\r\nДвижок: ";if(!f||f->stages.empty())t+=L"нет данных";else for(size_t i=0;i<f->stages.size();++i){if(i)t+=L" → ";t+=wide(f->stages[i]);}
 if(f&&!f->encoder.empty())t+=L"\r\nКодировщик: "+wide(f->encoder)+L" ("+wide(f->backend)+L")"+(f->fallback?L"; выполнен fallback":L"");return t;
}
}
