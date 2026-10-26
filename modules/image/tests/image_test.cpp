import std;
import openmui.image;
import openmui.testing;

using namespace omui::testing;
namespace image = omui::image;

int main(int argc, char** argv) {
    Suite s{"image"};

    s.test("PNG round trip keeps every pixel", [] {
        image::Image img;
        img.width = 37;
        img.height = 19;
        img.rgba.resize(37 * 19 * 4);
        for (std::size_t i = 0; i < img.rgba.size(); ++i) img.rgba[i] = static_cast<std::uint8_t>((i * 131) ^ (i >> 3));
        auto png = image::encode_png(image::view(img));
        check(png.has_value());
        check(png->size() > 8 && (*png)[1] == 'P' && (*png)[2] == 'N' && (*png)[3] == 'G');
        auto back = image::decode_png(*png);
        check(back.has_value());
        check(back->width == 37 && back->height == 19);
        check(back->rgba == img.rgba);
    });

    s.test("premultiplied pixels are written straight", [] {
        image::Image img;
        img.width = 1;
        img.height = 1;
        img.alpha = image::Alpha::premultiplied;
        img.rgba = {64, 0, 0, 128};  // 50% of #800000 premultiplied
        auto back = image::decode_png(*image::encode_png(image::view(img)));
        check(back->rgba == std::vector<std::uint8_t>{128, 0, 0, 128});
        image::premultiply(*back);
        check(back->rgba == std::vector<std::uint8_t>{64, 0, 0, 128});
    });

    s.test("files and errors", [] {
        auto dir = scratch_dir("image");
        image::Image img;
        img.width = 2;
        img.height = 2;
        img.rgba = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
        check(image::save_png(dir / "a.png", image::view(img)).has_value());
        auto back = image::load_png(dir / "a.png");
        check(back && back->rgba == img.rgba);
        std::array<std::uint8_t, 4> junk{1, 2, 3, 4};
        auto bad = image::decode_png(junk);
        check(!bad.has_value() && bad.error().starts_with("png:"));
        check(!image::load_png(dir / "missing.png").has_value());
    });

    return s.run(argc, argv);
}
