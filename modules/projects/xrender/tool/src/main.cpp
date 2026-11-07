// xrender — the renderer without a GUI, for people, agents and CI.
//
//   xrender effect check FILE…                   each effect read, its shader compiled for the
//                                                devices' formats, registered: every problem at
//                                                its file, line and column
//   xrender effect preview FILE --out PNG [--image PNG] [--size WxH] [--param NAME=VALUE …]
//                                                the effect drawn by the GPU renderer over a
//                                                picture (a test card when none): before | after
//   xrender cts [--report FILE]                  the render contract's conformance suite on this
//                                                machine's device, case by case
//   xrender device                               the device: backend, name, formats, limits
//
// One JSON answer on standard output; exit 0 done, 1 failed (said why), 2 a
// wrong command. The device is SDL_GPU's (OMUI_VULKAN_LIBRARY and
// OMUI_GPU_VIDEO_DRIVER as the tests take them: lavapipe headless works).
import std;
import mlibs.geom;
import mlibs.image;
import mlibs.rhi;
import mlibs.cts;
import xrender.raster;
import xrender.vector;
import xrender.effect;
import xrender.cts;
import xrender.rhi.sdl;
import mcpplibs.os;
import nlohmann.json;

namespace {

namespace fx = xrender::effect;
namespace geom = mlibs::geom;
namespace raster = xrender::raster;
namespace rhi = mlibs::rhi;
using Json = nlohmann::json;

constexpr std::string_view usage =
    "usage: xrender effect check FILE… | xrender effect preview FILE --out PNG [--image PNG] [--size WxH] [--param NAME=VALUE …] | "
    "xrender cts [--report FILE] | xrender device";

int answer(const Json& json, int code) {
    std::println("{}", json.dump());
    return code;
}
int wrong(std::string_view text) { return answer(Json{{"ok", false}, {"error", text}, {"usage", usage}}, 2); }
int failed(std::string_view text) { return answer(Json{{"ok", false}, {"error", text}}, 1); }

std::filesystem::path path_of(std::string_view text) { return mcpplibs::os::fs::from_utf8(text); }
std::string utf8(const std::filesystem::path& path) {
    const std::u8string u = path.generic_u8string();
    return {reinterpret_cast<const char*>(u.data()), u.size()};
}

std::expected<std::unique_ptr<rhi::Device>, std::string> open_device() {
    xrender::rhi::sdl::Options options;
    options.formats = xrender::gpu_shader_formats();
    if (auto library = mcpplibs::os::env::get("OMUI_VULKAN_LIBRARY")) options.vulkan_library = *library;
    if (auto video = mcpplibs::os::env::get("OMUI_GPU_VIDEO_DRIVER")) options.video_driver = *video;
    auto device = xrender::rhi::sdl::create(options);
    if (!device) return std::unexpected(std::format("no GPU device: {}", device.error().message));
    return std::move(*device);
}

Json problems_of(const fx::Diagnostics& diagnostics) {
    Json list = Json::array();
    for (const auto& d : diagnostics)
        list.push_back({{"file", d.file}, {"line", d.line}, {"column", d.column}, {"message", d.message}, {"hint", d.hint}});
    return list;
}

Json describe(const xrender::EffectInfo& info) {
    Json params = Json::array();
    for (const auto& p : info.params) params.push_back({{"name", std::string(p.name)}, {"default", p.value}, {"min", p.min}, {"max", p.max}});
    return {{"doc", std::string(info.doc)}, {"params", std::move(params)}};
}

int effect_check(int argc, char** argv) {
    if (argc < 4) return wrong("effect check takes one file or more");
    fx::Library library({.formats = xrender::gpu_shader_formats()});
    Json effects = Json::array();
    bool ok = true;
    for (int index = 3; index < argc; ++index) {
        const auto file = path_of(argv[index]);
        Json one{{"file", utf8(file)}};
        auto loaded = library.load(file);
        if (!loaded) {
            ok = false;
            one["ok"] = false;
            one["problems"] = problems_of(loaded.error());
        } else {
            one["ok"] = true;
            one["name"] = loaded->name;
            one["compile_ms"] = static_cast<double>(loaded->took.count()) / 1000;
            if (const auto* info = xrender::effects().info(loaded->ref)) one.update(describe(*info));
        }
        effects.push_back(std::move(one));
    }
    return answer(Json{{"ok", ok}, {"effects", std::move(effects)}}, ok ? 0 : 1);
}

// A picture to show an effect on when none is given: bands of colour, a
// gradient, rounded shapes, fine lines — edges, flats and detail.
void test_card(xrender::DisplayList& list, float width, float height) {
    list.fill_rect({0, 0, width, height}, geom::Color::hex(0xf4f1ea));
    const std::array bands{0xe4572e, 0xf3a712, 0x29335c, 0x669bbc, 0x2a9d8f, 0x8338ec};
    for (std::size_t k = 0; k < bands.size(); ++k)
        list.fill_rect({width * static_cast<float>(k) / bands.size(), 0, width / bands.size() + 0.5f, height * 0.3f},
                       geom::Color::hex(static_cast<std::uint32_t>(bands[k])));
    for (int k = 0; k < 32; ++k) {
        const float t = static_cast<float>(k) / 31;
        list.fill_rect({width * t, height * 0.3f, width / 31 + 0.5f, height * 0.12f}, geom::Color{t, t, t, 1});
    }
    list.fill_round_rect({width * 0.08f, height * 0.5f, width * 0.36f, height * 0.4f}, geom::CornerRadii::all(height * 0.08f),
                         xrender::Paint::solid(geom::Color::hex(0x264653)));
    xrender::Path circle;
    circle.add_circle({width * 0.68f, height * 0.7f}, height * 0.2f);
    list.fill_path(circle, xrender::FillRule::nonzero, xrender::Paint::solid(geom::Color::hex(0xe76f51)));
    for (int k = 0; k < 12; ++k)
        list.fill_rect({width * 0.08f, height * 0.5f + height * 0.4f * k / 12, width * 0.36f, 1}, geom::Color{1, 1, 1, 0.5f});
}

int effect_preview(int argc, char** argv) {
    if (argc < 4) return wrong("effect preview takes a file");
    const auto file = path_of(argv[3]);
    std::optional<std::filesystem::path> out, image_file;
    int width = 320, height = 200;
    std::vector<std::pair<std::string, float>> given;
    for (int index = 4; index < argc; ++index) {
        const std::string_view option = argv[index];
        if (index + 1 >= argc) return wrong(std::format("{} takes a value", option));
        const std::string_view value = argv[++index];
        if (option == "--out") out = path_of(value);
        else if (option == "--image") image_file = path_of(value);
        else if (option == "--size") {
            const auto x = value.find('x');
            if (x == std::string_view::npos || std::from_chars(value.data(), value.data() + x, width).ec != std::errc{} ||
                std::from_chars(value.data() + x + 1, value.data() + value.size(), height).ec != std::errc{} || width < 8 || height < 8 ||
                width > 4096 || height > 4096)
                return wrong("--size is WxH, 8 … 4096 each");
        } else if (option == "--param") {
            const auto eq = value.find('=');
            float number = 0;
            if (eq == std::string_view::npos || std::from_chars(value.data() + eq + 1, value.data() + value.size(), number).ec != std::errc{})
                return wrong("--param is NAME=VALUE");
            given.emplace_back(std::string(value.substr(0, eq)), number);
        } else {
            return wrong(std::format("no option `{}` here", option));
        }
    }
    if (!out) return wrong("effect preview takes --out PNG");
    auto device = open_device();
    if (!device) return failed(device.error());
    fx::Library library({.formats = (*device)->caps().formats});
    auto loaded = library.load(file);
    if (!loaded) return answer(Json{{"ok", false}, {"problems", problems_of(loaded.error())}}, 1);
    const auto* info = xrender::effects().info(loaded->ref);
    if (!info) return failed("the effect was not registered");
    std::vector<float> params;
    for (const auto& p : info->params) params.push_back(p.value);
    for (const auto& [name, value] : given) {
        const auto found = std::ranges::find(info->params, name, &xrender::EffectParam::name);
        if (found == info->params.end()) return wrong(std::format("`{}` has no parameter `{}`", loaded->name, name));
        params[static_cast<std::size_t>(found - info->params.begin())] = std::clamp(value, found->min, found->max);
    }
    xrender::ImageStore images;
    std::optional<xrender::ImageRef> picture;
    if (image_file) {
        auto decoded = mlibs::image::load_png(*image_file);
        if (!decoded) return failed(std::format("cannot read {}: {}", utf8(*image_file), decoded.error()));
        width = decoded->width;
        height = decoded->height;
        std::vector<raster::Rgba8> pixels(static_cast<std::size_t>(width) * height);
        for (std::size_t k = 0; k < pixels.size(); ++k) {
            const auto* p = decoded->rgba.data() + k * 4;
            pixels[k] = decoded->alpha == mlibs::image::Alpha::premultiplied
                            ? raster::Rgba8{p[0], p[1], p[2], p[3]}
                            : raster::premultiply(geom::Color{p[0] / 255.f, p[1] / 255.f, p[2] / 255.f, p[3] / 255.f});
        }
        picture = images.add(width, height, std::move(pixels));
    }
    const auto w = static_cast<float>(width), h = static_cast<float>(height);
    const auto content = [&](xrender::DisplayList& list) {
        if (picture) list.draw_image(*picture, {0, 0, w, h}, {0, 0, w, h});
        else test_card(list, w, h);
    };
    xrender::DisplayList before, after;
    content(before);
    after.fill_rect({0, 0, w, h}, geom::Color::white());
    after.push_layer(1, xrender::BlendMode::normal, loaded->ref, params, {0, 0, w, h});
    content(after);
    after.pop_layer();
    auto renderer = xrender::GpuRenderer::create(**device);
    if (!renderer) return failed(std::format("no renderer: {}", renderer.error().message));
    auto target = (*device)->create(rhi::TextureDesc{rhi::Format::rgba8_unorm, static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height),
                                                     rhi::TextureUsage::color_target | rhi::TextureUsage::sampled, "xrender.preview"});
    if (!target) return failed("no target texture");
    std::vector<std::uint8_t> strip(static_cast<std::size_t>(width) * 2 * height * 4);
    Json reports = Json::array();
    for (int side = 0; side < 2; ++side) {
        auto report = renderer->render(side ? after : before, *target, {&images, nullptr}, {});
        if (!report) return failed(std::format("the render failed: {}", report.error().message));
        if (report->effects_skipped) return failed("the renderer skipped the effect");
        auto bytes = (*device)->read(*target, rhi::Region{0, 0, static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)});
        if (!bytes) return failed(std::format("read back: {}", bytes.error().message));
        for (int y = 0; y < height; ++y)
            std::memcpy(strip.data() + (static_cast<std::size_t>(y) * width * 2 + static_cast<std::size_t>(side) * width) * 4,
                        bytes->data() + static_cast<std::size_t>(y) * width * 4, static_cast<std::size_t>(width) * 4);
        reports.push_back({{"commands", report->commands}, {"uploaded", report->uploaded}});
    }
    if (!mlibs::image::save_png(*out, mlibs::image::View{strip.data(), width * 2, height, width * 8, mlibs::image::Alpha::premultiplied}))
        return failed(std::format("cannot write {}", utf8(*out)));
    Json used = Json::object();
    for (std::size_t k = 0; k < params.size(); ++k) used[std::string(info->params[k].name)] = params[k];
    return answer(Json{{"ok", true}, {"effect", loaded->name}, {"params", std::move(used)}, {"out", utf8(*out)}, {"size", {width, height}},
                       {"layout", "before | after"}, {"device", (*device)->caps().device}, {"renders", std::move(reports)}},
                  0);
}

