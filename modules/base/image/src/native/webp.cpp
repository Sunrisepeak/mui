// WebP through libwebp: decoding straight into the Image's buffer, encoding
// with a WebPConfig (exact, single-threaded) into a memory writer; animations
// through WebPAnimEncoder (libwebpmux) and WebPAnimDecoder (libwebpdemux).
module openmui.image;

import std;
import mcpplibs.mwrap.libwebp;
import mcpplibs.mwrap.libwebp.demux;  // WebPAnimDecoder
import mcpplibs.mwrap.libwebp.mux;    // WebPAnimEncoder
// The headers' static inline functions: WebPConfigInit, WebPPictureInit,
// WebPGetFeatures, WebPAnimEncoderOptionsInit, WebPAnimEncoderNew,
// WebPAnimDecoderOptionsInit, WebPAnimDecoderNew.
using namespace mcpplibs::mwrap::libwebp;

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

std::expected<WebPConfig, std::string> configure(WebpOptions options) {
    WebPConfig config;
    if (!WebPConfigInit(&config)) return std::unexpected("webp: library version mismatch");
    config.lossless = options.lossless ? 1 : 0;
    config.quality = std::clamp(options.quality, 0.0f, 100.0f);
    config.method = std::clamp(options.method, 0, 6);
    config.exact = 1;         // keep the colours under clear pixels
    config.thread_level = 0;  // one thread: the same bytes everywhere
    if (!WebPValidateConfig(&config)) return std::unexpected("webp: invalid options");
    return config;
}

std::expected<std::vector<std::uint8_t>, std::string> write(const std::uint8_t* rgba, int width, int height, int stride, WebpOptions options) {
    auto config = configure(options);
    if (!config) return std::unexpected(config.error());
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
    const bool ok = WebPEncode(&*config, &picture) != 0;
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
    if (features.has_animation) return std::unexpected("webp: an animated WebP (decode_animated_webp reads it)");
    Image out;
    out.width = features.width;
    out.height = features.height;
    out.alpha = Alpha::straight;
    out.rgba.resize(static_cast<std::size_t>(out.width) * out.height * 4);
    if (!WebPDecodeRGBAInto(bytes.data(), bytes.size(), out.rgba.data(), out.rgba.size(), out.width * 4))
        return std::unexpected("webp: corrupt or truncated image");
    return out;
}

// When frame i starts, in whole milliseconds: i × num / den seconds rounded
// to the nearest, so the delays never drift from the clock.
std::uint64_t start_ms(std::size_t i, const AnimatedWebpOptions& o) {
    const std::uint64_t t = std::uint64_t{o.delay_num} * 1000 * i, den = o.delay_den;
    return (2 * t + den) / (2 * den);
}

std::expected<std::vector<std::uint8_t>, std::string> write_animation(std::span<const View> frames, const AnimatedWebpOptions& o) {
    auto config = configure({.quality = o.quality, .lossless = o.lossless, .method = o.method});
    if (!config) return std::unexpected(config.error());
    WebPAnimEncoderOptions options;
    if (!WebPAnimEncoderOptionsInit(&options)) return std::unexpected("webp: library version mismatch");
    options.anim_params.loop_count = static_cast<int>(o.loops);
    options.anim_params.bgcolor = 0x00000000;  // clear (a hint; viewers mostly ignore it)
    options.minimize_size = 0;
    options.allow_mixed = 0;  // every frame coded as asked
    options.verbose = 0;
    options.kmin = o.kmin;
    options.kmax = o.kmax;
    const int width = frames.front().width, height = frames.front().height;
    WebPAnimEncoder* encoder = WebPAnimEncoderNew(width, height, &options);
    if (!encoder) return std::unexpected(std::format("webp: cannot make a {} × {} animation (at most 16383 × 16383)", width, height));
    WebPPicture picture;
    if (!WebPPictureInit(&picture)) {
        WebPAnimEncoderDelete(encoder);
        return std::unexpected("webp: library version mismatch");
    }
    picture.use_argb = 1;
    picture.width = width;
    picture.height = height;
    std::string error;
    const auto failed = [&](std::string_view what) {
        const char* why = WebPAnimEncoderGetError(encoder);
        error = std::format("webp: {}: {}", what, why && *why ? std::string_view(why) : encode_error(picture.error_code));
    };
    for (std::size_t i = 0; i < frames.size() && error.empty(); ++i) {
        const Straight src = straight(frames[i]);  // files are straight alpha
        if (!WebPPictureImportRGBA(&picture, src.rgba, src.stride)) error = std::format("webp: frame {}: {}", i, encode_error(picture.error_code));
        else if (!WebPAnimEncoderAdd(encoder, &picture, static_cast<int>(start_ms(i, o)), &*config)) failed(std::format("frame {}", i));
    }
    WebPPictureFree(&picture);
    WebPData data;
    WebPDataInit(&data);
    // The end of the last frame, then the file.
    if (error.empty() && !WebPAnimEncoderAdd(encoder, nullptr, static_cast<int>(start_ms(frames.size(), o)), nullptr)) failed("the end");
    if (error.empty() && !WebPAnimEncoderAssemble(encoder, &data)) failed("assembly");
    WebPAnimEncoderDelete(encoder);
    std::vector<std::uint8_t> out;
    if (error.empty()) out.assign(data.bytes, data.bytes + data.size);
    WebPDataClear(&data);
    if (!error.empty()) return std::unexpected(error);
    return out;
}

