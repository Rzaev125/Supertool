#pragma once
#include "policy.h"
#include <map>

namespace shiftwheel {
enum class QualityPreset { Compact, Balanced, High };
enum class ResourceMode { Economy, Balanced, Fast };
enum class VideoAcceleration { Auto, Cpu };
enum class VideoCodec { Default, H264, Hevc, Av1 };
enum class ImageMetadata { Remove, CaptureDate, PhotoDetails };
struct ConversionOptions {
    QualityPreset quality = QualityPreset::Balanced;
    ResourceMode resources = ResourceMode::Balanced;
    unsigned image_max_dimension = 0, video_max_height = 0, audio_bitrate_kbps = 0, pdf_dpi = 144;
    bool pdf_ocr = true;
    std::string ocr_languages = "eng+rus+aze";
    VideoAcceleration video_acceleration = VideoAcceleration::Auto;
    VideoCodec video_codec = VideoCodec::Default;
    ImageMetadata image_metadata = ImageMetadata::Remove;
};
enum class NotificationMode { All, Errors, None };
struct Settings {
    ConversionOptions conversion;
    std::filesystem::path output_directory;
    std::map<Category, std::vector<Format>> wheels;
    bool auto_show_progress = false;
    NotificationMode notifications = NotificationMode::All;
    bool start_with_windows = false, automatic_updates = true;
    std::wstring update_channel = L"https://raw.githubusercontent.com/Rzaev125/Supertool/main/updates/stable.txt";
};
Settings load_settings(const std::filesystem::path& path);
void save_settings(const std::filesystem::path& path, const Settings& settings);
ConversionOptions load_options(const std::filesystem::path& path);
// Exclusively creates a worker-owned snapshot; an existing path is never replaced.
void save_options(const std::filesystem::path& path, const ConversionOptions& options);
void validate_options(const ConversionOptions& options);
std::vector<Format> configured_formats(const std::filesystem::path& input, const Settings& settings);
unsigned resource_threads(ResourceMode mode);
}
