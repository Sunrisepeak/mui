// PNG through libpng's simplified API, which reports errors in its control
// structure instead of longjmp-ing through C++ frames.
module openmui.image;

import std;
import mcpplibs.mwrap.libpng;
using namespace mcpplibs::mwrap::libpng::macros;  // PNG_IMAGE_VERSION, PNG_FORMAT_RGBA, PNG_IMAGE_SIZE

namespace omui::imageio {

std::expected<std::vector<std::uint8_t>, std::string> encode_png(const View& pixels, PngOptions) {
    if (!pixels.rgba || pixels.width <= 0 || pixels.height <= 0) return std::unexpected("empty image");
    const Straight src = straight(pixels);  // files are straight alpha
    png_image image{};
    image.version = PNG_IMAGE_VERSION;
    image.width = static_cast<png_uint_32>(pixels.width);
    image.height = static_cast<png_uint_32>(pixels.height);
    image.format = PNG_FORMAT_RGBA;
    png_alloc_size_t size = 0;
    if (!png_image_write_to_memory(&image, nullptr, &size, 0, src.rgba, src.stride, nullptr))
        return std::unexpected(std::string("png: ") + image.message);
    std::vector<std::uint8_t> out(size);
    if (!png_image_write_to_memory(&image, out.data(), &size, 0, src.rgba, src.stride, nullptr))
        return std::unexpected(std::string("png: ") + image.message);
    out.resize(size);
    png_image_free(&image);
    return out;
}

std::expected<Image, std::string> decode_png(std::span<const std::uint8_t> bytes) {
    png_image image{};
    image.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_memory(&image, bytes.data(), bytes.size()))
        return std::unexpected(std::string("png: ") + image.message);
    image.format = PNG_FORMAT_RGBA;
    Image out;
    out.width = static_cast<int>(image.width);
    out.height = static_cast<int>(image.height);
    out.alpha = Alpha::straight;
    out.rgba.resize(PNG_IMAGE_SIZE(image));
    if (!png_image_finish_read(&image, nullptr, out.rgba.data(), 0, nullptr)) {
        std::string message = std::string("png: ") + image.message;
        png_image_free(&image);
        return std::unexpected(message);
    }
    return out;
}

}  // namespace omui::imageio