std::expected<Animation, std::string> read_animation(std::span<const std::uint8_t> bytes) {
    WebPAnimDecoderOptions options;
    if (!WebPAnimDecoderOptionsInit(&options)) return std::unexpected("webp: library version mismatch");
    options.color_mode = MODE_RGBA;  // straight alpha
    options.use_threads = 0;
    const WebPData data{bytes.data(), bytes.size()};
    WebPAnimDecoder* decoder = WebPAnimDecoderNew(&data, &options);
    if (!decoder) return std::unexpected("webp: not a WebP image, or a corrupt or truncated one");
    WebPAnimInfo info;
    if (!WebPAnimDecoderGetInfo(decoder, &info)) {
        WebPAnimDecoderDelete(decoder);
        return std::unexpected("webp: corrupt or truncated animation");
    }
    Animation out;
    out.width = static_cast<int>(info.canvas_width);
    out.height = static_cast<int>(info.canvas_height);
    out.loops = info.loop_count;
    out.frames.reserve(info.frame_count);
    const std::size_t size = static_cast<std::size_t>(out.width) * out.height * 4;
    int before = 0;  // when the frame before ended
    std::string error;
    while (WebPAnimDecoderHasMoreFrames(decoder)) {
        std::uint8_t* canvas = nullptr;
        int end = 0;
        if (!WebPAnimDecoderGetNext(decoder, &canvas, &end) || !canvas) {
            error = std::format("webp: frame {} is corrupt or truncated", out.frames.size());
            break;
        }
        AnimationFrame f;
        f.image.width = out.width;
        f.image.height = out.height;
        f.image.alpha = Alpha::straight;
        f.image.rgba.assign(canvas, canvas + size);
        f.delay_ms = static_cast<std::uint32_t>(std::max(0, end - before));
        before = end;
        out.frames.push_back(std::move(f));
    }
    WebPAnimDecoderDelete(decoder);
    if (!error.empty()) return std::unexpected(error);
    if (out.frames.empty()) return std::unexpected("webp: no frames");
    return out;
}

// libwebp's DSP and colour-conversion tables are set up on first use, and
// that setup is only lock-guarded in a build with threads — this one has
// none. So the first use of either direction encodes (lossy with alpha, and
// lossless) and decodes a tiny picture once, still and as a two-frame
// animation, under a std::once_flag: every table is in place before two
// threads can race to make it.
void ready() {
    static std::once_flag once;
    std::call_once(once, [] {
        const std::array<std::uint8_t, 16> px{255, 0, 0, 255, 0, 255, 0, 128, 0, 0, 255, 0, 255, 255, 255, 255};
        const std::array<std::uint8_t, 16> moved{0, 0, 255, 0, 255, 255, 255, 255, 255, 0, 0, 255, 0, 255, 0, 128};
        const std::array<View, 2> frames{View{px.data(), 2, 2, 8, Alpha::straight}, View{moved.data(), 2, 2, 8, Alpha::straight}};
        for (const bool lossless : {false, true}) {
            if (auto bytes = write(px.data(), 2, 2, 8, {.quality = 50, .lossless = lossless, .method = 0})) (void)read(*bytes);
            if (auto bytes = write_animation(frames, {.quality = 50, .lossless = lossless, .method = 0})) (void)read_animation(*bytes);
        }
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

std::expected<std::vector<std::uint8_t>, std::string> encode_animated_webp(std::span<const View> frames, AnimatedWebpOptions options) {
    if (frames.empty()) return std::unexpected("no frames");
    const View& first = frames.front();
    if (first.width <= 0 || first.height <= 0) return std::unexpected("empty image");
    for (const View& f : frames)
        if (f.width != first.width || f.height != first.height || !f.rgba) return std::unexpected("every frame is the size of the first");
    if (options.delay_den == 0) return std::unexpected("a delay's denominator is not 0");
    if (options.loops > 0xffff) return std::unexpected("webp: at most 65535 loops (0: forever)");
    // Each frame at least a millisecond; the whole within libwebp's int timestamps.
    if (std::uint64_t{options.delay_num} * 1000 < options.delay_den) return std::unexpected("webp: a delay under a millisecond");
    if (start_ms(frames.size(), options) > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) return std::unexpected("webp: too long");
    ready();
    return write_animation(frames, options);
}

std::expected<Animation, std::string> decode_animated_webp(std::span<const std::uint8_t> bytes) {
    ready();
    return read_animation(bytes);
}

}  // namespace omui::imageio
