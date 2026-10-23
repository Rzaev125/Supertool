#pragma once
#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace shiftwheel {
enum class Format { Jpeg, Png, Bmp, Mp4, Mkv, Webm, Mp3, Wav, Flac, M4a, Ogg, Pdf, Docx, Odt, Rtf, Txt, Xlsx, Ods, Csv, Avif, Webp, Tiff, Opus, Aac, Mov, Heic, Pptx, Odp, Gif };
enum class Category { Unsupported, Image, Video, Audio, Document, Spreadsheet, Pdf, Presentation };
Category category(const std::filesystem::path& path);
std::vector<Format> formats_for(const std::filesystem::path& path);
// Filters preferences without changing them. The callback reads cached module status.
std::vector<Format> available_formats(const std::filesystem::path& input, std::vector<Format> candidates,
                                    const std::function<bool(std::string_view)>& module_available);
bool supports_conversion(const std::filesystem::path& path, Format format);
bool folder_output(const std::filesystem::path& path, Format format);
Format parse_format(std::wstring_view id);
const wchar_t* label(Format format);
const wchar_t* caption(Format format,bool first_frame=false);
int sector_at(float x, float y, float inner, float outer, int count);
bool can_accept(bool shift, bool copy_allowed, std::size_t files, bool supported, int sector, bool busy, int sector_count = 3);
bool supported_input(const std::filesystem::path& path);
std::wstring quote_argument(std::wstring_view argument);
std::filesystem::path output_candidate(const std::filesystem::path& input, Format format, unsigned suffix = 0);
const wchar_t* extension(Format format);

class Gesture {
public:
    void begin(int x, int y, bool eligible);
    bool move(int x, int y, bool shift, int threshold_x, int threshold_y);
    void shift_released();
    void end();
    bool active() const { return down_; }
private:
    int start_x_ = 0, start_y_ = 0;
    bool down_ = false, fired_ = false;
};
}
