// WebP through libwebp: decoding straight into the Image's buffer, encoding
// with a WebPConfig (exact, single-threaded) into a memory writer.
module openmui.image;

import std;
import mcpplibs.mwrap.libwebp;
using namespace mcpplibs::mwrap::libwebp;  // the header's static inline functions: WebPConfigInit, WebPPictureInit, WebPGetFeatures

namespace omui::imageio {

namespace {

std::string_view encode_error(WebPEncodingError e) {
    switch (e) {
        case VP8_ENC_ERROR_OUT_OF_MEMORY:
        case VP8_ENC_ERROR_BITSTREAM_OUT_OF_MEMORY: return "out of memory";
        case VP8_ENC_ERROR_BAD_DIMENSION: return "bad dimension (at most 16383 × 16383)";
        case VP8_ENC_ERROR_PARTITION0_OVERFLOW:
        case VP8_ENC_ERROR_PARTITION_OVERFLOW: return "partition overflow";
        case VP8_ENC_ERROR_FILE_TOO_BIG: return "file too big";
        default: return "encoding failed";
    }
}

std::expected<std::vector<std::uint8_t>, std::string> write(const std::uint8_t* rgba, int width, int height, int stride, WebpOptions options) {
    WebPConfig config;
    if (!WebPConfigInit(&config)) return std::unexpected("webp: library version mismatch");
    config.lossless = options.lossless ? 1 : 0;
    config.quality = std::clamp(options.quality, 0.0f, 100.0f);
    config.method = std::clamp(options.method, 0, 6);
    config.exact = 1;         // keep the colours under clear pixels
    config.thread_level = 0;  // one thread: the same bytes everywhere
    if (!WebPValidateConfig(&config)) return std::unexpected("webp: invalid options");
    WebPPicture picture;
    if (!WebPPictureInit(&picture)) return std::unexpected("webp: library version mismatch");
    picture.use_argb = 1;
    picture.width = width;
    picture.height = height;
    if (!WebPPictureImportRGBA(&picture, rgba, stride)) {
        std::string message = std::format("webp: {}", encode_error(picture.error_code));
        WebPPictureFree(&picture);
        return std::unexpected(message);
    }
    WebPMemoryWriter writer;
    WebPMemoryWriterInit(&writer);
    picture.writer = WebPMemoryWrite;
    picture.custom_ptr = &writer;
    const bool ok = WebPEncode(&config, &picture) != 0;
    const WebPEncodingError error = picture.error_code;
    WebPPictureFree(&picture);
    std::vector<std::uint8_t> out;
    if (ok) out.assign(writer.mem, writer.mem + writer.size);
    WebPMemoryWriterClear(&writer);
    if (!ok) return std::unexpected(std::format("webp: {}", encode_error(error)));
    return out;
}

std::expected<Image, std::string> read(std::span<const std::uint8_t> bytes) {
    WebPBitstreamFeatures features;
    if (WebPGetFeatures(bytes.data(), bytes.size(), &features) != VP8_STATUS_OK) return std::unexpected("webp: not a WebP image");
    if (features.has_animation) return std::unexpected("webp: animated WebP is not read (its first frame neither)");
    Image out;
    out.width = features.width;
    out.height = features.height;
    out.alpha = Alpha::straight;
    out.rgba.resize(static_cast<std::size_t>(out.width) * out.height * 4);
    if (!WebPDecodeRGBAInto(bytes.data(), bytes.size(), out.rgba.data(), out.rgba.size(), out.width * 4))
        return std::unexpected("webp: corrupt or truncated image");
    return out;
}

// libwebp's DSP and colour-conversion tables are set up on first use, and
// that setup is only lock-guarded in a build with threads — this one has
// none. So the first use of either direction encodes (lossy with alpha, and
// lossless) and decodes a tiny picture once, under a std::once_flag: every
// table is in place before two threads can race to make it.
void ready() {
    static std::once_flag once;
    std::call_once(once, [] {
        const std::array<std::uint8_t, 16> px{255, 0, 0, 255, 0, 255, 0, 128, 0, 0, 255, 0, 255, 255, 255, 255};
        for (const bool lossless : {false, true})
            if (auto bytes = write(px.data(), 2, 2, 8, {.quality = 50, .lossless = lossless, .method = 0}))
                (void)read(*bytes);
    });
}

}  // namespace

std::expected<std::vector<std::uint8_t>, std::string> encode_webp(const View& pixels, WebpOptions options) {
    if (!pixels.rgba || pixels.width <= 0 || pixels.height <= 0) return std::unexpected("empty image");
    ready();
    const Straight src = straight(pixels);  // files are straight alpha
    return write(src.rgba, pixels.width, pixels.height, src.stride, options);
}

std::expected<Image, std::string> decode_webp(std::span<const std::uint8_t> bytes) {
    ready();
    return read(bytes);
}

}  // namespace omui::imageio
