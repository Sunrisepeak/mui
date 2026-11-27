// Radiance pictures (.hdr, "RGBE"): light as three bytes and a shared power of two a pixel. Written here (the
// format is a header of text lines and run-length coded rows; no library is named): Ward's rgbe.c is the model
// for what a pixel's four bytes mean.
module mlibs.image.api;

import std;

namespace mlibs::image {

namespace {

constexpr std::uint64_t most_pixels = std::uint64_t{1} << 28;

// A pixel's four bytes as light: each of the first three times two to the (fourth − 136); a fourth of 0 is no light.
void light_of(const std::uint8_t* rgbe, float* out) noexcept {
    if (rgbe[3] == 0) {
        out[0] = out[1] = out[2] = 0;
        return;
    }
    const float scale = std::ldexp(1.0f, static_cast<int>(rgbe[3]) - 136);
    out[0] = static_cast<float>(rgbe[0]) * scale, out[1] = static_cast<float>(rgbe[1]) * scale, out[2] = static_cast<float>(rgbe[2]) * scale;
}
// Light as a pixel's four bytes: the largest of the three sets the power, and each is cut to a byte under it.
void rgbe_of(const float* light, std::uint8_t* out) noexcept {
    const auto held = [](float value) { return std::isfinite(value) && value > 0 ? value : 0.0f; };
    const float red = held(light[0]), green = held(light[1]), blue = held(light[2]);
    const float most = std::max({red, green, blue});
    if (most < 1e-32f) {
        out[0] = out[1] = out[2] = out[3] = 0;
        return;
    }
    int power = 0;
    const float scale = std::frexp(most, &power) * 256.0f / most;
    if (power + 128 > 255) {  // more light than the format holds: its most
        out[0] = out[1] = out[2] = out[3] = 255;
        return;
    }
    if (power + 128 < 1) {
        out[0] = out[1] = out[2] = out[3] = 0;
        return;
    }
    out[0] = static_cast<std::uint8_t>(red * scale), out[1] = static_cast<std::uint8_t>(green * scale), out[2] = static_cast<std::uint8_t>(blue * scale);
    out[3] = static_cast<std::uint8_t>(power + 128);
}

}  // namespace

bool is_hdr(std::span<const std::uint8_t> bytes) noexcept {
    const std::string_view text(reinterpret_cast<const char*>(bytes.data()), std::min<std::size_t>(bytes.size(), 11));
    return text.starts_with("#?RADIANCE") || text.starts_with("#?RGBE");
}

std::expected<Light, std::string> decode_hdr(std::span<const std::uint8_t> bytes) {
    if (!is_hdr(bytes)) return std::unexpected(std::string("not a Radiance picture: it does not begin with #?RADIANCE"));
    std::size_t at = 0;
    // A line of the header: up to the next line end (none left: the file is cut short).
    const auto line = [&]() -> std::optional<std::string_view> {
        const std::size_t begins = at;
        while (at < bytes.size() && bytes[at] != '\n') ++at;
        if (at >= bytes.size()) return std::nullopt;
        std::string_view text(reinterpret_cast<const char*>(bytes.data()) + begins, at - begins);
        ++at;
        if (text.ends_with('\r')) text.remove_suffix(1);
        return text;
    };
    bool format_said = false;
    for (;;) {
        const auto text = line();
        if (!text) return std::unexpected(std::string("a Radiance picture cut short in its header"));
        if (text->empty()) break;
        if (text->starts_with("FORMAT=")) {
            if (*text != "FORMAT=32-bit_rle_rgbe") return std::unexpected(std::format("a Radiance picture of {}: only 32-bit_rle_rgbe is read", text->substr(7)));
            format_said = true;
        }
    }
    if (!format_said) return std::unexpected(std::string("a Radiance picture that does not say its format"));
    const auto size = line();
    if (!size) return std::unexpected(std::string("a Radiance picture cut short before its size"));
    // "-Y height +X width": rows from the top, each from the left. "+Y …": rows from the bottom.
    std::uint64_t height = 0, width = 0;
    bool from_bottom = false;
    {
        std::string_view text = *size;
        if (text.starts_with("+Y ")) from_bottom = true;
        else if (!text.starts_with("-Y ")) return std::unexpected(std::format("a Radiance picture laid out as `{}`: only rows of +X are read", text));
        text.remove_prefix(3);
        const char* const end = text.data() + text.size();
        const auto first = std::from_chars(text.data(), end, height);
        const std::string_view rest(first.ptr, static_cast<std::size_t>(end - first.ptr));
        if (first.ec != std::errc{} || !rest.starts_with(" +X ")) return std::unexpected(std::format("a Radiance picture laid out as `{}`: only rows of +X are read", *size));
        const auto second = std::from_chars(rest.data() + 4, end, width);
        if (second.ec != std::errc{} || second.ptr != end) return std::unexpected(std::format("a Radiance picture whose size does not read: `{}`", *size));
    }
    if (width == 0 || height == 0) return std::unexpected(std::string("a Radiance picture of no size"));
    if (width > most_pixels || height > most_pixels || width * height > most_pixels) return std::unexpected(std::format("a Radiance picture of {} × {} pixels: more than 2^28 is not read", width, height));

    Light light{.width = static_cast<int>(width), .height = static_cast<int>(height)};
    light.rgb.resize(static_cast<std::size_t>(width * height) * 3);
    std::vector<std::uint8_t> row(static_cast<std::size_t>(width) * 4);
    const auto cut_short = [&](std::uint64_t which) { return std::unexpected(std::format("a Radiance picture cut short in row {} of {}", which, height)); };
    for (std::uint64_t y = 0; y < height; ++y) {
        const bool packed = width >= 8 && width < 32768 && at + 4 <= bytes.size() && bytes[at] == 2 && bytes[at + 1] == 2 && (bytes[at + 2] & 0x80) == 0;
        if (packed) {
            if ((std::uint64_t{bytes[at + 2]} << 8 | bytes[at + 3]) != width) return std::unexpected(std::format("a Radiance picture whose row {} says another width than the picture's", y));
            at += 4;
            // The four bytes of every pixel apart: all the reds, then the greens, the blues, the powers — each as runs.
            for (std::size_t channel = 0; channel < 4; ++channel)
                for (std::uint64_t x = 0; x < width;) {
                    if (at >= bytes.size()) return cut_short(y);
                    const std::uint8_t count = bytes[at++];
                    const bool run = count > 128;
                    const std::uint64_t many = run ? count - 128u : count;
                    if (many == 0 || x + many > width) return std::unexpected(std::format("a Radiance picture whose row {} holds a run past its end", y));
                    if (at + (run ? 1 : many) > bytes.size()) return cut_short(y);
                    for (std::uint64_t each = 0; each < many; ++each) row[static_cast<std::size_t>(x + each) * 4 + channel] = run ? bytes[at] : bytes[at + each];
                    at += run ? 1 : many, x += many;
                }
        } else {
            // Not packed: four bytes a pixel as they are. (The older packing — a pixel of 1 1 1 n that repeats the one
            // before it — is not told from a pixel of that light, and is read as the pixel it also is.)
            if (at + row.size() > bytes.size()) return cut_short(y);
            std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(at), row.size(), row.begin());
            at += row.size();
        }
        float* const out = light.rgb.data() + static_cast<std::size_t>(from_bottom ? height - 1 - y : y) * static_cast<std::size_t>(width) * 3;
        for (std::uint64_t x = 0; x < width; ++x) light_of(row.data() + x * 4, out + x * 3);
    }
    return light;
}

