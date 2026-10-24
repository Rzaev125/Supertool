#include "profiles.h"
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <set>
#include <stdexcept>
namespace shiftwheel {
namespace {
[[noreturn]] void invalid(){throw std::runtime_error("Некорректный профиль: проверьте имя, формат и параметры.");}
const ConversionProfile* active(Category c,const Settings&s){auto a=s.active_profiles.find(c);if(a==s.active_profiles.end())return nullptr;auto p=std::find_if(s.profiles.begin(),s.profiles.end(),[&](auto&v){return v.id==a->second&&v.category==c;});return p==s.profiles.end()?nullptr:&*p;}
}
const wchar_t* category_sample(Category c){switch(c){case Category::Image:return L"x.png";case Category::Video:return L"x.mp4";case Category::Audio:return L"x.wav";case Category::Document:return L"x.docx";case Category::Spreadsheet:return L"x.xlsx";case Category::Pdf:return L"x.pdf";case Category::Presentation:return L"x.pptx";default:invalid();}}
const char* profile_category_key(Category c){switch(c){case Category::Image:return "image";case Category::Video:return "video";case Category::Audio:return "audio";case Category::Document:return "document";case Category::Spreadsheet:return "spreadsheet";case Category::Pdf:return "pdf";case Category::Presentation:return "presentation";default:invalid();}}
Category parse_profile_category(std::string_view key){for(auto c:{Category::Image,Category::Video,Category::Audio,Category::Document,Category::Spreadsheet,Category::Pdf,Category::Presentation})if(key==profile_category_key(c))return c;invalid();}
bool valid_profile_id(std::string_view s){return s.size()==32&&std::all_of(s.begin(),s.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});}
bool profile_extracts_audio(const ConversionProfile&p){return p.category==Category::Video&&(p.format==Format::Mp3||p.format==Format::Wav);}
bool valid_profile_name(std::wstring_view s){if(s.empty()||s.size()>64||s.front()==L' '||s.back()==L' ')return false;for(size_t i=0;i<s.size();++i){auto c=s[i];if(c<32||(c>=127&&c<=159))return false;if(c>=0xd800&&c<=0xdbff){if(++i==s.size()||s[i]<0xdc00||s[i]>0xdfff)return false;}else if(c>=0xdc00&&c<=0xdfff)return false;}return true;}
void validate_profiles(const Settings&s){
 if(s.profiles.size()>32)invalid();std::set<std::string> ids;
 for(size_t i=0;i<s.profiles.size();++i){auto&p=s.profiles[i];if(!valid_profile_id(p.id)||!ids.insert(p.id).second||!valid_profile_name(p.name)||!supports_conversion(category_sample(p.category),p.format))invalid();validate_options(p.options);
  if(p.category==Category::Video&&((p.format==Format::Webm&&p.options.video_codec!=VideoCodec::Default&&p.options.video_codec!=VideoCodec::Av1)||(p.format==Format::Mov&&p.options.video_codec==VideoCodec::Av1)))invalid();
  for(size_t j=0;j<i;++j)if(s.profiles[j].category==p.category&&CompareStringOrdinal(p.name.c_str(),-1,s.profiles[j].name.c_str(),-1,TRUE)==CSTR_EQUAL)invalid();
 }
 for(auto&[c,id]:s.active_profiles){category_sample(c);auto*p=active(c,s);if(!p||id.empty())invalid();}
}
ResolvedProfile resolve_profile(const std::filesystem::path&input,Format target,const Settings&s){
 ResolvedProfile r{s.conversion,{},{}};auto c=category(input);auto*p=active(c,s);if(!p||p->format!=target)return r;
 const auto configured=configured_formats(input,s);if(std::find(configured.begin(),configured.end(),target)==configured.end())return r;
 const auto&o=p->options;auto&a=r.options;
 if(c==Category::Image){a.quality=o.quality;a.resources=o.resources;a.image_max_dimension=o.image_max_dimension;a.image_metadata=o.image_metadata;}
 else if(c==Category::Video){a.quality=o.quality;a.resources=o.resources;if(profile_extracts_audio(*p))a.audio_bitrate_kbps=o.audio_bitrate_kbps;else{a.video_max_height=o.video_max_height;a.video_codec=o.video_codec;a.video_acceleration=o.video_acceleration;}}
 else if(c==Category::Audio){a.quality=o.quality;a.resources=o.resources;a.audio_bitrate_kbps=o.audio_bitrate_kbps;}
 else if(c==Category::Pdf){a.pdf_dpi=o.pdf_dpi;a.pdf_ocr=o.pdf_ocr;a.ocr_languages=o.ocr_languages;}
 r.profile_id=p->id;r.profile_name=p->name;return r;
}
std::vector<Format> profile_formats(const std::filesystem::path&input,const Settings&s){auto result=configured_formats(input,s);if(auto*p=active(category(input),s)){auto it=std::find(result.begin(),result.end(),p->format);if(it!=result.end())std::rotate(result.begin(),it,it+1);}return result;}
ConversionProfile make_profile(Category c,const ConversionOptions&o,int index){
 ConversionProfile p;p.category=c;p.options=o;p.name=L"Новый профиль";p.format=formats_for(category_sample(c)).front();
 unsigned char bytes[16];if(BCryptGenRandom(nullptr,bytes,sizeof(bytes),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)throw std::runtime_error("Не удалось создать ID профиля.");for(auto b:bytes){p.id+="0123456789abcdef"[b>>4];p.id+="0123456789abcdef"[b&15];}
 if(index==0){p.category=Category::Image;p.name=L"Фото для отправки";p.format=Format::Jpeg;p.options.quality=QualityPreset::Balanced;p.options.resources=ResourceMode::Balanced;p.options.image_max_dimension=1920;p.options.image_metadata=ImageMetadata::Remove;}
 if(index==1){p.category=Category::Video;p.name=L"Видео 1080p";p.format=Format::Mp4;p.options.quality=QualityPreset::Balanced;p.options.resources=ResourceMode::Balanced;p.options.video_max_height=1080;p.options.video_codec=VideoCodec::H264;p.options.video_acceleration=VideoAcceleration::Auto;}
 if(index==2){p.category=Category::Audio;p.name=L"Аудио для телефона";p.format=Format::Mp3;p.options.quality=QualityPreset::Balanced;p.options.resources=ResourceMode::Balanced;p.options.audio_bitrate_kbps=192;}return p;
}
std::wstring profile_consequences(const ConversionProfile&p){auto&o=p.options;std::wstring t=L"Параметры действуют только при выборе "+std::wstring(label(p.format))+L". Точный размер файла заранее неизвестен.\r\n";
 if(p.category==Category::Image){t+=o.image_max_dimension?L"Сторона до "+std::to_wstring(o.image_max_dimension)+L" px; без увеличения, пропорции сохраняются. ":L"Исходные размеры. ";t+=(p.format==Format::Jpeg||p.format==Format::Bmp)?L"Прозрачность заменяется белым фоном. ":p.format==Format::Gif?L"Сохранение прозрачности не обещается. ":L"Поддерживаемая прозрачность сохраняется. ";t+=(p.format==Format::Gif||p.format==Format::Webp)?L"Анимация сохраняется для поддерживаемых GIF/WebP; ограничения палитры GIF остаются. ":L"Анимация: первый кадр. ";t+=(p.format!=Format::Jpeg&&p.format!=Format::Tiff)||o.image_metadata==ImageMetadata::Remove?L"Метаданные удаляются. ":o.image_metadata==ImageMetadata::CaptureDate?L"Только поддерживаемая дата JPEG/TIFF, без GPS. ":L"Поддерживаемые дата, автор, copyright JPEG/TIFF/HEIC, без GPS. ";t+=L"Поддерживаемые HDR AVIF/HEIC → SDR; не любой HDR.";}
 else if(profile_extracts_audio(p)){t+=L"Извлечение аудио: видеодорожка удаляется, размеры видео и GPU-кодирование не применяются. ";t+=p.format==Format::Wav?L"WAV: без потерь кодирования; битрейт не задаётся.":L"MP3: с потерями, битрейт "+(o.audio_bitrate_kbps?std::to_wstring(o.audio_bitrate_kbps)+L" кбит/с.":L"по качеству.");}
 else if(p.category==Category::Video){t+=o.video_max_height?L"Высота до "+std::to_wstring(o.video_max_height)+L" px; без увеличения. ":L"Исходная высота. ";t+=L"Перекодирование с потерями. ";t+=o.video_acceleration==VideoAcceleration::Cpu?L"Только CPU.":L"Auto: доступный GPU, при ошибке CPU; GPU заранее не гарантирован.";}
 else if(p.category==Category::Audio){t+=(p.format==Format::Flac||p.format==Format::Wav)?L"Без потерь при кодировании; прежние потери исходника не восстанавливаются. Битрейт не задаётся.":L"Перекодирование с потерями. Битрейт: "+(o.audio_bitrate_kbps?std::to_wstring(o.audio_bitrate_kbps)+L" кбит/с.":L"по качеству.");}
 else if(p.category==Category::Pdf)t+=p.format==Format::Txt?L"TXT: извлечение текста; OCR при отсутствии текста, если включён. Языки задаются ниже; возможны ошибки. PDF с поиском не создаётся.":L"Растровый экспорт: "+std::to_wstring(o.pdf_dpi)+L" DPI. Вёрстка Office не восстанавливается.";
 else t+=L"Конвертация текущим Office-движком; внешний вид сложных документов может измениться.";return t;
}
}
