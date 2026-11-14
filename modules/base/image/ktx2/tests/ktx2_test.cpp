import std;
import mlibs.image;
import mlibs.image.ktx2;
import mlibs.hash;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace image = mlibs::image;

namespace {

// Upstream's own test files (Basis Universal's test_files/): a photograph packed as ETC1S, and one picture packed
// as XUASTC twice — by Zstandard and by the arithmetic coder. In the program itself, so that the same test runs
// where there are no files to read (a page).
constexpr std::uint8_t photograph_file[] = {
#embed "data/kodim23.ktx2"
};
constexpr std::uint8_t by_zstd_file[] = {
#embed "data/base_xuastc_zstd.ktx2"
};
constexpr std::uint8_t by_arith_file[] = {
#embed "data/base_xuastc_arith.ktx2"
};
std::vector<std::uint8_t> file(std::string_view name) {
    const std::span<const std::uint8_t> bytes = name == "kodim23.ktx2" ? std::span<const std::uint8_t>(photograph_file) : name == "base_xuastc_zstd.ktx2" ? std::span<const std::uint8_t>(by_zstd_file) : std::span<const std::uint8_t>(by_arith_file);
    return {bytes.begin(), bytes.end()};
}
std::uint64_t fnv(std::span<const std::uint8_t> bytes) { return mlibs::hash::Fnv1a().bytes(bytes).value(); }

}  // namespace

static UTest cases{"mlibs.image.ktx2", [] {
    "what a KTX2 file is, without reading a picture"_test = [] {
        const std::vector<std::uint8_t> photograph = file("kodim23.ktx2"), packed = file("base_xuastc_zstd.ktx2");
        expect(fatal(!photograph.empty() && !packed.empty())) << "the test's files beside it";
        expect(image::is_ktx2(photograph) && image::is_ktx2(packed));
        const auto first = image::describe_ktx2(photograph);
        expect(fatal(first.has_value())) << (first ? "" : first.error());
        expect(eq(first->width, 768) && eq(first->height, 512) && eq(first->levels, 1) && eq(first->layers, 0) && eq(first->faces, 1));
        expect(first->codec == image::Ktx2Codec::etc1s && !first->alpha && first->srgb);
        const auto second = image::describe_ktx2(packed);
        expect(fatal(second.has_value())) << (second ? "" : second.error());
        expect(eq(second->width, 512) && eq(second->height, 512) && second->codec == image::Ktx2Codec::xuastc);
        expect(eq(image::name(second->codec), std::string_view("xuastc")) && eq(image::name(image::Ktx2Codec::hdr), std::string_view("hdr")));
    };

    "a picture as pixels: the same bytes every time and on every machine"_test = [] {
        const std::vector<std::uint8_t> photograph = file("kodim23.ktx2");
        const auto pixels = image::decode_ktx2(photograph);
        expect(fatal(pixels.has_value())) << (pixels ? "" : pixels.error());
        expect(eq(pixels->width, 768) && eq(pixels->height, 512) && pixels->alpha == image::Alpha::straight && eq(pixels->rgba.size(), std::size_t{768} * 512 * 4));
        expect(eq(fnv(pixels->rgba), 13856374787171296120ull));
        const auto again = image::decode_ktx2(photograph);
        expect(again.has_value() && again->rgba == pixels->rgba);
        // No alpha in the file: every pixel whole. And a photograph: not one colour.
        bool whole = true;
        std::set<std::uint32_t> colours;
        for (std::size_t pixel = 0; pixel < pixels->rgba.size() / 4; ++pixel) {
            whole = whole && pixels->rgba[pixel * 4 + 3] == 255;
            if (pixel % 97 == 0) colours.insert(std::uint32_t{pixels->rgba[pixel * 4]} << 16 | std::uint32_t{pixels->rgba[pixel * 4 + 1]} << 8 | pixels->rgba[pixel * 4 + 2]);
        }
        expect(whole && gt(colours.size(), std::size_t{500}));
        // One picture packed two ways is one picture.
        const auto by_zstd = image::decode_ktx2(file("base_xuastc_zstd.ktx2")), by_arith = image::decode_ktx2(file("base_xuastc_arith.ktx2"));
        expect(fatal(by_zstd.has_value() && by_arith.has_value())) << (by_zstd ? (by_arith ? "" : by_arith.error()) : by_zstd.error());
        expect(eq(fnv(by_zstd->rgba), 12946497234108996751ull) && by_zstd->rgba == by_arith->rgba);
        // Every level the file has: here the one.
        const auto levels = image::decode_ktx2_levels(photograph);
        expect(fatal(levels.has_value())) << (levels ? "" : levels.error());
        expect(eq(levels->size(), std::size_t{1}) && levels->front().rgba == pixels->rgba);
    };

    "a picture as a GPU's blocks: BC7, ETC2 and ASTC, sixteen bytes a block of four by four"_test = [] {
        const std::vector<std::uint8_t> photograph = file("kodim23.ktx2");
        const std::array<std::pair<image::Ktx2Blocks, std::uint64_t>, 3> each{{{image::Ktx2Blocks::bc7, 15766935376129529535ull}, {image::Ktx2Blocks::etc2, 3619060494117872028ull},
                                                                               {image::Ktx2Blocks::astc_4x4, 17062277099759253142ull}}};
        for (const auto& [as, pinned] : each) {
            const auto packed = image::ktx2_blocks(photograph, as);
            expect(fatal(packed.has_value())) << (packed ? "" : packed.error());
            expect(eq(packed->width, 768) && eq(packed->height, 512) && eq(packed->across, 192) && eq(packed->down, 128) && eq(packed->bytes.size(), std::size_t{192} * 128 * 16));
            expect(eq(fnv(packed->bytes), pinned));
        }
    };

    "what is not a KTX2 file, is cut short, or asks for what is not there is refused with its reason"_test = [] {
        const std::vector<std::uint8_t> photograph = file("kodim23.ktx2");
        expect(fatal(gt(photograph.size(), std::size_t{1000})));
        const std::vector<std::uint8_t> noise(4096, 0x5a);
        expect(!image::is_ktx2(noise) && !image::is_ktx2({}));
        const auto of_noise = image::decode_ktx2(noise);
        expect(!of_noise && of_noise.error().contains("not a KTX2 file")) << (of_noise ? "" : of_noise.error());
        expect(!image::describe_ktx2(std::span(photograph).first(40)));
        for (const std::size_t kept : {std::size_t{80}, std::size_t{200}, photograph.size() / 2, photograph.size() - 1}) {
            const auto cut = image::decode_ktx2(std::span(photograph).first(kept));
            expect(!cut.has_value()) << kept;
            expect(!image::ktx2_blocks(std::span(photograph).first(kept), image::Ktx2Blocks::bc7).has_value()) << kept;
        }
        const auto beyond = image::decode_ktx2(photograph, 1);
        expect(!beyond && beyond.error().contains("level 1: the file has 1")) << (beyond ? "" : beyond.error());
        expect(!image::decode_ktx2(photograph, -1));
        expect(!image::load_ktx2("no-such-file.ktx2"));
    };
}};
