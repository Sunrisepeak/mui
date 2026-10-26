// PNG through libpng's simplified API, which reports errors in its control
// structure instead of longjmp-ing through C++ frames.
module;
#include <png.h>
module openmui.image;

import std;

namespace omui::imageio {

std::expected<std::vector<std::uint8_t>, std::string> encode_png(const View& pixels, PngOptions) {
    if (!pixels.rgba || pixels.width <= 0 || pixels.height <= 0) return std::unexpected("empty image");
    // Files are straight alpha: undo premultiplication into a copy.
    std::vector<std::uint8_t> straight;
    const std::uint8_t* src = pixels.rgba;
    int stride = pixels.stride;
    if (pixels.alpha == Alpha::premultiplied) {
        Image tmp;
        tmp.width = pixels.width;
        tmp.height = pixels.height;
        tmp.alpha = Alpha::premultiplied;
        tmp.rgba.resize(static_cast<std::size_t>(pixels.width) * pixels.height * 4);
        for (int y = 0; y < pixels.height; ++y)
            std::memcpy(tmp.rgba.data() + static_cast<std::size_t>(y) * pixels.width * 4,
                        pixels.rgba + static_cast<std::size_t>(y) * pixels.stride, static_cast<std::size_t>(pixels.width) * 4);
        unpremultiply(tmp);
        straight = std::move(tmp.rgba);
        src = straight.data();
        stride = pixels.width * 4;
    }
    png_image image{};
    image.version = PNG_IMAGE_VERSION;
    image.width = static_cast<png_uint_32>(pixels.width);
    image.height = static_cast<png_uint_32>(pixels.height);
    image.format = PNG_FORMAT_RGBA;
    png_alloc_size_t size = 0;
    if (!png_image_write_to_memory(&image, nullptr, &size, 0, src, stride, nullptr))
        return std::unexpected(std::string("png: ") + image.message);
    std::vector<std::uint8_t> out(size);
    if (!png_image_write_to_memory(&image, out.data(), &size, 0, src, stride, nullptr))
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
