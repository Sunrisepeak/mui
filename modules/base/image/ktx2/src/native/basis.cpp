// The one unit that names Basis Universal (mcpplibs.mwrap.basis_universal): its KTX2 transcoder behind
// mlibs.image.ktx2's own types.
module mlibs.image.ktx2;

import std;
import mlibs.image;
import mcpplibs.mwrap.basis_universal;

namespace mlibs::image {

namespace {

// More than this many pixels in a picture is refused (as the other readers refuse it): 2^28.
constexpr std::uint64_t most_pixels = std::uint64_t{1} << 28;

// The transcoder's tables, made once.
void ready() {
    static std::once_flag once;
    std::call_once(once, [] { basist::basisu_transcoder_init(); });
}

// A file opened: its header read and held to what is read here.
struct Opened {
    basist::ktx2_transcoder transcoder;
    Ktx2                    what;
};
std::expected<void, std::string> open(Opened& opened, std::span<const std::uint8_t> bytes) {
    ready();
    if (!is_ktx2(bytes)) return std::unexpected(std::string("not a KTX2 file: it does not begin with KTX2's identifier"));
    if (bytes.size() > std::numeric_limits<std::uint32_t>::max()) return std::unexpected(std::string("a KTX2 file of more than 4 GB is not read"));
    basist::ktx2_transcoder& transcoder = opened.transcoder;
    if (!transcoder.init(bytes.data(), static_cast<std::uint32_t>(bytes.size())))
        return std::unexpected(std::string("not a KTX2 file this reader takes: its header is cut short, or it is not packed by one of Basis Universal's codecs"));
    Ktx2& what = opened.what;
    const std::uint64_t width = transcoder.get_width(), height = transcoder.get_height();
    if (width == 0 || height == 0) return std::unexpected(std::string("a KTX2 file of no size"));
    if (width * height > most_pixels) return std::unexpected(std::format("a KTX2 picture of {} × {} pixels: more than 2^28 is not read", width, height));
    what.width = static_cast<int>(width), what.height = static_cast<int>(height);
    what.levels = static_cast<int>(std::max<std::uint32_t>(transcoder.get_levels(), 1));
    what.layers = static_cast<int>(transcoder.get_layers()), what.faces = static_cast<int>(transcoder.get_faces());
    what.codec = transcoder.is_hdr() ? Ktx2Codec::hdr : transcoder.is_etc1s() ? Ktx2Codec::etc1s : transcoder.is_uastc() ? Ktx2Codec::uastc : transcoder.is_xuastc_ldr() ? Ktx2Codec::xuastc : Ktx2Codec::astc;
    what.alpha = transcoder.get_has_alpha() != 0, what.srgb = transcoder.is_srgb();
    return {};
}
// The level's size, the transcoder begun: what both ways of reading a picture need first.
std::expected<basist::ktx2_image_level_info, std::string> begun(Opened& opened, int level) {
    if (opened.what.codec == Ktx2Codec::hdr) return std::unexpected(std::string("a KTX2 file of light beyond a display's white (HDR) is not read yet"));
    if (level < 0 || level >= opened.what.levels) return std::unexpected(std::format("level {}: the file has {}", level, opened.what.levels));
    if (!opened.transcoder.start_transcoding()) return std::unexpected(std::string("the KTX2 file's tables do not read: it is cut short or damaged"));
    basist::ktx2_image_level_info info;
    if (!opened.transcoder.get_image_level_info(info, static_cast<std::uint32_t>(level), 0, 0)) return std::unexpected(std::format("the KTX2 file's level {} does not read", level));
    return info;
}
std::expected<Image, std::string> pixels_of(Opened& opened, int level) {
    const auto info = begun(opened, level);
    if (!info) return std::unexpected(info.error());
    Image image{.width = static_cast<int>(info->m_orig_width), .height = static_cast<int>(info->m_orig_height), .alpha = Alpha::straight};
    const std::uint32_t count = info->m_orig_width * info->m_orig_height;
    image.rgba.resize(std::size_t{count} * 4);
    if (!opened.transcoder.transcode_image_level(static_cast<std::uint32_t>(level), 0, 0, image.rgba.data(), count, basist::transcoder_texture_format::cTFRGBA32))
        return std::unexpected(std::format("the KTX2 file's level {} does not read: it is cut short or damaged", level));
    return image;
}

}  // namespace

std::string_view name(Ktx2Codec codec) noexcept {
    switch (codec) {
        case Ktx2Codec::etc1s: return "etc1s";
        case Ktx2Codec::uastc: return "uastc";
        case Ktx2Codec::astc: return "astc";
        case Ktx2Codec::xuastc: return "xuastc";
        case Ktx2Codec::hdr: return "hdr";
    }
    return "";
}

bool is_ktx2(std::span<const std::uint8_t> bytes) noexcept {
    constexpr std::array<std::uint8_t, 12> identifier{0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32, 0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};
    return bytes.size() >= identifier.size() && std::equal(identifier.begin(), identifier.end(), bytes.begin());
}

std::expected<Ktx2, std::string> describe_ktx2(std::span<const std::uint8_t> bytes) {
    Opened opened;
    if (auto read = open(opened, bytes); !read) return std::unexpected(read.error());
    return opened.what;
}

std::expected<Image, std::string> decode_ktx2(std::span<const std::uint8_t> bytes, int level) {
    Opened opened;
    if (auto read = open(opened, bytes); !read) return std::unexpected(read.error());
    return pixels_of(opened, level);
}

std::expected<std::vector<Image>, std::string> decode_ktx2_levels(std::span<const std::uint8_t> bytes) {
    Opened opened;
    if (auto read = open(opened, bytes); !read) return std::unexpected(read.error());
    std::vector<Image> out;
    for (int level = 0; level < opened.what.levels; ++level) {
        auto image = pixels_of(opened, level);
        if (!image) return std::unexpected(image.error());
        out.push_back(std::move(*image));
    }
    return out;
}

std::expected<Ktx2Packed, std::string> ktx2_blocks(std::span<const std::uint8_t> bytes, Ktx2Blocks as, int level) {
    Opened opened;
    if (auto read = open(opened, bytes); !read) return std::unexpected(read.error());
    const auto info = begun(opened, level);
    if (!info) return std::unexpected(info.error());
    const basist::transcoder_texture_format format = as == Ktx2Blocks::bc7    ? basist::transcoder_texture_format::cTFBC7_RGBA
                                                     : as == Ktx2Blocks::etc2 ? basist::transcoder_texture_format::cTFETC2_RGBA
                                                                              : basist::transcoder_texture_format::cTFASTC_4x4_RGBA;
    // Four pixels a block each way, whatever the file's own blocks are.
    Ktx2Packed packed{.width = static_cast<int>(info->m_orig_width), .height = static_cast<int>(info->m_orig_height)};
    packed.across = (packed.width + 3) / 4, packed.down = (packed.height + 3) / 4;
    const std::uint32_t count = static_cast<std::uint32_t>(packed.across) * static_cast<std::uint32_t>(packed.down);
    packed.bytes.resize(std::size_t{count} * 16);
    if (!opened.transcoder.transcode_image_level(static_cast<std::uint32_t>(level), 0, 0, packed.bytes.data(), count, format))
        return std::unexpected(std::format("the KTX2 file's level {} does not read as blocks: it is cut short or damaged", level));
    return packed;
}

std::expected<Image, std::string> load_ktx2(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        const std::u8string name = file.generic_u8string();  // UTF-8 whatever the system's code page is
        return std::unexpected(std::format("cannot read {}", std::string_view(reinterpret_cast<const char*>(name.data()), name.size())));
    }
    const std::vector<char> held{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    return decode_ktx2(std::span(reinterpret_cast<const std::uint8_t*>(held.data()), held.size()));
}

}  // namespace mlibs::image
