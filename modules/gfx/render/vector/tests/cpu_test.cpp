// The CPU backend renders a display list exactly as the raster canvas would,
// at any scale, and records which node drew each pixel.
import std;
import openmui.geom;
import openmui.raster;
import openmui.render.vector;
import openmui.image;
import openmui.testing;

using namespace omui::testing;
namespace render = omui::render;
namespace raster = omui::raster;
namespace geom = omui::geom;

namespace {

void card(render::DisplayList& dl) {
    dl.begin_node(1);
    dl.box_shadow({20, 20, 160, 100}, geom::CornerRadii::all(12), {0, 4}, 16, 0, {0, 0, 0, 0.25f});
    dl.fill_round_rect({20, 20, 160, 100}, geom::CornerRadii::all(12), render::Paint::solid(geom::Color::white()));
    dl.end_node();
    dl.begin_node(2);
    std::array<render::GradientStop, 2> stops{{{0, geom::Color::hex(0x6366f1)}, {1, geom::Color::hex(0xec4899)}}};
    render::Paint g;
    g.kind = render::Paint::Kind::gradient;
    g.gradient.start = {40, 40};
    g.gradient.end = {160, 40};
    g.gradient.stops = stops;
    dl.fill_round_rect({40, 40, 120, 32}, geom::CornerRadii::all(16), g);
    dl.end_node();
    dl.begin_node(3);
    dl.push_transform(geom::Affine::translate(100, 96));
    render::Path check;
    check.move_to({-10, 0}).line_to({-3, 7}).line_to({10, -6});
    dl.stroke_path(check, {.width = 3, .cap = render::LineCap::round, .join = render::LineJoin::round},
                   render::Paint::solid(geom::Color::hex(0x10b981)));
    dl.pop_transform();
    dl.end_node();
}

}  // namespace