int cts(int argc, char** argv) {
    std::optional<std::filesystem::path> report_file;
    for (int index = 2; index < argc; ++index) {
        if (std::string_view(argv[index]) == "--report" && index + 1 < argc) report_file = path_of(argv[++index]);
        else return wrong(std::format("no option `{}` here", argv[index]));
    }
    auto device = open_device();
    if (!device) return failed(device.error());
    const auto scratch = std::filesystem::temp_directory_path() / "xrender-cts";
    const auto report = xrender::cts::run(**device, {.subject = (*device)->caps().device, .platform = "sdl"}, {}, {.scratch = scratch});
    if (report_file && !mcpplibs::os::fs::write_atomic(*report_file, report.json() + "\n")) return failed(std::format("cannot write {}", utf8(*report_file)));
    Json failures = Json::array(), unsupported = Json::array();
    for (const auto& outcome : report.outcomes) {
        if (outcome.status == mlibs::cts::Status::failed) failures.push_back(outcome.id);
        if (outcome.status == mlibs::cts::Status::unsupported) unsupported.push_back(outcome.id);
    }
    return answer(Json{{"ok", report.ok()},
                       {"device", (*device)->caps().device},
                       {"passed", report.count(mlibs::cts::Status::passed)},
                       {"failed", std::move(failures)},
                       {"unsupported", std::move(unsupported)},
                       {"pictures", utf8(scratch)},
                       {"report", report_file ? Json(utf8(*report_file)) : Json(nullptr)}},
                  report.ok() ? 0 : 1);
}

