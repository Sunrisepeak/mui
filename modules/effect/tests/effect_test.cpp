// A program's own effect: read and checked, compiled for the device,
// registered, drawn by the GPU renderer, and loaded again — within 500 ms —
// when its shader changes (M4 acceptance ③), without a line of C++.
import std;
import mcpplibs.os;
import openmui.geom;
import openmui.raster;
import openmui.render;
import openmui.rhi;
import openmui.rhi.sdl;
import openmui.effect;
import openmui.testing;

using namespace omui::testing;
namespace fx = omui::effect;
namespace render = omui::render;
namespace raster = omui::raster;
namespace geom = omui::geom;
namespace rhi = omui::rhi;

namespace {

std::filesystem::path data(std::string_view name, std::source_location here = std::source_location::current()) {
    return std::filesystem::path(here.file_name()).parent_path() / "data" / name;
}

bool required() { return mcpplibs::os::env::get("OMUI_REQUIRE_GPU") == "1"; }

std::unique_ptr<rhi::Device> open_device() {
    rhi::sdl::Options o;
    o.formats = render::gpu_shader_formats();
    if (auto lib = mcpplibs::os::env::get("OMUI_VULKAN_LIBRARY")) o.vulkan_library = *lib;
    if (auto video = mcpplibs::os::env::get("OMUI_GPU_VIDEO_DRIVER")) o.video_driver = *video;
    auto d = rhi::sdl::create(o);
    if (!d) {
        if (required()) check(false, std::format("OMUI_REQUIRE_GPU=1 and no device: {}", d.error().message));
        else std::println("no GPU device here ({}); set OMUI_REQUIRE_GPU=1 to make that a failure", d.error().message);
        return nullptr;
    }
    return std::move(*d);
}

std::string problems(const fx::Diagnostics& ds) {
    std::string out;
    for (const auto& d : ds) out += d.text() + "\n";
    return out;
}

// A scratch copy of the fixtures: the reload test edits them.
std::filesystem::path scratch() {
    const auto dir = std::filesystem::temp_directory_path() / std::format("openmui-effect-test-{}", std::hash<std::thread::id>{}(std::this_thread::get_id()));
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    for (std::string_view f : {"scanlines.effect.omui.toml", "scanlines.slang"}) std::filesystem::copy_file(data(f), dir / f, ec);
    return dir;
}

void write(const std::filesystem::path& f, std::string_view text) {
    // A later modification time than the load saw, whatever the clock's grain.
    const auto before = std::filesystem::last_write_time(f);
    std::ofstream(f, std::ios::binary) << text;
    std::filesystem::last_write_time(f, std::max(std::filesystem::last_write_time(f), before + std::chrono::seconds(1)));
}

std::string text_of(const std::filesystem::path& f) {
    std::ifstream in(f, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

constexpr std::uint32_t W = 64, H = 48;
constexpr geom::Rect layer{16, 12, 32, 24};

// An opaque layer under the effect, over white.
render::DisplayList scene(render::EffectRef ref, float strength) {
    render::DisplayList dl;
    dl.fill_rect({0, 0, W, H}, geom::Color::white());
    const std::array<float, 2> params{strength, 2};
    dl.push_layer(1, render::BlendMode::normal, ref, params, layer);
    dl.fill_rect(layer, geom::Color::hex(0x2563eb));
    dl.pop_layer();
    return dl;
}

struct Gpu {
    std::unique_ptr<rhi::Device> device;
    std::optional<render::GpuRenderer> renderer;
    rhi::Texture target;

    static std::optional<Gpu> open() {
        Gpu g;
        g.device = open_device();
        if (!g.device) return std::nullopt;
        auto r = render::GpuRenderer::create(*g.device);
        check(r.has_value(), r ? "" : r.error().message);
        if (!r) return std::nullopt;
        g.renderer.emplace(std::move(*r));
        auto t = g.device->create(rhi::TextureDesc{rhi::Format::rgba8_unorm, W, H, rhi::TextureUsage::color_target, "test.target"});
        if (!t) return std::nullopt;
        g.target = *t;
        return g;
    }

    std::vector<std::byte> draw(const render::DisplayList& dl) {
        auto report = renderer->render(dl, target, {});
        check(report.has_value(), report ? "" : report.error().message);
        if (report) check_eq(report->effects_skipped, std::size_t{0});
        auto bytes = device->read(target, rhi::Region{0, 0, W, H});
        check(bytes.has_value());
        return bytes ? std::move(*bytes) : std::vector<std::byte>{};
    }
};

int channel(const std::vector<std::byte>& px, std::uint32_t x, std::uint32_t y, int c) {
    return std::to_integer<int>(px[(static_cast<std::size_t>(y) * W + x) * 4 + static_cast<std::size_t>(c)]);
}

// The layer's rows as the effect makes them: the surface starts a pixel
// above the layer (reach 0, plus the renderer's pixel), so the scanlines are
// the layer's rows y with (y − 11) % 2 == 0; a dark row is the blue scaled.
void expect_scanlines(const std::vector<std::byte>& px, float strength, std::string_view what) {
    if (px.empty()) return;
    const raster::Rgba8 blue = raster::premultiply(geom::Color::hex(0x2563eb));
    for (std::uint32_t y = 12; y < 36; ++y) {
        const bool dark = (y - 11) % 2 == 0;
        // The CPU reference's bytes: q8(channel · (1 − strength)).
        auto q8 = [&](std::uint8_t v) { return static_cast<int>(std::clamp(v * (1 - strength), 0.0f, 255.0f) + 0.5f); };
        const int r = dark ? q8(blue.r) : blue.r, g = dark ? q8(blue.g) : blue.g, b = dark ? q8(blue.b) : blue.b, a = dark ? q8(blue.a) : blue.a;
        // Over white: what the dark row's translucency lets through.
        const int inv = 255 - a;
        auto over = [&](int s) { return s + static_cast<int>(raster::mul255(255, static_cast<std::uint32_t>(inv))); };
        const int x = 30;
        check(std::abs(channel(px, x, y, 0) - over(r)) <= 1 && std::abs(channel(px, x, y, 1) - over(g)) <= 1 && std::abs(channel(px, x, y, 2) - over(b)) <= 1,
              std::format("{}: row {} is ({}, {}, {}), not ({}, {}, {})", what, y, channel(px, x, y, 0), channel(px, x, y, 1), channel(px, x, y, 2),
                          over(r), over(g), over(b)));
    }
}

}  // namespace

int main(int argc, char** argv) {
    Suite s{"effect"};

    s.test("an effect file: what it declares, and every mistake where it is", [] {
        auto ok = fx::read(text_of(data("scanlines.effect.omui.toml")), "scanlines.effect.omui.toml");
        check(ok.has_value(), ok ? "" : problems(ok.error()));
        if (ok) {
            check(ok->name == "scanlines" && ok->shader == "scanlines.slang" && ok->entry == "scanlines_fs");
            check_eq(ok->params.size(), std::size_t{2});
            check(ok->params[0].name == "strength" && ok->params[0].value == 0.5f && ok->params[1].max == 16);
        }
        auto bad = fx::read(R"(name = "Scan lines"
reahc = 2
[[param]]
name = "strength"
default = 2
[[param]]
name = "strength"
)",
                            "bad.effect.omui.toml");
        check(!bad.has_value());
        if (!bad) {
            const std::string all = problems(bad.error());
            check(all.contains("bad.effect.omui.toml:1:") && all.contains("not an effect name"), all);
            check(all.contains("no key `reahc`") && all.contains("did you mean `reach`?"), all);
            check(all.contains("the default 2 is outside [0, 1]"), all);
            check(all.contains("a second parameter named `strength`"), all);
        }
        auto reach = fx::read("name = \"x\"\nreach = \"spread\"\n", "x.effect.omui.toml");
        check(!reach && problems(reach.error()).contains("`reach` names `spread`, which is not a parameter"));
        auto broken = fx::read("name = \"x\"\n[param\n", "x.effect.omui.toml");
        check(!broken && broken.error().front().line == 2, broken ? "" : problems(broken.error()));
    });

    s.test("loading: compiled for the device, registered, refused when it binds more than the template", [] {
        fx::Library lib({.formats = render::gpu_shader_formats()});
        auto loaded = lib.load(data("scanlines.effect.omui.toml"));
        check(loaded.has_value(), loaded ? "" : problems(loaded.error()));
        if (!loaded) return;
        const auto found = render::effects().find("scanlines");
        check(found && found->id == loaded->ref.id);
        const render::EffectInfo* info = render::effects().info(loaded->ref);
        check(info && info->gpu.pass == render::EffectPass::pixel && info->gpu.fragment && !info->cpu);
        if (info) check(info->params.size() == 2 && info->params[1].name == "period" && info->doc.contains("CRT"));
        std::println("  load (read, compile, register): {} ms", loaded->took.count() / 1000.0);
        auto greedy = lib.load(data("greedy.effect.omui.toml"));
        check(!greedy && problems(greedy.error()).contains("binds resources of its own"), greedy ? "" : problems(greedy.error()));
        auto taken = lib.load(data("blur.effect.omui.toml"));
        check(!taken && problems(taken.error()).contains("an effect named `blur` is registered already"), taken ? "" : problems(taken.error()));
        // The CPU renderer has no path for it: drawn without it, counted.
        raster::Surface cpu(W, H);
        render::CpuRenderer r;
        const auto report = r.render(scene(loaded->ref, 0.5f), cpu, {}, {});
        check_eq(report.effects_skipped, std::size_t{1});
    });

    s.test("drawn by the GPU renderer, and loaded again within 500 ms when its shader changes", [] {
        auto gpu = Gpu::open();
        if (!gpu) return;
        const auto dir = scratch();
        fx::Library lib({.formats = gpu->device->caps().formats});
        auto loaded = lib.load(dir / "scanlines.effect.omui.toml");
        check(loaded.has_value(), loaded ? "" : problems(loaded.error()));
        if (!loaded) return;
        expect_scanlines(gpu->draw(scene(loaded->ref, 0.5f)), 0.5f, "as written");
        check(lib.poll().empty(), "nothing changed, nothing reloaded");

        // The shader edited while the program runs: scanlines at full strength.
        const auto shader = dir / "scanlines.slang";
        std::string src = text_of(shader);
        src.replace(src.find("scaled(s, 1 - param(0))"), std::string_view("scaled(s, 1 - param(0))").size(), "scaled(s, 0.0)");
        write(shader, src);
        auto reloads = lib.poll();
        check_eq(reloads.size(), std::size_t{1});
        if (reloads.size() == 1) {
            check(reloads[0].ok(), problems(reloads[0].problems));
            check(reloads[0].ref.id == loaded->ref.id, "the same effect, replaced in place");
            std::println("  reload: {} ms", reloads[0].took.count() / 1000.0);
            check(reloads[0].took < std::chrono::milliseconds(500), std::format("a reload takes {} ms; M4 asks for under 500", reloads[0].took.count() / 1000.0));
        }
        expect_scanlines(gpu->draw(scene(loaded->ref, 0.5f)), 1.0f, "edited");

        // A mistake: reported where it is; the effect draws as it was.
        write(shader, text_of(shader) + "\nfloat broken( {\n");
        reloads = lib.poll();
        check(reloads.size() == 1 && !reloads[0].ok(), "a broken shader is reported");
        if (reloads.size() == 1 && !reloads[0].ok()) {
            const auto& d = reloads[0].problems.front();
            check(d.file.ends_with("scanlines.slang") && d.line > 10, d.text());
        }
        expect_scanlines(gpu->draw(scene(loaded->ref, 0.5f)), 1.0f, "after a broken edit");
        check(lib.poll().empty(), "a broken file is not retried until it changes again");

        // The parameters edited: a reload of the file alone.
        write(shader, src);
        std::string decl = text_of(dir / "scanlines.effect.omui.toml");
        decl.replace(decl.find("max     = 16"), std::string_view("max     = 16").size(), "max     = 32");
        write(dir / "scanlines.effect.omui.toml", decl);
        reloads = lib.poll();
        check(reloads.size() == 1 && reloads[0].ok(), reloads.empty() ? "" : problems(reloads[0].problems));
        const render::EffectInfo* info = render::effects().info(loaded->ref);
        check(info && info->params.size() == 2 && info->params[1].max == 32);
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    });

    return s.run(argc, argv);
}
