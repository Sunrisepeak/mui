// JPEG through libjpeg-turbo's TurboJPEG API, which reports errors by its return values and a message instead of
// longjmp-ing through C++ frames. The library is built without its SIMD extensions and the default path is whole
// numbers throughout: a file decodes to the same pixels on every platform — the pixels libjpeg-turbo gives
// everywhere else too (its SIMD code reproduces its C code), so what a browser shows of the same file.
module mlibs.image.api;

import std;
import mcpplibs.mwrap.libjpeg_turbo;

namespace mlibs::image {

namespace {

namespace turbo = mcpplibs::mwrap::libjpeg_turbo;

struct Handle {
    tjhandle handle;
    explicit Handle(int kind) : handle(turbo::tj3Init(kind)) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    ~Handle() {
        if (handle) tj3Destroy(handle);
    }
    [[nodiscard]] std::string said() const { return std::string("jpeg: ") + tj3GetErrorStr(handle); }
};

// A picture of more than this is refused before anything is set aside for it, and a progressive file of more
// scans than this before it is gone through: what a file a few bytes long can otherwise ask of memory and time.
constexpr int most_pixels = 1 << 28;
constexpr int most_scans = 500;

}  // namespace

std::expected<Image, std::string> decode_jpeg(std::span<const std::uint8_t> bytes) {
    if (bytes.empty()) return std::unexpected("jpeg: no bytes");
    const Handle decoder(TJINIT_DECOMPRESS);
    if (!decoder.handle) return std::unexpected("jpeg: no decoder");
    tj3Set(decoder.handle, TJPARAM_MAXPIXELS, most_pixels);
    tj3Set(decoder.handle, TJPARAM_SCANLIMIT, most_scans);
    tj3Set(decoder.handle, TJPARAM_STOPONWARNING, 1);  // a file cut short is an error, not a picture grey below the cut
    if (tj3DecompressHeader(decoder.handle, bytes.data(), bytes.size()) != 0) return std::unexpected(decoder.said());
    const int width = tj3Get(decoder.handle, TJPARAM_JPEGWIDTH), height = tj3Get(decoder.handle, TJPARAM_JPEGHEIGHT);
    if (width <= 0 || height <= 0) return std::unexpected("jpeg: a file of tables only, with no picture");
    if (const int bits = tj3Get(decoder.handle, TJPARAM_PRECISION); bits != 8 || tj3Get(decoder.handle, TJPARAM_LOSSLESS) == 1)
        return std::unexpected(std::format("jpeg: {} bits a sample{}, where 8 and not lossless are read", bits, tj3Get(decoder.handle, TJPARAM_LOSSLESS) == 1 ? ", lossless" : ""));
    Image out;
    out.width = width, out.height = height, out.alpha = Alpha::straight;
    out.rgba.resize(static_cast<std::size_t>(width) * height * 4);
    const int colours = tj3Get(decoder.handle, TJPARAM_COLORSPACE);
    const bool four_inks = colours == TJCS_CMYK || colours == TJCS_YCCK;
    if (tj3Decompress8(decoder.handle, bytes.data(), bytes.size(), out.rgba.data(), 0, four_inks ? TJPF_CMYK : TJPF_RGBA) != 0) return std::unexpected(decoder.said());
    if (four_inks)
        // Four inks as the programs that write such files store them (each the share of light the ink lets through):
        // a colour is its ink's share of what black lets through.
        for (std::size_t pixel = 0; pixel < out.rgba.size(); pixel += 4) {
            const std::uint32_t black = out.rgba[pixel + 3];
            for (std::size_t ink = 0; ink < 3; ++ink) out.rgba[pixel + ink] = static_cast<std::uint8_t>((out.rgba[pixel + ink] * black + 127) / 255);
            out.rgba[pixel + 3] = 255;
        }
    return out;
}

std::expected<std::vector<std::uint8_t>, std::string> encode_jpeg(const View& pixels, JpegOptions options) {
    if (!pixels.rgba || pixels.width <= 0 || pixels.height <= 0) return std::unexpected("jpeg: empty image");
    if (options.quality < 1 || options.quality > 100) return std::unexpected(std::format("jpeg: quality {} is not 1 … 100", options.quality));
    // JPEG has no alpha: each pixel is written as it shows over `behind`.
    const Straight src = straight(pixels);
    std::vector<std::uint8_t> shown(static_cast<std::size_t>(pixels.width) * pixels.height * 3);
    for (int row = 0; row < pixels.height; ++row) {
        const std::uint8_t* from = src.rgba + static_cast<std::ptrdiff_t>(row) * src.stride;
        std::uint8_t* into = shown.data() + static_cast<std::size_t>(row) * pixels.width * 3;
        for (int column = 0; column < pixels.width; ++column, from += 4, into += 3) {
            const std::uint32_t cover = from[3];
            for (std::size_t part = 0; part < 3; ++part) into[part] = static_cast<std::uint8_t>((from[part] * cover + options.behind[part] * (255 - cover) + 127) / 255);
        }
    }
    const Handle encoder(TJINIT_COMPRESS);
    if (!encoder.handle) return std::unexpected("jpeg: no encoder");
    if (tj3Set(encoder.handle, TJPARAM_QUALITY, options.quality) != 0 || tj3Set(encoder.handle, TJPARAM_SUBSAMP, options.full_colour ? TJSAMP_444 : TJSAMP_420) != 0 ||
        tj3Set(encoder.handle, TJPARAM_PROGRESSIVE, options.progressive ? 1 : 0) != 0 || tj3Set(encoder.handle, TJPARAM_OPTIMIZE, 1) != 0)
        return std::unexpected(encoder.said());
    unsigned char* written = nullptr;
    std::size_t size = 0;
    if (tj3Compress8(encoder.handle, shown.data(), pixels.width, 0, pixels.height, TJPF_RGB, &written, &size) != 0) {
        tj3Free(written);
        return std::unexpected(encoder.said());
    }
    std::vector<std::uint8_t> out(written, written + size);
    tj3Free(written);
    return out;
}

}  // namespace mlibs::image
