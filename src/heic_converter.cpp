#include "heic_converter.h"
#include "avif_converter.h"
#include "webp_converter.h"
#include "image_converter.h"
#include "engines.h"
#include "module_paths.h"
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <memory>
#include <stdexcept>

namespace shiftwheel {
namespace fs = std::filesystem;
fs::path heic_module(const wchar_t* filename) {
    return module_directory("heic") / filename;
}
namespace {
// Public libheif 1.23.6 C ABI. Loaded only by the short-lived conversion worker.
// Keep the runtime pinned: these structures intentionally mirror its published headers.
struct Error { int code, subcode; const char* message; };
struct Nclx { uint8_t version; int primaries, transfer, matrix; uint8_t full_range; float xy[8]; };
struct Limits {
    uint8_t version;
    uint64_t image_pixels, tiles;
    uint32_t bayer, items, profile_size;
    uint64_t memory_block;
    uint32_t components, extents, groups, children;
    uint64_t total_memory;
    uint32_t sample_descriptions, sample_groups, sequence_frames, brands, bad_pixels, pixel_bytes;
    const Limits* parent;
};
static_assert(sizeof(Error) == 16 && sizeof(Nclx) == 52);
static_assert(sizeof(Limits) == 104 && offsetof(Limits, total_memory) == 64);
void checked(Error error) {
    if (error.code) throw std::runtime_error("Повреждённый или неподдерживаемый HEIC: " + std::string(error.message ? error.message : "libheif"));
}
struct Heif {
    HMODULE dll = nullptr;
    Error (*init)(const void*) = nullptr;
    void (*deinit)() = nullptr;
    uint32_t (*version)() = nullptr;
    void* (*alloc)() = nullptr;
    void (*free_context)(void*) = nullptr, (*release)(void*) = nullptr;
    Limits* (*limits)(const void*) = nullptr;
    Error (*read)(void*, const void*, size_t, const void*) = nullptr;
    int (*images)(void*) = nullptr, (*sequence)(const void*) = nullptr;
    Error (*primary)(void*, void**) = nullptr;
    int (*width)(const void*) = nullptr, (*height)(const void*) = nullptr;
    int (*ispe_width)(const void*) = nullptr, (*ispe_height)(const void*) = nullptr;
    int (*bits)(const void*) = nullptr, (*premultiplied)(const void*) = nullptr;
    int (*light_level)(const void*) = nullptr, (*mastering)(const void*) = nullptr;
    int (*aux)(const void*, int) = nullptr;
    size_t (*profile_size)(const void*) = nullptr;
    Error (*nclx)(const void*, Nclx**) = nullptr;
    void (*free_nclx)(Nclx*) = nullptr;
    int (*metadata_ids)(const void*, const char*, uint32_t*, int) = nullptr;
    size_t (*metadata_size)(const void*, uint32_t) = nullptr;
    Error (*metadata)(const void*, uint32_t, void*) = nullptr;
    template<class T> T load(const char* name) {
        auto symbol = reinterpret_cast<T>(GetProcAddress(dll, name));
        if (!symbol) throw std::runtime_error("Несовместимая версия модуля HEIC.");
        return symbol;
    }
    Heif() {
        dll = LoadLibraryExW(heic_module(L"libheif.dll").c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!dll) throw std::runtime_error("Модуль HEIC отсутствует или повреждён. Нужна папка modules/heic из полной сборки.");
        try {
#define API(member, name) member = load<decltype(member)>(name)
            API(init, "heif_init"); API(deinit, "heif_deinit"); API(version, "heif_get_version_number");
            API(alloc, "heif_context_alloc"); API(free_context, "heif_context_free");
            API(limits, "heif_context_get_security_limits"); API(read, "heif_context_read_from_memory_without_copy");
            API(images, "heif_context_get_number_of_top_level_images"); API(sequence, "heif_context_has_sequence");
            API(primary, "heif_context_get_primary_image_handle"); API(release, "heif_image_handle_release");
            API(width, "heif_image_handle_get_width"); API(height, "heif_image_handle_get_height");
            API(ispe_width, "heif_image_handle_get_ispe_width"); API(ispe_height, "heif_image_handle_get_ispe_height");
            API(bits, "heif_image_handle_get_luma_bits_per_pixel"); API(premultiplied, "heif_image_handle_is_premultiplied_alpha");
            API(light_level, "heif_image_handle_has_content_light_level"); API(mastering, "heif_image_handle_has_mastering_display_colour_volume");
            API(aux, "heif_image_handle_get_number_of_auxiliary_images");
            API(profile_size, "heif_image_handle_get_raw_color_profile_size");
            API(nclx, "heif_image_handle_get_nclx_color_profile"); API(free_nclx, "heif_nclx_color_profile_free");
            API(metadata_ids, "heif_image_handle_get_list_of_metadata_block_IDs");
            API(metadata_size, "heif_image_handle_get_metadata_size"); API(metadata, "heif_image_handle_get_metadata");
#undef API
            if (version() != 0x01170600) throw std::runtime_error("Нужен модуль HEIC libheif 1.23.6 из полной сборки.");
            checked(init(nullptr));
        } catch (...) { FreeLibrary(dll); dll = nullptr; throw; }
    }
    ~Heif() { if (dll) { deinit(); FreeLibrary(dll); } }
};
void dimensions(int width, int height) {
    if (width <= 0 || height <= 0 || width > 32768 || height > 32768 || uint64_t(width) * height > 40000000)
        throw std::runtime_error("Лимит HEIC: 40 млн пикселей и 32768 пикселей по каждой стороне.");
}
void check_exif(const std::vector<unsigned char>& bytes) {
    // HEIF Exif starts with a big-endian 32-bit TIFF-header offset, measured after that field.
    if (bytes.size() < 12) throw std::runtime_error("Повреждены метаданные EXIF HEIC.");
    const uint64_t start = 4 + (uint64_t(bytes[0]) << 24 | uint64_t(bytes[1]) << 16 | uint64_t(bytes[2]) << 8 | bytes[3]);
    if (start + 8 > bytes.size()) throw std::runtime_error("Повреждены метаданные EXIF HEIC.");
    auto p = bytes.data() + start; const auto size = bytes.size() - start;
    const bool little = p[0] == 'I' && p[1] == 'I';
    if (!little && !(p[0] == 'M' && p[1] == 'M')) throw std::runtime_error("Повреждены метаданные EXIF HEIC.");
    auto number = [&](uint64_t offset, unsigned length) {
        if (offset > size || length > size - offset) throw std::runtime_error("Повреждены метаданные EXIF HEIC.");
        uint32_t value = 0;
        for (unsigned i = 0; i < length; ++i) value = value << 8 | p[offset + (little ? length - i - 1 : i)];
        return value;
    };
    if (number(2, 2) != 42) throw std::runtime_error("Повреждены метаданные EXIF HEIC.");
    auto offset = number(4, 4), count = number(offset, 2);
    if (count > 4096 || uint64_t(offset) + 2 + uint64_t(count) * 12 + 4 > size) throw std::runtime_error("Повреждены метаданные EXIF HEIC.");
    for (uint32_t i = 0; i < count; ++i) {
        const uint64_t entry = uint64_t(offset) + 2 + uint64_t(i) * 12;
        if (number(entry, 2) == 274 && (number(entry + 2, 2) != 3 || number(entry + 4, 4) != 1 || number(entry + 8, 2) != 1))
            throw std::runtime_error("Сначала примените поворот EXIF HEIC в редакторе. Повороты контейнера HEIF поддерживаются.");
    }
}
bool validate_heic(const fs::path& input) {
    WinHandle file(CreateFileW(input.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    LARGE_INTEGER size{};
    if (file.value == INVALID_HANDLE_VALUE || !GetFileSizeEx(file.value, &size) || size.QuadPart <= 0 || size.QuadPart > 1024LL * 1024 * 1024)
        throw std::runtime_error("HEIC пуст, недоступен или больше 1 ГБ.");
    WinHandle mapping(CreateFileMappingW(file.value, nullptr, PAGE_READONLY, 0, 0, nullptr));
    void* bytes = mapping.value ? MapViewOfFile(mapping.value, FILE_MAP_READ, 0, 0, 0) : nullptr;
    if (!bytes) throw std::runtime_error("Не удалось прочитать HEIC.");
    struct View { void* value; ~View() { UnmapViewOfFile(value); } } view{bytes};
    Heif heif;
    using Object = std::unique_ptr<void, void(*)(void*)>;
    Object context(heif.alloc(), heif.free_context);
    if (!context) throw std::runtime_error("Недостаточно памяти для HEIC.");
    auto limits = heif.limits(context.get());
    if (!limits || limits->version < 4) throw std::runtime_error("Несовместимые лимиты модуля HEIC.");
    limits->image_pixels = 40000000; limits->profile_size = 4 * 1024 * 1024;
    limits->memory_block = 512ULL * 1024 * 1024; limits->total_memory = 1024ULL * 1024 * 1024;
    checked(heif.read(context.get(), bytes, static_cast<size_t>(size.QuadPart), nullptr));
    if (heif.sequence(context.get()) || heif.images(context.get()) != 1)
        throw std::runtime_error("Анимированные и многокадровые HEIC пока не поддерживаются.");
    void* raw = nullptr; checked(heif.primary(context.get(), &raw)); Object image(raw, heif.release);
    dimensions(heif.width(raw), heif.height(raw)); dimensions(heif.ispe_width(raw), heif.ispe_height(raw));
    // Alpha and thumbnails are supported. Additional auxiliary images can contain HDR gainmaps or depth.
    if (heif.aux(raw, 2) > 0) throw std::runtime_error("HEIC с картой глубины, HDR gain map или дополнительными каналами пока не поддерживается.");
    if (heif.premultiplied(raw)) throw std::runtime_error("HEIC с предварительно умноженной прозрачностью пока не поддерживается.");
    if (heif.light_level(raw) || heif.mastering(raw)) throw std::runtime_error("HEIC с метаданными яркости HDR пока не поддерживается. Сначала преобразуйте его в SDR sRGB.");
    if (heif.bits(raw) != 8) throw std::runtime_error("Поддерживается только 8-битный SDR HEIC. HDR и более высокая точность пока не поддерживаются.");
    const auto profile_size = heif.profile_size(raw);
    if (profile_size > 4 * 1024 * 1024) throw std::runtime_error("Слишком большой цветовой профиль HEIC.");
    Nclx* nclx = nullptr; const auto error = heif.nclx(raw, &nclx);
    std::unique_ptr<Nclx, void(*)(Nclx*)> colors(nclx, heif.free_nclx);
    if (error.code != 0 && error.code != 10) checked(error);
    if (nclx && (nclx->transfer == 16 || nclx->transfer == 18)) throw std::runtime_error("HDR HEIC пока не поддерживается. Сначала преобразуйте его в SDR sRGB.");
    if (!profile_size && (!nclx || (nclx->primaries != 1 && nclx->primaries != 2) || (nclx->transfer != 13 && nclx->transfer != 2)))
        throw std::runtime_error("HEIC использует цветовое пространство без поддерживаемого ICC-профиля. Сначала преобразуйте его в sRGB.");
    uint32_t ids[2]{}; const int metadata_count = heif.metadata_ids(raw, "Exif", ids, 2);
    if (metadata_count > 1) throw std::runtime_error("Несколько блоков EXIF HEIC не поддерживаются.");
    if (metadata_count == 1) {
        const auto length = heif.metadata_size(raw, ids[0]);
        if (!length || length > 4 * 1024 * 1024) throw std::runtime_error("Повреждены метаданные EXIF HEIC.");
        std::vector<unsigned char> metadata(length); checked(heif.metadata(raw, ids[0], metadata.data())); check_exif(metadata);
    }
    return profile_size != 0;
}
}
void convert_heic(const fs::path& input, const fs::path& output, Format format, const fs::path& work, const ConversionOptions& options) {
    validate_options(options);
    if (fs::file_size(input) > 1024ULL * 1024 * 1024) throw std::runtime_error("Лимит исходного изображения для HEIC: 1 ГБ.");
    const auto threads = std::to_wstring(resource_threads(options.resources));
    const auto source_format = parse_format(input.extension().wstring());
    fs::path pixels = input;
    auto color = ImageColor::ToSrgb;
    if (source_format == Format::Heic) {
        if (validate_heic(input)) color = ImageColor::RequireProfile;
        pixels = work / L"heic-decoded.png";
        // Default libheif decoding applies irot, imir and clean-aperture crop before writing PNG.
        run_engine(heic_module(L"heif-dec.exe"), {L"--quiet", L"--codec-threads", threads, L"--tile-threads", L"1",
            input.wstring(), pixels.wstring()}, work / L"heic-decode.log", 120000);
    } else if (source_format == Format::Avif || source_format == Format::Webp) {
        pixels = work / L"heic-bridge.png";
        if (source_format == Format::Avif) convert_avif(input, pixels, Format::Png, work, options);
        else convert_webp(input, pixels, Format::Png, work, options);
    }
    if (format == Format::Heic || format == Format::Avif || format == Format::Webp) {
        const auto normalized = work / L"heic-normalized.png";
        convert_image(pixels, normalized, work / L"heic-normalize.tmp", color, options);
        if (format == Format::Avif) convert_avif(normalized, output, format, work, options);
        else if (format == Format::Webp) convert_webp(normalized, output, format, work, options);
        else {
            const auto quality = options.quality == QualityPreset::Compact ? L"60" : options.quality == QualityPreset::High ? L"95" : L"80";
            run_engine(heic_module(L"heif-enc.exe"), {L"-e", L"x265", L"-q", quality, L"-p", L"preset=fast", L"-p", L"chroma=444",
                L"-p", L"x265:pools=" + threads, L"-p", L"x265:frame-threads=1", L"--colour_primaries", L"1",
                L"--transfer_characteristic", L"13", L"--matrix_coefficients", L"6", L"-o", output.wstring(), normalized.wstring()},
                work / L"heic-encode.log", 300000);
            validate_heic(output);
        }
    } else convert_image(pixels, output, work / L"heic-image.tmp", color, options);
}
}