std::vector<std::uint8_t> encode_hdr(const Light& light) {
    std::vector<std::uint8_t> out;
    if (light.empty() || light.rgb.size() != static_cast<std::size_t>(light.width) * static_cast<std::size_t>(light.height) * 3) return out;
    const std::string head = std::format("#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y {} +X {}\n", light.height, light.width);
    out.assign(head.begin(), head.end());
    const std::size_t width = static_cast<std::size_t>(light.width);
    std::vector<std::uint8_t> row(width * 4);
    const bool packed = light.width >= 8 && light.width < 32768;
    for (int y = 0; y < light.height; ++y) {
        const float* const from = light.rgb.data() + static_cast<std::size_t>(y) * width * 3;
        for (std::size_t x = 0; x < width; ++x) rgbe_of(from + x * 3, row.data() + x * 4);
        if (!packed) {
            out.insert(out.end(), row.begin(), row.end());
            continue;
        }
        out.insert(out.end(), {std::uint8_t{2}, std::uint8_t{2}, static_cast<std::uint8_t>(width >> 8), static_cast<std::uint8_t>(width & 0xff)});
        for (std::size_t channel = 0; channel < 4; ++channel) {
            const auto byte = [&](std::size_t x) { return row[x * 4 + channel]; };
            for (std::size_t x = 0; x < width;) {
                // A run of four or more alike is a run; what is between runs is written as it is, 128 at the most.
                std::size_t alike = 1;
                while (x + alike < width && alike < 127 && byte(x + alike) == byte(x)) ++alike;
                if (alike >= 4) {
                    out.push_back(static_cast<std::uint8_t>(128 + alike)), out.push_back(byte(x));
                    x += alike;
                    continue;
                }
                std::size_t plain = 0;
                while (x + plain < width && plain < 128) {
                    std::size_t next = 1;
                    while (x + plain + next < width && next < 4 && byte(x + plain + next) == byte(x + plain)) ++next;
                    if (next >= 4) break;
                    ++plain;
                }
                out.push_back(static_cast<std::uint8_t>(plain));
                for (std::size_t each = 0; each < plain; ++each) out.push_back(byte(x + each));
                x += plain;
            }
        }
    }
    return out;
}

}  // namespace mlibs::image
