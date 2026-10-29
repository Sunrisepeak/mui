// The sync card, drawn with integers.
module openmui.video;

import std;
import hanim;
import openmui.image;

namespace omui::video {

namespace {

struct Rgb {
    std::uint8_t r, g, b;
};

// Digits and a colon, three by five.
constexpr std::array<std::array<std::uint8_t, 5>, 11> glyphs{{
    {0b111, 0b101, 0b101, 0b101, 0b111},  // 0
    {0b010, 0b110, 0b010, 0b010, 0b111},  // 1
    {0b111, 0b001, 0b111, 0b100, 0b111},  // 2
    {0b111, 0b001, 0b111, 0b001, 0b111},  // 3
    {0b101, 0b101, 0b111, 0b001, 0b001},  // 4
    {0b111, 0b100, 0b111, 0b001, 0b111},  // 5
    {0b111, 0b100, 0b111, 0b101, 0b111},  // 6
    {0b111, 0b001, 0b010, 0b010, 0b010},  // 7
    {0b111, 0b101, 0b111, 0b101, 0b111},  // 8
    {0b111, 0b101, 0b111, 0b001, 0b111},  // 9
    {0b000, 0b010, 0b000, 0b010, 0b000},  // :
}};

class Canvas {
public:
    explicit Canvas(imageio::Image& img) : img_(img) {}
    void fill(std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1, Rgb c) {
        x0 = std::clamp(x0, 0, img_.width), x1 = std::clamp(x1, 0, img_.width);
        y0 = std::clamp(y0, 0, img_.height), y1 = std::clamp(y1, 0, img_.height);
        for (std::int32_t y = y0; y < y1; ++y) {
            std::uint8_t* p = img_.rgba.data() + (static_cast<std::size_t>(y) * img_.width + x0) * 4;
            for (std::int32_t x = x0; x < x1; ++x, p += 4) p[0] = c.r, p[1] = c.g, p[2] = c.b, p[3] = 255;
        }
    }
    // Text of digits and colons at a scale (a cell is s × s pixels).
    void text(std::string_view s, std::int32_t x, std::int32_t y, std::int32_t scale, Rgb c) {
        for (const char ch : s) {
            const std::size_t g = ch == ':' ? 10 : static_cast<std::size_t>(ch - '0');
            if (g < glyphs.size())
                for (std::int32_t row = 0; row < 5; ++row)
                    for (std::int32_t col = 0; col < 3; ++col)
                        if (glyphs[g][static_cast<std::size_t>(row)] & (4 >> col))
                            fill(x + col * scale, y + row * scale, x + (col + 1) * scale, y + (row + 1) * scale, c);
            x += 4 * scale;
        }
    }

private:
    imageio::Image& img_;
};

constexpr std::int32_t text_width(std::size_t chars, std::int32_t scale) { return static_cast<std::int32_t>(chars) * 4 * scale - scale; }

}  // namespace

bool card_flash(const Timing& timing, std::int64_t index) noexcept {
    const std::int64_t s0 = timing.sample_of(index), s1 = timing.sample_of(index + 1);
    const std::int64_t next = -floor_div(-s0, timing.rate) * timing.rate;  // the first second's start at or after s0
    return next < s1;
}

void draw_card(imageio::Image& img, const Timing& timing, std::int64_t index) {
    img.alpha = imageio::Alpha::straight;
    img.rgba.resize(static_cast<std::size_t>(img.width) * img.height * 4);
    Canvas c(img);
    const std::int32_t w = img.width, h = img.height;
    const std::int32_t bars_bottom = h * 7 / 12, castle_bottom = h * 2 / 3;
    constexpr std::array<Rgb, 7> bars{{{191, 191, 191}, {191, 191, 0}, {0, 191, 191}, {0, 191, 0}, {191, 0, 191}, {191, 0, 0}, {0, 0, 191}}};
    constexpr std::array<Rgb, 7> castles{{{0, 0, 191}, {19, 19, 19}, {191, 0, 191}, {19, 19, 19}, {0, 191, 191}, {19, 19, 19}, {191, 191, 191}}};
    for (std::int32_t i = 0; i < 7; ++i) {
        c.fill(i * w / 7, 0, (i + 1) * w / 7, bars_bottom, bars[static_cast<std::size_t>(i)]);
        c.fill(i * w / 7, bars_bottom, (i + 1) * w / 7, castle_bottom, castles[static_cast<std::size_t>(i)]);
    }
    // The ramp over five sevenths, the flash box over the rest.
    const std::int32_t ramp_right = w * 5 / 7;
    for (std::int32_t i = 0; i < 8; ++i) {
        const auto v = static_cast<std::uint8_t>((7 - i) * 255 / 7);  // white to black: the dark end beside the flash
        c.fill(i * ramp_right / 8, castle_bottom, (i + 1) * ramp_right / 8, h, {v, v, v});
    }
    const bool flash = card_flash(timing, index);
    c.fill(ramp_right, castle_bottom, w, h, flash ? Rgb{255, 255, 255} : Rgb{0, 0, 0});

    // Where in its second this frame is.
    const std::int64_t s0 = timing.sample_of(index);
    const std::int64_t second = floor_div(s0, timing.rate);
    const std::int64_t first = timing.frame_of_sample(second * timing.rate);
    const std::int64_t in_second = index - first;
    const std::int64_t per_second = (timing.fps.num + timing.fps.den - 1) / timing.fps.den;

    // The sweep: black with white edges, so it shows on every bar.
    const std::int32_t lw = std::max(3, w / 96);
    const auto x = static_cast<std::int32_t>(per_second > 1 ? in_second * (w - lw) / (per_second - 1) : 0);
    c.fill(x, 0, x + lw, bars_bottom, {255, 255, 255});
    c.fill(x + 1, 0, x + lw - 1, bars_bottom, {0, 0, 0});

    // The counter.
    const std::int32_t s = std::max(1, h / 60);
    const std::string timecode = std::format("{:02}:{:02}:{:02}:{:02}", second / 3600, second / 60 % 60, second % 60, in_second);
    const std::string number = std::format("{:06}", index);
    const std::int32_t tw = text_width(timecode.size(), s), th = 5 * s;
    const std::int32_t bw = tw + 4 * s, bh = 2 * th + 7 * s;
    const std::int32_t bx = (w - bw) / 2, by = (bars_bottom - bh) / 2;
    c.fill(bx, by, bx + bw, by + bh, {0, 0, 0});
    c.text(timecode, bx + 2 * s, by + 2 * s, s, {235, 235, 235});
    c.text(number, bx + (bw - text_width(number.size(), s)) / 2, by + 5 * s + th, s, {235, 235, 235});
}

Card::Card(const Format& format) : timing_(format.timing) {
    image_.width = format.width;
    image_.height = format.height;
}

std::expected<imageio::View, std::string> Card::frame(std::int64_t index) {
    draw_card(image_, timing_, index);
    return imageio::view(image_);
}

std::vector<float> card_track(const Format& format) {
    if (format.channels <= 0) return {};
    const auto channels = static_cast<std::size_t>(format.channels);
    std::vector<float> out(static_cast<std::size_t>(format.samples()) * channels, 0.0f);
    const Timing& t = format.timing;
    for (std::int64_t i = 0; i < format.frames; ++i) {
        if (!card_flash(t, i)) continue;
        const std::int64_t s0 = t.sample_of(i), n = t.samples_in(i);
        for (std::int64_t k = 0; k < n; ++k) {
            const auto v = static_cast<float>(0.5 * hanim::dm::sin(hanim::dm::two_pi * 1000.0 * static_cast<double>(k) / t.rate));
            for (std::size_t ch = 0; ch < channels; ++ch) out[static_cast<std::size_t>(s0 + k) * channels + ch] = v;
        }
    }
    return out;
}

}  // namespace omui::video
