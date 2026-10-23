#include "media_converter.h"
#include "engines.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace fs = std::filesystem;
using namespace shiftwheel;
namespace {
void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
std::string bytes(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), {}};
}
void write(const fs::path& path, const std::string& content) {
    std::ofstream stream(path, std::ios::binary); stream << content;
    check(static_cast<bool>(stream), "test fixture write failed");
}
std::wstring option(const MediaEngineCall& call, const std::wstring& key) {
    const auto found = std::find(call.arguments.begin(), call.arguments.end(), key);
    return found == call.arguments.end() || found + 1 == call.arguments.end() ? L"" : *(found + 1);
}
bool flag(const MediaEngineCall& call, const std::wstring& key) {
    return std::find(call.arguments.begin(), call.arguments.end(), key) != call.arguments.end();
}
bool probe(const MediaEngineCall& call) { return option(call, L"-f") == L"lavfi"; }
struct Scenario {
    fs::path root, source, output;
    std::wstring available = L"h264_nvenc";
    std::vector<MediaEngineCall> calls;
    bool fail_hardware = false, fail_cpu = false, cancel_probe = false, cancel_hardware = false, introduce_foreign = false, recreate_attempt = false;
    fs::path hardware_partial;
    explicit Scenario(const fs::path& path) : root(path), source(path.parent_path() / L"original.mp4"), output(path / L"result.mp4") {
        fs::create_directory(root);
        if (!fs::exists(source)) write(source, "original source stays unchanged");
    }
    MediaRunner runner() {
        return [this](const MediaEngineCall& call) {
            calls.push_back(call);
            check(!fs::exists(call.log), "attempt log must be exclusive and distinct");
            write(call.log, "separate process diagnostic");
            if (call.executable.filename() == L"ffprobe.exe") { write(call.log, "4.0\n"); return; }
            const auto encoder = option(call, L"-c:v");
            if (probe(call)) {
                if (cancel_probe) throw EngineCancelled();
                if (encoder != available) throw std::runtime_error("hardware initialization unavailable");
                return;
            }
            const fs::path destination(call.arguments.back());
            check(!fs::exists(destination), "process cannot overwrite a foreign file");
            if (encoder.ends_with(L"_nvenc") || encoder.ends_with(L"_qsv") || encoder.ends_with(L"_amf")) {
                hardware_partial = destination;
                write(destination, "unfinished hardware output");
                if (introduce_foreign) write(output, "foreign output appeared during encoding");
                if (cancel_hardware) throw EngineCancelled();
                if (fail_hardware) throw std::runtime_error("driver failed after encoding started");
                write(destination, "completed hardware output");
            } else {
                check(hardware_partial.empty() || !fs::exists(hardware_partial), "CPU must start only after owned hardware partial is removed");
                if (recreate_attempt) {
                    fs::create_directory(hardware_partial.parent_path());
                    write(hardware_partial.parent_path() / "foreign.txt", "new foreign directory");
                }
                write(destination, "completed software output");
                if (fail_cpu) throw std::runtime_error("software conversion failed");
            }
        };
    }
    std::vector<MediaEngineCall> probes() const {
        std::vector<MediaEngineCall> result;
        for (const auto& call : calls) if (probe(call)) result.push_back(call);
        return result;
    }
    std::vector<MediaEngineCall> encodes() const {
        std::vector<MediaEngineCall> result;
        for (const auto& call : calls) if (call.executable.filename() != L"ffprobe.exe" && !probe(call)) result.push_back(call);
        return result;
    }
    void convert(const ConversionOptions& options = {}, Format format = Format::Mp4) {
        convert_media(source, output, format, root, options, runner());
        check(bytes(source) == "original source stays unchanged", "media conversion changed original");
    }
};
}
int wmain(int argc, wchar_t** argv) {
    // The integration driver exercises this same production media path against
    // real FFmpeg. Only unavailable-device/runtime-failure conditions are
    // controlled at the external process boundary; outputs remain real media.
    if (argc == 7 && std::wstring(argv[1]) == L"--real-media") {
        try {
            const fs::path input(argv[2]), output(argv[3]), work(argv[4]);
            const std::wstring mode(argv[6]);
            check(CreateDirectoryW(work.c_str(), nullptr) != FALSE, "integration work directory must be newly owned");
            const auto options = load_options(argv[5]);
            const MediaRunner runner = [&](const MediaEngineCall& call) {
                const auto encoder = option(call, L"-c:v");
                if (call.executable.filename() != L"ffprobe.exe") {
                    std::ofstream trace(work / "calls.tsv", std::ios::binary | std::ios::app);
                    trace << (probe(call) ? "probe" : "encode") << '\t' << utf8(encoder) << '\t' << utf8(call.progress_stage) << '\n';
                }
                if (probe(call) && (mode == L"unavailable" || (mode == L"intel" && !encoder.ends_with(L"_qsv")) ||
                    (mode == L"amd" && !encoder.ends_with(L"_amf")))) {
                    write(call.log, "controlled unavailable hardware at process boundary");
                    throw std::runtime_error("controlled unavailable hardware");
                }
                run_engine(call.executable, call.arguments, call.log, call.timeout_ms, call.duration_seconds, {}, {}, call.progress_stage);
                if (!probe(call) && mode == L"runtime-failure" && (encoder.ends_with(L"_nvenc") || encoder.ends_with(L"_qsv") || encoder.ends_with(L"_amf"))) {
                    std::ofstream(call.log, std::ios::app) << "controlled error after hardware wrote its output\n";
                    throw std::runtime_error("controlled error after real hardware output");
                }
            };
            convert_media(input, output, parse_format(output.extension().wstring()), work, options, runner);
            return 0;
        } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    }
    const auto root = fs::current_path() / "test-output" / (L"media-gpu-" + std::to_wstring(GetTickCount64()));
    fs::create_directories(root);
    int failures = 0, number = 0;
    auto test = [&](const char* name, auto body) {
        try { body(root / std::to_wstring(++number)); std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& error) { ++failures; std::cerr << "FAIL " << name << ": " << error.what() << '\n'; }
    };
    test("auto probes actual bounded hardware encode before choosing NVIDIA", [](const fs::path& path) {
        Scenario run(path); run.convert();
        check(run.probes().size() == 1, "auto must perform a real initialization probe");
        const auto call = run.probes().front();
        check(option(call, L"-c:v") == L"h264_nvenc" && option(call, L"-frames:v") == L"3", "probe must encode bounded frames with NVENC");
        check(call.timeout_ms > 0 && call.timeout_ms <= 5000 && call.duration_seconds == 0, "probe must be time bounded and never emit conversion percentages");
        check(flag(call, L"-nostdin") && flag(call, L"-xerror"), "probe cannot wait for input or accept encoder errors");
        check(run.encodes().size() == 1 && option(run.encodes().front(), L"-c:v") == L"h264_nvenc", "initialized hardware handles real conversion");
        check(bytes(run.output) == "completed hardware output", "hardware output must reach requested staging target");
    });
    test("failed initialization tries Intel and AMD then CPU", [](const fs::path& path) {
        for (const auto& available : {L"h264_qsv", L"h264_amf", L""}) {
            const auto directory = path.wstring() + available;
            Scenario run(directory); run.available = available; run.convert();
            const auto probes = run.probes(); const auto encodes = run.encodes();
            const auto expected_count = available == std::wstring(L"h264_qsv") ? 2u : 3u;
            check(probes.size() == expected_count, "failed hardware initialization must continue through available backends");
            check(option(probes[0], L"-c:v") == L"h264_nvenc" && option(probes[1], L"-c:v") == L"h264_qsv", "hardware initialization order changed");
            if (probes.size() == 3) check(option(probes[2], L"-c:v") == L"h264_amf", "AMD must be tried before CPU fallback");
            check(encodes.size() == 1 && option(encodes[0], L"-c:v") == (std::wstring(available).empty() ? L"libx264" : available), "use actual initialized encoder or CPU");
        }
    });
    test("CPU only never initializes GPU", [](const fs::path& path) {
        Scenario run(path); ConversionOptions options; options.video_acceleration = VideoAcceleration::Cpu;
        run.convert(options);
        check(run.probes().empty() && run.encodes().size() == 1 && option(run.encodes()[0], L"-c:v") == L"libx264", "CPU only must bypass all hardware probes");
    });
    test("WebM and audio never initialize GPU", [](const fs::path& path) {
        fs::create_directory(path);
        for (const auto format : {Format::Webm, Format::Mp3, Format::Wav, Format::Flac, Format::M4a, Format::Ogg, Format::Opus, Format::Aac}) {
            Scenario run(path / std::to_wstring(static_cast<int>(format))); run.output.replace_extension(extension(format)); run.convert({}, format);
            check(run.probes().empty() && run.encodes().size() == 1, "unrelated format must not touch GPU");
            check(option(run.encodes()[0], L"-c:v") == (format == Format::Webm ? L"libvpx-vp9" : L""), "WebM and audio codec policy changed");
        }
    });
    test("hardware runtime error removes only own partial then retries CPU once", [](const fs::path& path) {
        Scenario run(path); run.fail_hardware = true;
        const auto unrelated = path / "unrelated.txt"; write(unrelated, "caller owned"); run.convert();
        const auto encodes = run.encodes();
        check(encodes.size() == 2 && option(encodes[0], L"-c:v") == L"h264_nvenc" && option(encodes[1], L"-c:v") == L"libx264", "runtime failure must retry once on CPU");
        check(encodes[0].log != encodes[1].log && bytes(encodes[0].log) == "separate process diagnostic", "fallback must retain hardware log and use distinct CPU log");
        check(bytes(run.output) == "completed software output" && bytes(unrelated) == "caller owned", "fallback must preserve unrelated file and publish CPU result");
    });
    test("failed CPU retry is final", [](const fs::path& path) {
        Scenario run(path); run.fail_hardware = true; run.fail_cpu = true; bool failed = false;
        try { run.convert(); } catch (const std::runtime_error&) { failed = true; }
        check(failed && run.encodes().size() == 2, "CPU failure after hardware failure must propagate without another retry");
    });
    test("cleanup ownership ends after removing the hardware directory", [](const fs::path& path) {
        Scenario run(path); run.fail_hardware = true; run.recreate_attempt = true; run.convert();
        check(bytes(run.hardware_partial.parent_path() / "foreign.txt") == "new foreign directory", "fallback destructor must not delete a replacement foreign directory");
    });
    test("cancellation during probe or encode never triggers fallback", [](const fs::path& path) {
        fs::create_directory(path);
        for (bool during_probe : {true, false}) {
            Scenario run(path / (during_probe ? "probe" : "encode")); run.cancel_probe = during_probe; run.cancel_hardware = !during_probe;
            bool cancelled = false; try { run.convert(); } catch (const EngineCancelled&) { cancelled = true; }
            check(cancelled && run.probes().size() == 1 && run.encodes().size() == (during_probe ? 0u : 1u), "cancellation is not an encoder availability failure");
        }
    });
    test("foreign output is preserved both before conversion and during hardware failure", [](const fs::path& path) {
        fs::create_directory(path);
        Scenario existing(path / "existing"); write(existing.output, "foreign existing output"); bool failed = false;
        try { existing.convert(); } catch (const std::exception&) { failed = true; }
        check(failed && existing.calls.empty() && bytes(existing.output) == "foreign existing output", "existing output must be rejected before processes start");
        Scenario racing(path / "racing"); racing.fail_hardware = true; racing.introduce_foreign = true; failed = false;
        try { racing.convert(); } catch (const std::exception&) { failed = true; }
        check(failed && bytes(racing.output) == "foreign output appeared during encoding" && racing.encodes().size() == 1, "retry must not delete or open a foreign final output");
        check(!fs::exists(racing.hardware_partial), "owned failed hardware partial still needs removal");
    });
    test("each GPU uses supported rate control and preserves mapping scaling and AAC", [](const fs::path& path) {
        fs::create_directory(path);
        for (const auto& encoder : {L"h264_nvenc", L"h264_qsv", L"h264_amf"}) for (const auto quality : {QualityPreset::Compact, QualityPreset::Balanced, QualityPreset::High}) {
            Scenario run(path / (std::wstring(encoder) + std::to_wstring(static_cast<int>(quality)))); run.available = encoder;
            ConversionOptions options; options.quality = quality; options.video_max_height = 720; options.audio_bitrate_kbps = 160; options.resources = ResourceMode::Economy;
            run.convert(options);
            const auto call = run.encodes().front();
            const auto q = quality == QualityPreset::Compact ? L"28" : quality == QualityPreset::High ? L"18" : L"23";
            check(option(call, L"-c:v") == encoder && !flag(call, L"-crf"), "GPU must not receive x264 quality flags");
            check(option(call, L"-vf") == L"scale=-2:trunc(min(ih\\,720)/2)*2,pad=ceil(iw/2)*2:ceil(ih/2)*2", "scaling and even dimension padding changed");
            check(flag(call, L"0:v:0") && flag(call, L"0:a:0?") && flag(call, L"-sn") && flag(call, L"-dn"), "video and optional audio mapping changed");
            check(option(call, L"-c:a") == L"aac" && option(call, L"-b:a") == L"160k" && option(call, L"-movflags") == L"+faststart", "AAC bitrate or MP4 faststart changed");
            check(option(call, L"-threads") == L"1" && option(call, L"-filter_threads") == L"1", "resource CPU thread policy changed");
            if (std::wstring(encoder) == L"h264_nvenc") check(option(call, L"-rc") == L"vbr" && option(call, L"-cq") == q && option(call, L"-b:v") == L"0", "NVENC requires CQ VBR rate control");
            if (std::wstring(encoder) == L"h264_qsv") check(option(call, L"-global_quality") == q && option(call, L"-pix_fmt") == L"nv12", "QSV requires global quality and supported NV12 input");
            if (std::wstring(encoder) == L"h264_amf") check(option(call, L"-rc") == L"cqp" && option(call, L"-qp_i") == q && option(call, L"-qp_p") == q && option(call, L"-qp_b") == q, "AMF requires explicit constant QP values");
            check(call.progress_stage.find(std::wstring(encoder) == L"h264_nvenc" ? L"NVIDIA" : std::wstring(encoder) == L"h264_qsv" ? L"Intel" : L"AMD") != std::wstring::npos, "progress stage must identify active GPU");
        }
    });
    test("new codec probes, fallback and container policy", [](const fs::path& path) {
        fs::create_directory(path); unsigned n=0;
        for (const auto codec : {VideoCodec::Hevc, VideoCodec::Av1}) {
            const std::wstring prefix = codec==VideoCodec::Hevc ? L"hevc" : L"av1";
            const std::wstring cpu = codec==VideoCodec::Hevc ? L"libx265" : L"libaom-av1";
            for (const auto backend : {L"_nvenc", L"_qsv", L"_amf"}) {
                Scenario run(path / std::to_wstring(++n)); run.available=prefix+backend;
                ConversionOptions options; options.video_codec=codec; run.convert(options);
                check(option(run.encodes()[0],L"-c:v")==run.available,"requested hardware codec lost");
                for(const auto& call:run.probes()) check(option(call,L"-c:v").starts_with(prefix),"probe must request selected codec");
                if(codec==VideoCodec::Hevc&&std::wstring(backend)==L"_amf")check(!flag(run.encodes()[0],L"-qp_b"),"HEVC AMF has no qp_b option");
                run.fail_hardware=true;
                Scenario retry(path / std::to_wstring(++n));retry.available=prefix+backend;retry.fail_hardware=true;retry.convert(options);
                check(retry.encodes().size()==2&&option(retry.encodes()[1],L"-c:v")==cpu,"CPU fallback silently changed codec");
                Scenario cancelled(path / std::to_wstring(++n));cancelled.available=prefix+backend;cancelled.cancel_hardware=true;
                bool stopped=false;try{cancelled.convert(options);}catch(const EngineCancelled&){stopped=true;}
                check(stopped&&cancelled.encodes().size()==1,"new codec cancellation retried");
            }
            Scenario unavailable(path / std::to_wstring(++n));unavailable.available=L"";
            ConversionOptions options;options.video_codec=codec;unavailable.convert(options);
            check(unavailable.probes().size()==3&&option(unavailable.encodes()[0],L"-c:v")==cpu,"missing GPU must use same CPU codec");
            options.video_acceleration=VideoAcceleration::Cpu; options.resources=ResourceMode::Economy;
            Scenario only_cpu(path / std::to_wstring(++n));only_cpu.convert(options);
            check(only_cpu.probes().empty()&&option(only_cpu.encodes()[0],L"-c:v")==cpu,"CPU-only codec policy");
            if(codec==VideoCodec::Hevc)check(option(only_cpu.encodes()[0],L"-x265-params").find(L"pools=1:frame-threads=1")!=std::wstring::npos,"x265 thread pool unbounded");
            else check(option(only_cpu.encodes()[0],L"-row-mt")==L"1"&&option(only_cpu.encodes()[0],L"-threads")==L"1","AV1 CPU threads unbounded");
            Scenario incompatible(path / std::to_wstring(++n)); bool failed=false;
            try{incompatible.convert(options,codec==VideoCodec::Hevc?Format::Webm:Format::Mov);}catch(const std::runtime_error&){failed=true;}
            check(failed&&incompatible.calls.empty()&&!fs::exists(incompatible.output),"container mismatch must fail before probes");
            Scenario audio(path / std::to_wstring(++n));audio.convert(options,Format::Mp3);
            check(audio.probes().empty()&&option(audio.encodes()[0],L"-c:v").empty(),"video codec must not affect audio");
        }
        Scenario explicit_h264(path / std::to_wstring(++n));ConversionOptions options;options.video_codec=VideoCodec::H264;bool failed=false;
        try{explicit_h264.convert(options,Format::Webm);}catch(const std::runtime_error&){failed=true;}
        check(failed&&explicit_h264.calls.empty(),"H264 cannot go into WebM");
        options.video_codec=static_cast<VideoCodec>(99);failed=false;Scenario invalid(path / std::to_wstring(++n));
        try{invalid.convert(options);}catch(const std::runtime_error&){failed=true;}
        check(failed&&invalid.calls.empty(),"invalid codec enum must fail before processes");
    });
    remove_owned_tree(root);
    if (failures) std::cerr << failures << " media GPU tests failed\n";
    return failures ? 1 : 0;
}