int device_info() {
    auto device = open_device();
    if (!device) return failed(device.error());
    const auto& caps = (*device)->caps();
    Json formats = Json::array();
    for (const auto format : {rhi::ShaderFormat::spirv, rhi::ShaderFormat::msl, rhi::ShaderFormat::dxil, rhi::ShaderFormat::dxbc, rhi::ShaderFormat::wgsl})
        if (caps.formats.has(format)) formats.push_back(std::string(rhi::name(format)));
    return answer(Json{{"ok", true},
                       {"backend", std::string(rhi::name(caps.backend))},
                       {"device", caps.device},
                       {"software", caps.software},
                       {"compute", caps.compute},
                       {"formats", std::move(formats)},
                       {"max_texture_size", caps.max_texture_size},
                       {"max_samples", caps.max_samples},
                       {"depth", caps.depth}},
                  0);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) return wrong("a command");
    const std::string_view command = argv[1];
    if (command == "effect" && argc >= 3 && std::string_view(argv[2]) == "check") return effect_check(argc, argv);
    if (command == "effect" && argc >= 3 && std::string_view(argv[2]) == "preview") return effect_preview(argc, argv);
    if (command == "cts") return cts(argc, argv);
    if (command == "device" && argc == 2) return device_info();
    return wrong(std::format("no command `{}`", command));
}