int main(int argc, char** argv) {
    Suite s{"render.cpu"};

    s.test("a card at 1x and 2x, with provenance", [] {
        render::DisplayList dl;
        card(dl);
        check(dl.balanced());
        render::CpuRenderer cpu;
        render::ImageStore images;
        raster::Surface one(200, 140), two(400, 280);
        auto report = cpu.render(dl, one, {&images, nullptr}, {.scale = 1, .object_ids = true, .background = geom::Color::hex(0xf1f5f9)});
        check_eq(report.commands, dl.size());
        check_eq(cpu.object_at(100, 56, 200), std::uint32_t{2}, std::source_location::current());
        check_eq(cpu.object_at(30, 110, 200), std::uint32_t{1});
        check_eq(cpu.object_at(5, 5, 200), std::uint32_t{0});
        cpu.render(dl, two, {&images, nullptr}, {.scale = 2, .background = geom::Color::hex(0xf1f5f9)});
        // The same pixel at 2x is the same colour where the colour is flat.
        check(one.at(30, 110) == two.at(60, 220));
        auto dir = scratch_dir("render-cpu");
        omui::imageio::View v1{reinterpret_cast<const std::uint8_t*>(one.pixels().data()), 200, 140, 800};
        omui::imageio::View v2{reinterpret_cast<const std::uint8_t*>(two.pixels().data()), 400, 280, 1600};
        check(omui::imageio::save_png(dir / "card@1x.png", v1).has_value());
        check(omui::imageio::save_png(dir / "card@2x.png", v2).has_value());
        std::println("    {:016x} {:016x}  ({})", one.hash(), two.hash(), (dir / "card@2x.png").generic_string());
        check_eq(std::format("{:016x}/{:016x}", one.hash(), two.hash()), std::string("76f0acb68946e096/09f8ffc1b651f38d"));
    });

    s.test("images by reference; missing ones are reported", [] {
        render::ImageStore images;
        std::vector<raster::Rgba8> px{{255, 0, 0, 255}, {0, 0, 255, 255}};
        auto ref = images.add(2, 1, px);
        render::DisplayList dl;
        dl.draw_image(ref, {0, 0, 2, 1}, {0, 0, 4, 2}, render::Sampling::nearest);
        dl.draw_image({999}, {0, 0, 1, 1}, {0, 0, 1, 1});
        raster::Surface out(4, 2);
        render::CpuRenderer cpu;
        auto report = cpu.render(dl, out, {&images, nullptr});
        check(out.at(0, 0) == px[0] && out.at(3, 1) == px[1]);
        check_eq(report.missing_images, std::size_t{1});
    });

    s.test("a textured mesh: a picture bent by its vertices — flat, it is the image drawn; bent, the same bytes every run", [] {
        render::ImageStore images;
        std::vector<raster::Rgba8> px(32 * 32);
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 32; ++x)
                px[static_cast<std::size_t>(y * 32 + x)] = {static_cast<std::uint8_t>(x * 8), static_cast<std::uint8_t>(y * 8), 160, 255};
        const auto ref = images.add(32, 32, px);
        // A 3 × 3 grid of vertices over the image, drawn at twice its size.
        std::vector<geom::Point> uv, at;
        std::vector<std::uint16_t> tri;
        for (int j = 0; j <= 2; ++j)
            for (int i = 0; i <= 2; ++i) {
                uv.push_back({16.0f * i, 16.0f * j});
                at.push_back({8 + 32.0f * i, 8 + 32.0f * j});
            }
        for (std::uint16_t j = 0; j < 2; ++j)
            for (std::uint16_t i = 0; i < 2; ++i) {
                const std::uint16_t k = static_cast<std::uint16_t>(j * 3 + i);
                for (std::uint16_t v : {k, static_cast<std::uint16_t>(k + 1), static_cast<std::uint16_t>(k + 4), k, static_cast<std::uint16_t>(k + 4), static_cast<std::uint16_t>(k + 3)})
                    tri.push_back(v);
            }
        render::CpuRenderer cpu;
        render::DisplayList flat, image;
        flat.draw_mesh(ref, at, uv, tri, render::Sampling::nearest);
        image.draw_image(ref, {0, 0, 32, 32}, {8, 8, 64, 64}, render::Sampling::nearest);
        raster::Surface a(80, 80), b(80, 80);
        cpu.render(flat, a, {&images, nullptr});
        cpu.render(image, b, {&images, nullptr});
        int differ = 0;
        for (int y = 10; y < 70; ++y)
            for (int x = 10; x < 70; ++x) differ += !(a.at(x, y) == b.at(x, y));
        check_eq(differ, 0);  // inside, where no edge's antialiasing reaches: the image itself, no cracks along the diagonals
        // Bent: the middle row pushed right, as a bone would.
        for (int i = 3; i < 6; ++i) at[static_cast<std::size_t>(i)].x += 10;
        render::DisplayList bent;
        bent.draw_mesh(ref, at, uv, tri);
        raster::Surface c(90, 80);
        cpu.render(bent, c, {&images, nullptr});
        std::println("    bent {:016x}", c.hash());
        check(c.at(50, 40) != b.at(50, 40), "moved");
        check_eq(bent.hash(), [&] {
            render::DisplayList again;
            again.draw_mesh(ref, at, uv, tri);
            return again.hash();
        }());
    });

    s.test("unregistered effects and external textures are skipped and counted (R-3)", [] {
        render::DisplayList dl;
        std::array<float, 1> params{0.5f};
        dl.push_layer(1, render::BlendMode::normal, {999}, params);  // no effect has this ref
        dl.fill_rect({0, 0, 4, 4}, geom::Color::white());
        dl.pop_layer();
        dl.external_texture(42, {0, 0, 2, 2});
        raster::Surface out(4, 4);
        render::CpuRenderer cpu;
        auto report = cpu.render(dl, out, {});
        check_eq(report.effects_skipped, std::size_t{1});
        check_eq(report.external_skipped, std::size_t{1});
        check_eq(int(out.at(1, 1).a), 255, std::source_location::current());
    });

    return s.run(argc, argv);
}
