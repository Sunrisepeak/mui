// openmui.video:picture — pictures as a video codec takes them: Y'CbCr
// 4:2:0, BT.709, limited range (Y' 16–235, Cb and Cr 16–240), the colour
// HD video and every browser expect.
//
// The conversion is integer arithmetic (16-bit fixed point; the chroma of
// each 2 × 2 block from the sum of its four pixels, which is where 4:2:0's
// centred chroma lies), so the same pixels give the same bytes everywhere:
// black is 16, white 235, every grey has Cb = Cr = 128 exactly. Pixels that
// are not opaque count as over black (premultiplied, they already are).
export module openmui.video:picture;

import std;
import hanim;
import openmui.image;

export namespace omui::video {

// The three planes of a 4:2:0 picture: Y' at every pixel, Cb and Cr at each
// 2 × 2 block (an odd edge's block is its one or two pixels, repeated).
struct Yuv420 {
    std::int32_t              width = 0, height = 0;
    std::vector<std::uint8_t> y, u, v;  // u is Cb, v is Cr

    [[nodiscard]] std::int32_t chroma_width() const noexcept { return (width + 1) / 2; }
    [[nodiscard]] std::int32_t chroma_height() const noexcept { return (height + 1) / 2; }
    // Its bytes as a Y4M frame or an I420 buffer has them: Y', then Cb, then Cr.
    [[nodiscard]] std::size_t bytes() const noexcept { return y.size() + u.size() + v.size(); }
    bool operator==(const Yuv420&) const = default;
};

// Into `out` (its planes resized, reused from frame to frame).
void to_yuv420(const imageio::View& pixels, Yuv420& out);
[[nodiscard]] Yuv420 to_yuv420(const imageio::View& pixels);
// Back to opaque RGBA (each block's chroma for its four pixels), for
// looking at and comparing what a codec gives back.
[[nodiscard]] imageio::Image to_rgba(const Yuv420& picture);

// How alike two pictures of one size are: the peak signal-to-noise ratio
// of their Y' planes (and of all three), in dB; identical: infinity.
struct Psnr {
    double y = 0, all = 0;
};
[[nodiscard]] Psnr psnr(const Yuv420& a, const Yuv420& b);

// A Y4M file (YUV4MPEG2) read back: its size, rate and frames. Only what
// the Y4M encoder writes is read (4:2:0, 8 bits, progressive).
struct Y4m {
    std::int32_t        width = 0, height = 0;
    hanim::FrameRate    fps{30, 1};
    std::string         colour;  // its C tag ("420jpeg")
    std::vector<Yuv420> frames;
};
[[nodiscard]] std::expected<Y4m, std::string> read_y4m(std::span<const std::byte> file);
// Its header line, as the encoder writes it (BT.709 limited range is said in
// the X tags ffmpeg and others read).
[[nodiscard]] std::string y4m_header(std::int32_t width, std::int32_t height, hanim::FrameRate fps);

// FNV-1a 64 over bytes: what the tools report and the tests pin.
[[nodiscard]] std::uint64_t hash(std::span<const std::byte> bytes) noexcept;
[[nodiscard]] std::uint64_t hash(std::span<const std::uint8_t> bytes) noexcept;

}  // namespace omui::video
