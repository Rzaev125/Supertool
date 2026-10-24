#include "media_converter.h"
#include "engines.h"
#include "execution.h"
#include "file_policy.h"
#include "progress.h"
#include <charconv>
#include <cmath>
#include <fstream>
#include <stdexcept>

namespace shiftwheel {
namespace fs = std::filesystem;
namespace {
enum class VideoEncoder { Cpu, Nvidia, Intel, Amd };
bool video_output(Format format) { return format == Format::Mp4 || format == Format::Mkv || format == Format::Mov || format == Format::Webm; }
VideoCodec requested_codec(Format format, const ConversionOptions& options) {
    if (!video_output(format)) return VideoCodec::Default;
    const auto codec=options.video_codec;
    if (codec==VideoCodec::Default) return format==Format::Webm ? VideoCodec::Default : VideoCodec::H264;
    if ((format==Format::Webm && codec!=VideoCodec::Av1) || (format==Format::Mov && codec==VideoCodec::Av1))
        throw std::runtime_error("Кодек несовместим с контейнером. Для H.264/HEVC выберите MP4, MKV или MOV; для AV1 — MP4, MKV или WebM.");
    return codec;
}
std::wstring encoder_name(VideoEncoder encoder, VideoCodec codec) {
    const std::wstring prefix=codec==VideoCodec::Hevc ? L"hevc" : codec==VideoCodec::Av1 ? L"av1" : L"h264";
    switch(encoder) {
    case VideoEncoder::Nvidia: return prefix+L"_nvenc";
    case VideoEncoder::Intel: return prefix+L"_qsv";
    case VideoEncoder::Amd: return prefix+L"_amf";
    default: return codec==VideoCodec::Hevc ? L"libx265" : codec==VideoCodec::Av1 ? L"libaom-av1" : L"libx264";
    }
}
std::wstring encoder_label(VideoEncoder encoder) {
    switch (encoder) {
    case VideoEncoder::Nvidia: return L"NVIDIA NVENC";
    case VideoEncoder::Intel: return L"Intel QSV";
    case VideoEncoder::Amd: return L"AMD AMF";
    default: return L"CPU";
    }
}
std::vector<std::wstring> codec_arguments(VideoEncoder encoder, VideoCodec codec, const ConversionOptions& options) {
    const auto quality=options.quality;
    const bool compact = quality == QualityPreset::Compact, high = quality == QualityPreset::High;
    const std::wstring quantizer = codec==VideoCodec::Av1 ? (compact ? L"42" : high ? L"24" : L"32") : codec==VideoCodec::Hevc ? (compact ? L"30" : high ? L"22" : L"26") : (compact ? L"28" : high ? L"18" : L"23");
    std::vector<std::wstring> args{L"-c:v", encoder_name(encoder,codec)};
    auto add = [&](std::initializer_list<std::wstring> values) { args.insert(args.end(), values); };
    // These quality controls are encoder-specific. Their numeric values are not
    // a promise of identical perceptual quality or size across different GPUs.
    switch (encoder) {
    case VideoEncoder::Nvidia:
        add({L"-preset", compact ? L"p3" : high ? L"p5" : L"p4", L"-rc", L"vbr", L"-cq", quantizer, L"-b:v", L"0"}); break;
    case VideoEncoder::Intel:
        add({L"-preset", compact ? L"fast" : high ? L"slow" : L"medium", L"-global_quality", quantizer}); break;
    case VideoEncoder::Amd:
        add({L"-quality", compact ? L"speed" : high ? L"quality" : L"balanced", L"-rc", L"cqp",
            L"-qp_i", quantizer, L"-qp_p", quantizer});
        if(codec==VideoCodec::H264) add({L"-qp_b",quantizer});
        break;
    default:
        if(codec==VideoCodec::Av1) add({L"-crf",quantizer,L"-b:v",L"0",L"-cpu-used",compact?L"8":high?L"4":L"6",L"-row-mt",L"1"});
        else {
            add({L"-preset",L"fast",L"-crf",quantizer});
            if(codec==VideoCodec::Hevc) add({L"-x265-params",L"pools="+std::to_wstring(resource_threads(options.resources))+L":frame-threads=1:log-level=error"});
        }
        break;
    }
    return args;
}
VideoEncoder select_encoder(const fs::path& executable, VideoCodec codec, const fs::path& work,
                           const ConversionOptions& options, const MediaRunner& runner) {
    if (codec == VideoCodec::Default || options.video_acceleration == VideoAcceleration::Cpu) return VideoEncoder::Cpu;
    for (auto encoder : {VideoEncoder::Nvidia, VideoEncoder::Intel, VideoEncoder::Amd}) {
        report_progress(-1, L"Проверка " + encoder_label(encoder));
        // 256x256 avoids the minimum-dimension restriction of NVENC on Turing.
        // Three real frames exercise device creation, encode and flush, not just
        // an encoder's presence in this FFmpeg build. No result file is needed.
        std::vector<std::wstring> args{L"-hide_banner", L"-nostdin", L"-loglevel", L"error", L"-xerror",
            L"-f", L"lavfi", L"-i", L"color=c=black:s=256x256:r=30:d=0.1", L"-frames:v", L"3", L"-an"};
        const auto codec_args = codec_arguments(encoder, codec, options); args.insert(args.end(), codec_args.begin(), codec_args.end());
        args.insert(args.end(), {L"-pix_fmt", encoder == VideoEncoder::Intel ? L"nv12" : L"yuv420p", L"-threads",
            std::to_wstring(resource_threads(options.resources)), L"-f", L"null", L"-"});
        try {
            runner({executable, args, work / (L"probe-" + encoder_name(encoder,codec) + L".log"), 5000});
            return encoder;
        } catch (const EngineCancelled&) { throw; }
        catch (const std::runtime_error&) { /* Unavailable, busy, incompatible driver or bounded probe timeout. */ }
    }
    return VideoEncoder::Cpu;
}
double media_duration(const fs::path& input, const fs::path& work, const MediaRunner& runner) {
    const auto probe = module_path(category(input)).parent_path() / L"ffprobe.exe";
    if (!fs::is_regular_file(probe)) return 0;
    report_progress(-1, L"Анализ длительности");
    try {
        const auto log = work / L"duration.log";
        runner({probe, {L"-v", L"error", L"-protocol_whitelist", L"file,pipe", L"-show_entries", L"format=duration",
            L"-of", L"default=noprint_wrappers=1:nokey=1", input.wstring()}, log, 30000});
        std::ifstream stream(log, std::ios::binary);
        char line[128]{}; stream.getline(line, sizeof(line));
        if (!stream && !stream.eof()) return 0;
        std::string_view value(line); if (value.ends_with('\r')) value.remove_suffix(1);
        double duration = 0;
        const auto parsed = std::from_chars(value.data(), value.data() + value.size(), duration);
        return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size() && std::isfinite(duration) && duration > 0 ? duration : 0;
    } catch (const EngineCancelled&) { throw; }
    catch (const std::runtime_error&) { return 0; } // Duration is optional; decoding remains authoritative.
}
std::vector<std::wstring> media_arguments(const fs::path& input, const fs::path& output, Format format,
                                         const ConversionOptions& options, VideoEncoder encoder, VideoCodec codec, double duration) {
    const auto threads = std::to_wstring(resource_threads(options.resources));
    const bool compact = options.quality == QualityPreset::Compact, high = options.quality == QualityPreset::High;
    auto bitrate = [&](unsigned normal, unsigned compact_rate, unsigned best) {
        return std::to_wstring(options.audio_bitrate_kbps ? options.audio_bitrate_kbps : compact ? compact_rate : high ? best : normal) + L"k";
    };
    std::vector<std::wstring> args{L"-hide_banner", L"-nostdin", L"-loglevel", L"error", L"-xerror", L"-max_error_rate", L"0", L"-n", L"-threads", threads, L"-err_detect", L"explode",
        L"-filter_threads", threads, L"-filter_complex_threads", threads, L"-protocol_whitelist", L"file,pipe", L"-i", input.wstring()};
    auto add = [&](std::initializer_list<std::wstring> values) { args.insert(args.end(), values); };
    if (format == Format::Mp4 || format == Format::Mkv || format == Format::Webm || format == Format::Mov) {
        std::wstring filter = L"pad=ceil(iw/2)*2:ceil(ih/2)*2";
        if (options.video_max_height)
            filter = L"scale=-2:trunc(min(ih\\," + std::to_wstring(options.video_max_height) + L")/2)*2," + filter;
        add({L"-map", L"0:v:0", L"-map", L"0:a:0?", L"-sn", L"-dn", L"-vf", filter, L"-pix_fmt", encoder == VideoEncoder::Intel ? L"nv12" : L"yuv420p"});
        if (format == Format::Webm && codec==VideoCodec::Default) add({L"-c:v", L"libvpx-vp9", L"-crf", compact ? L"40" : high ? L"24" : L"32", L"-b:v", L"0", L"-deadline", L"good", L"-cpu-used", L"4", L"-c:a", L"libopus", L"-b:a", bitrate(128, 96, 192)});
        else {
            const auto codec_args = codec_arguments(encoder, codec, options); args.insert(args.end(), codec_args.begin(), codec_args.end());
            if(format==Format::Webm) add({L"-c:a",L"libopus",L"-b:a",bitrate(128,96,192)});
            else add({L"-c:a", L"aac", L"-b:a", bitrate(192, 128, 256)});
            if(codec==VideoCodec::Hevc&&(format==Format::Mp4||format==Format::Mov))add({L"-tag:v",L"hvc1"});
            if(codec==VideoCodec::Av1&&format==Format::Mp4)add({L"-tag:v",L"av01"});
        }
        if (format == Format::Mp4 || format == Format::Mov) add({L"-movflags", L"+faststart"});
    } else {
        add({L"-map", L"0:a:0", L"-vn", L"-sn", L"-dn"});
        switch (format) {
        case Format::Mp3: add({L"-c:a", L"libmp3lame", L"-b:a", bitrate(192, 128, 320), L"-ac", L"2"}); break;
        case Format::Wav: add({L"-c:a", L"pcm_s16le"}); break;
        case Format::Flac: add({L"-c:a", L"flac"}); break;
        case Format::M4a: add({L"-c:a", L"aac", L"-b:a", bitrate(192, 128, 256)}); break;
        case Format::Ogg:
            add({L"-c:a", L"libvorbis"});
            if (options.audio_bitrate_kbps) add({L"-b:a", bitrate(192, 128, 256)});
            else add({L"-q:a", compact ? L"3" : high ? L"7" : L"5"});
            break;
        case Format::Opus: add({L"-c:a", L"libopus", L"-b:a", bitrate(128, 96, 192), L"-ar", L"48000", L"-ac", L"2"}); break;
        case Format::Aac: add({L"-c:a", L"aac", L"-b:a", bitrate(192, 128, 256), L"-f", L"adts"}); break;
        default: throw std::runtime_error("Неподдерживаемый медиаформат.");
        }
    }
    if (duration > 0) add({L"-progress", L"pipe:1", L"-stats_period", L"0.25"});
    add({L"-threads", threads, output.wstring()});
    return args;
}
void require_unused_output(const fs::path& output) {
    validate_local_path(fs::absolute(output).lexically_normal(), false);
    if (fs::exists(output)) throw std::runtime_error("Файл результата уже существует.");
}
}
void convert_media(const fs::path& input, const fs::path& output, Format format, const fs::path& work,
                   const ConversionOptions& options, const MediaRunner& custom_runner) {
    validate_options(options);
    const auto codec=requested_codec(format,options);
    require_unused_output(output);
    const MediaRunner runner = custom_runner ? custom_runner : [](const MediaEngineCall& call) {
        run_engine(call.executable, call.arguments, call.log, call.timeout_ms, call.duration_seconds, {}, {}, call.progress_stage);
    };
    const auto executable = module_path(category(input));
    const double duration = media_duration(input, work, runner);
    const auto encoder = select_encoder(executable, codec, work, options, runner);
    auto encode = [&](VideoEncoder selected, const fs::path& destination, bool fallback) {
        const std::wstring stage = L"Конвертация: " + encoder_label(selected) + (codec==VideoCodec::Hevc ? L" HEVC" : codec==VideoCodec::Av1 ? L" AV1" : L"") + (fallback ? L" (после GPU)" : L"");
        report_progress(duration > 0 ? 0 : -1, stage);
        runner({executable, media_arguments(input, destination, format, options, selected, codec, duration),
            work / (L"encode-" + encoder_name(selected,codec) + L".log"), 6 * 60 * 60 * 1000, duration, stage});
    };
    const auto completed=[&](VideoEncoder selected,bool fallback){if(video_output(format)){auto name=format==Format::Webm&&codec==VideoCodec::Default?L"libvpx-vp9":encoder_name(selected,codec);report_encoder(utf8(name),selected==VideoEncoder::Cpu?"cpu":selected==VideoEncoder::Nvidia?"nvidia":selected==VideoEncoder::Intel?"intel":"amd",fallback);}};
    if (encoder == VideoEncoder::Cpu) { encode(encoder, output, false); completed(encoder,false);
        if(!video_output(format)){const char* name=format==Format::Mp3?"libmp3lame":format==Format::Wav?"pcm_s16le":format==Format::Flac?"flac":format==Format::Ogg?"libvorbis":format==Format::Opus?"libopus":"aac";report_encoder(name,"cpu",false);}return; }

    // The hardware attempt writes inside a directory this call creates
    // exclusively. A failed attempt can therefore never remove a caller's
    // output, even if that output appeared while the GPU was encoding.
    const auto attempt = work / L"media-hardware";
    if (!CreateDirectoryW(attempt.c_str(), nullptr)) throw std::runtime_error("Не удалось создать временную папку GPU.");
    struct Cleanup { fs::path path; bool active = true; ~Cleanup() { if (active) remove_owned_tree(path); } } cleanup{attempt};
    const auto partial = attempt / output.filename();
    bool hardware_failed = false;
    try {
        encode(encoder, partial, false);
        if (!fs::is_regular_file(partial) || fs::file_size(partial) == 0) throw std::runtime_error("GPU не создал готовый файл.");
    } catch (const EngineCancelled&) { throw; }
    catch (const std::runtime_error&) { hardware_failed = true; }
    if (!hardware_failed) {
        require_unused_output(output);
        if (!MoveFileExW(partial.c_str(), output.c_str(), MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Не удалось сохранить результат GPU.");
        completed(encoder,false);
        return;
    }
    remove_owned_tree(attempt);
    cleanup.active = false;
    if (fs::exists(attempt)) throw std::runtime_error("Не удалось удалить незавершённый результат GPU.");
    require_unused_output(output);
    encode(VideoEncoder::Cpu, output, true);
    completed(VideoEncoder::Cpu,true);
}
}
