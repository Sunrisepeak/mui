// mlibs.image.ktx2 — KTX2 textures read: what a file is, its pictures as pixels (an `Image`, as PNG's and
// JPEG's are), and the same pictures as a GPU's own blocks. The codecs are Basis Universal's (ETC1S, UASTC,
// ASTC, XUASTC; with or without Zstandard), transcoded by its library (src/native/basis.cpp). Nothing of a file
// is trusted: one that is cut short, is not a KTX2 file, or holds what is not read here is refused with its
// reason. The same file gives the same bytes on every machine.
export module mlibs.image.ktx2;

import std;
import mlibs.image;

export namespace mlibs::image {

// How a KTX2 file's pictures are packed.
enum class Ktx2Codec : std::uint8_t {
    etc1s,   // small: a photograph in about a bit a pixel
    uastc,   // 4 × 4 blocks of 16 bytes, near what BC7 and ASTC hold
    astc,    // ASTC's own blocks, 4 × 4 to 12 × 12
    xuastc,  // ASTC packed again (Zstandard or an arithmetic coder)
    hdr,     // light beyond a display's white: not read here yet
};
[[nodiscard]] std::string_view name(Ktx2Codec codec) noexcept;

struct Ktx2 {
    int       width = 0, height = 0;  // of the largest picture
    int       levels = 1;             // pictures, each half the one before
    int       layers = 0;             // 0: not an array
    int       faces = 1;              // 6: a cube
    Ktx2Codec codec = Ktx2Codec::etc1s;
    bool      alpha = false;          // the file says its pictures hide by a fourth channel
    bool      srgb = true;            // its numbers are colours through a display's curve; false: plain numbers
};

// A GPU's own packing, each a 4 × 4 block in 16 bytes: what a device takes as it is.
enum class Ktx2Blocks : std::uint8_t { bc7, etc2, astc_4x4 };
struct Ktx2Packed {
    int                       width = 0, height = 0;  // pixels of the picture
    int                       across = 0, down = 0;   // blocks
    std::vector<std::uint8_t> bytes;                  // across × down × 16, a row of blocks after a row from the top
};

// Whether the bytes begin as a KTX2 file does (its twelve bytes of identifier).
[[nodiscard]] bool is_ktx2(std::span<const std::uint8_t> bytes) noexcept;
// What the file is, without reading a picture.
[[nodiscard]] std::expected<Ktx2, std::string> describe_ktx2(std::span<const std::uint8_t> bytes);
// A picture as pixels (straight alpha; whole where the file has none): `level` 0 the largest. Of an array or a
// cube, the first layer's first face.
[[nodiscard]] std::expected<Image, std::string> decode_ktx2(std::span<const std::uint8_t> bytes, int level = 0);
// Every picture the file has, the largest first.
[[nodiscard]] std::expected<std::vector<Image>, std::string> decode_ktx2_levels(std::span<const std::uint8_t> bytes);
// A picture as a GPU's blocks.
[[nodiscard]] std::expected<Ktx2Packed, std::string> ktx2_blocks(std::span<const std::uint8_t> bytes, Ktx2Blocks as, int level = 0);
[[nodiscard]] std::expected<Image, std::string> load_ktx2(const std::filesystem::path& file);

}  // namespace mlibs::image
