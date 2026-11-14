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
//   xrender look MODEL --out PNG [--light HDR] [--orbit THETA,PHI,RADIUS] [--target X,Y,Z] [--size WxH]
//                [--upright DEGREES] [--samples N] [--golden PNG] [--follow RAYS]
//                                                a glTF model drawn by the reference as a viewer of products
//                                                frames it, lit by an HDR picture of the surroundings; beside
//                                                another renderer's picture of the same (its distance in the
//                                                render-fidelity suite's measure), and beside the light
//                                                followed way by way
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
import xrender.asset;
import mcpplibs.os;
import mcpplibs.cmdline;
import nlohmann.json;

namespace {

namespace cmdline = mcpplibs::cmdline;

namespace fx = xrender::effect;
namespace geom = mlibs::geom;
namespace raster = xrender::raster;
namespace rhi = mlibs::rhi;
using Json = nlohmann::json;

constexpr std::string_view usage =
    "usage: xrender effect check FILE… | xrender effect preview FILE --out PNG [--image PNG] [--size WxH] [--param NAME=VALUE …] | "
    "xrender cts [--report FILE] | xrender device | "
    "xrender look MODEL --out PNG [--light HDR] [--orbit THETA,PHI,RADIUS] [--target X,Y,Z] [--size WxH] [--upright DEGREES] [--samples N] [--golden PNG] [--follow RAYS]";

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

int effect_check(const cmdline::ParsedArgs& args) {
    if (args.positional_count() == 0) return wrong("effect check takes one file or more");
    fx::Library library({.formats = xrender::gpu_shader_formats()});
    Json effects = Json::array();
    bool ok = true;
    for (const std::string& name : args.positionals) {
        const auto file = path_of(name);
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

int effect_preview(const cmdline::ParsedArgs& args) {
    const auto file = path_of(args.positional(0));
    std::optional<std::filesystem::path> out, image_file;
    if (auto value = args.value("out")) out = path_of(*value);
    if (auto value = args.value("image")) image_file = path_of(*value);
    int width = 320, height = 200;
    if (auto size = args.value("size")) {
        const std::string_view value = *size;
        {
            const auto x = value.find('x');
            if (x == std::string_view::npos || std::from_chars(value.data(), value.data() + x, width).ec != std::errc{} ||
                std::from_chars(value.data() + x + 1, value.data() + value.size(), height).ec != std::errc{} || width < 8 || height < 8 ||
                width > 4096 || height > 4096)
                return wrong("--size is WxH, 8 … 4096 each");
        }
    }
    std::vector<std::pair<std::string, float>> given;
    for (const std::string& value : args.option_or_empty("param").values) {
        const auto eq = value.find('=');
        float number = 0;
        if (eq == std::string::npos || std::from_chars(value.data() + eq + 1, value.data() + value.size(), number).ec != std::errc{})
            return wrong("--param is NAME=VALUE");
        given.emplace_back(value.substr(0, eq), number);
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

int cts(const cmdline::ParsedArgs& args) {
    std::optional<std::filesystem::path> report_file;
    if (auto value = args.value("report")) report_file = path_of(*value);
    auto device = open_device();
    if (!device) return failed(device.error());
    const auto scratch = std::filesystem::temp_directory_path() / "xrender-cts";
    // Counted here: this is no test program, and its answer is the cases by how they ended.
    const std::string subject = (*device)->caps().device;
    const mlibs::cts::Tally tally = mlibs::cts::tally([&] { xrender::cts::declare(**device, {.subject = subject}, {.scratch = scratch}); });
    Json failures = Json::array(), unsupported = Json::array(), cases = Json::array();
    for (const auto& entry : tally.cases) {
        if (entry.status == mlibs::cts::Status::failed) failures.push_back(entry.id);
        if (entry.status == mlibs::cts::Status::unsupported) unsupported.push_back(entry.id);
        Json one{{"id", entry.id}, {"contract", entry.contract}, {"level", std::string(mlibs::cts::name(entry.level))}, {"status", std::string(mlibs::cts::name(entry.status))}};
        if (!entry.missing.empty()) one["missing"] = entry.missing;
        for (const auto& failure : entry.failures) one["failures"].push_back({{"where", failure.where}, {"why", failure.why}});
        for (const auto& [kind, value] : entry.evidence) one["evidence"].push_back({{"kind", kind}, {"value", value}});
        cases.push_back(std::move(one));
    }
    if (report_file) {
        // The whole of it, for a reviewer: every case with its failures and evidence.
        const Json report{{"format", "xrender.cts@1"},
                          {"suite", "xrender"},
                          {"environment", {{"subject", subject}, {"platform", "sdl"}}},
                          {"totals",
                           {{"passed", tally.count(mlibs::cts::Status::passed)},
                            {"failed", tally.count(mlibs::cts::Status::failed)},
                            {"unsupported", tally.count(mlibs::cts::Status::unsupported)}}},
                          {"cases", std::move(cases)}};
        if (!mcpplibs::os::fs::write_atomic(*report_file, report.dump(2) + "\n")) return failed(std::format("cannot write {}", utf8(*report_file)));
    }
    return answer(Json{{"ok", tally.ok()},
                       {"device", subject},
                       {"passed", tally.count(mlibs::cts::Status::passed)},
                       {"failed", std::move(failures)},
                       {"unsupported", std::move(unsupported)},
                       {"pictures", utf8(scratch)},
                       {"report", report_file ? Json(utf8(*report_file)) : Json(nullptr)}},
                  tally.ok() ? 0 : 1);
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

// Numbers apart by commas: exactly `count` of them, or nothing.
std::optional<std::vector<float>> numbers(std::string_view text, std::size_t count) {
    std::vector<float> all;
    for (const auto part : std::views::split(text, ',')) {
        const std::string one(part.begin(), part.end());
        float value = 0;
        const auto read = std::from_chars(one.data(), one.data() + one.size(), value);
        if (read.ec != std::errc{} || read.ptr != one.data() + one.size()) return std::nullopt;
        all.push_back(value);
    }
    if (all.size() != count) return std::nullopt;
    return all;
}

// A model looked at: drawn by the CPU reference, written, and stood beside what it is asked to stand beside.
int look(const cmdline::ParsedArgs& args) {
    namespace asset = xrender::asset;
    namespace mesh = xrender::mesh;
    namespace image = mlibs::image;
    constexpr float degree = 0.017453292f;
    if (args.positional_count() != 1) return wrong("look takes one model");
    const auto out = args.value("out");
    if (!out) return wrong("look needs --out PNG");
    int width = 768, height = 768, samples = 2;
    if (const auto size = args.value("size")) {
        const std::size_t by = size->find('x');
        const auto wide = by == std::string::npos ? std::nullopt : numbers(std::string_view(*size).substr(0, by), 1), high = by == std::string::npos ? std::nullopt : numbers(std::string_view(*size).substr(by + 1), 1);
        if (!wide || !high || (*wide)[0] < 8 || (*wide)[0] > 4096 || (*high)[0] < 8 || (*high)[0] > 4096) return wrong("--size WxH, 8 … 4096 each");
        width = static_cast<int>((*wide)[0]), height = static_cast<int>((*high)[0]);
    }
    if (const auto said = args.value("samples")) {
        const auto count = numbers(*said, 1);
        if (!count || (*count)[0] < 1 || (*count)[0] > 4) return wrong("--samples 1 … 4 (each way)");
        samples = static_cast<int>((*count)[0]);
    }
    const auto model = asset::gltf_file(path_of(args.positional(0)));
    if (!model) return failed(model.error());
    const mlibs::geom::Box3 box = model->bounds();
    // As a viewer shows a model it was told nothing about: from before and a little above, all of it in the picture.
    asset::Orbit orbit{.theta = 0, .phi = 75 * degree, .radius = mlibs::math::length(box.half()) * 2.6f, .target = box.centre()};
    if (const auto said = args.value("orbit")) {
        const auto three = numbers(*said, 3);
        if (!three || !((*three)[2] > 0)) return wrong("--orbit THETA,PHI,RADIUS: degrees about the way up from +z, degrees down from the way up, how far");
        orbit.theta = (*three)[0] * degree, orbit.phi = (*three)[1] * degree, orbit.radius = (*three)[2], orbit.target = {0, 0, 0};
    }
    if (const auto said = args.value("target")) {
        const auto three = numbers(*said, 3);
        if (!three) return wrong("--target X,Y,Z");
        orbit.target = {(*three)[0], (*three)[1], (*three)[2]};
    }
    if (const auto said = args.value("upright")) {
        const auto angle = numbers(*said, 1);
        if (!angle || !((*angle)[0] > 1) || !((*angle)[0] < 170)) return wrong("--upright DEGREES, 1 … 170");
        orbit.upright = (*angle)[0] * degree;
    }
    std::optional<mesh::Surroundings> surroundings;
    Json light_said = nullptr;
    if (const auto file = args.value("light")) {
        const auto light = image::load_hdr(path_of(*file));
        if (!light) return failed(light.error());
        surroundings = mesh::Surroundings::made(light->rgb, light->width, light->height);
        if (!surroundings) return failed("the picture of the surroundings could not be made");
        light_said = Json{{"file", *file}, {"width", light->width}, {"height", light->height}, {"across", surroundings->sizes[0].side}, {"cut", surroundings->clamped}};
    }
    const xrender::scene::View view = asset::view_of(orbit, box, static_cast<float>(width) / static_cast<float>(height));
    const auto begun = std::chrono::steady_clock::now();
    const auto picture = asset::looked(*model, view, {.width = width, .height = height, .samples = samples, .surroundings = surroundings ? &*surroundings : nullptr});
    if (!picture) return failed(picture.error());
    const double drawn_in = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begun).count();
    const image::View ours{picture->rgba.data(), width, height, width * 4, image::Alpha::straight};
    if (const auto saved = image::save_png(path_of(*out), ours); !saved) return failed(saved.error());
    std::size_t whole = 0, partly = 0;
    for (std::size_t pixel = 0; pixel < static_cast<std::size_t>(width) * static_cast<std::size_t>(height); ++pixel) whole += picture->rgba[pixel * 4 + 3] == 255, partly += picture->rgba[pixel * 4 + 3] != 0 && picture->rgba[pixel * 4 + 3] != 255;
    Json said{{"ok", true},
              {"model", {{"meshes", model->meshes.size()}, {"triangles", model->triangles()}, {"materials", model->materials.size()}, {"textures", model->textures.size()}, {"notes", model->notes.size()}}},
              {"orbit", {{"theta", orbit.theta / degree}, {"phi", orbit.phi / degree}, {"radius", orbit.radius}, {"target", {orbit.target.x, orbit.target.y, orbit.target.z}}, {"upright", orbit.upright / degree}}},
              {"light", light_said},
              {"out", *out},
              {"width", width},
              {"height", height},
              {"samples", samples * samples},
              {"draws", picture->stats.draws},
              {"pixels", {{"whole", whole}, {"partly", partly}}},
              {"drawn_ms", drawn_in}};
    // Beside another renderer's picture of the same.
    std::optional<image::Image> golden;
    if (const auto file = args.value("golden")) {
        auto read = image::load_png(path_of(*file));
        if (!read) return failed(read.error());
        if (read->width != width || read->height != height) return failed(std::format("the golden is {} × {}, the picture {} × {}: --size {}x{}", read->width, read->height, width, height, read->width, read->height));
        golden = std::move(*read);
        const auto distance = image::apart(ours, image::view(*golden));
        if (!distance) return failed("nothing of the model is in the picture");
        std::size_t undrawn = 0;
        for (std::size_t pixel = 0; pixel < static_cast<std::size_t>(width) * static_cast<std::size_t>(height); ++pixel) undrawn += golden->rgba[pixel * 4 + 3] == 255 && picture->rgba[pixel * 4 + 3] == 0;
        said["golden"] = {{"file", *file}, {"compared", distance->compared}, {"ratio", distance->ratio}, {"decibels", distance->decibels()}, {"suite_bound_decibels", -22}, {"its_pixels_not_drawn", undrawn}};
    }
    // Beside the light followed way by way, a quarter as large each way: where every sample of both is whole.
    if (const auto rays = args.value("follow")) {
        const auto count = numbers(*rays, 1);
        if (!count || (*count)[0] < 1 || (*count)[0] > 65536) return wrong("--follow RAYS a pixel, 1 … 65536");
        if (width % 4 != 0 || height % 4 != 0) return wrong("--follow takes a picture whose sides are multiples of 4");
        const int small_width = width / 4, small_height = height / 4;
        xrender::scene::Scene world;
        if (const auto put = asset::place(*model, world); !put) return failed(put.error());
        std::vector<const mlibs::geom::ClusteredMesh*> meshes;
        std::vector<const mesh::Texture*> textures;
        std::vector<mlibs::geom::Bvh> grown;
        std::vector<const mlibs::geom::Bvh*> trees;
        for (const auto& one : model->meshes) meshes.push_back(&one), grown.push_back(mlibs::geom::bvh_of(one.mesh));
        for (const auto& tree : grown) trees.push_back(&tree);
        for (const auto& one : model->textures) textures.push_back(&one);
        mesh::Targets followed;
        followed.resize(small_width, small_height);
        mesh::Options options{.background = {0, 0, 0}};
        if (surroundings) options.environment = {.on = true, .picture = &*surroundings};
        const auto from = std::chrono::steady_clock::now();
        const mesh::Traced told = mesh::traced(world, view, {meshes, model->materials, trees, textures}, followed, options, {.samples = static_cast<std::uint32_t>((*count)[0]), .bounces = 3});
        const double followed_in = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - from).count();
        mesh::Glow glow;
        std::vector<std::uint8_t> truth;
        (void)glow.show(followed.light, small_width, small_height, {}, truth, {.neutral = true});
        for (std::size_t pixel = 0; pixel < truth.size() / 4; ++pixel) truth[pixel * 4 + 3] = 255;
        const std::string beside = *out + ".followed.png";
        const image::View truth_view{truth.data(), small_width, small_height, small_width * 4, image::Alpha::straight};
        if (const auto saved = image::save_png(path_of(beside), truth_view); !saved) return failed(saved.error());
        // A picture a quarter as large: a pixel the mean of sixteen, and there at all only where all sixteen are whole in both.
        const auto quarter = [&](const std::uint8_t* rgba) {
            std::vector<std::uint8_t> made(static_cast<std::size_t>(small_width) * static_cast<std::size_t>(small_height) * 4, 0);
            for (int y = 0; y < small_height; ++y)
                for (int x = 0; x < small_width; ++x) {
                    std::array<int, 3> sum{0, 0, 0};
                    bool all_whole = true;
                    for (int below = 0; below < 4; ++below)
                        for (int along = 0; along < 4; ++along) {
                            const std::size_t at = (static_cast<std::size_t>(y * 4 + below) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x * 4 + along)) * 4;
                            all_whole = all_whole && picture->rgba[at + 3] == 255 && (!golden || golden->rgba[at + 3] == 255);
                            for (std::size_t channel = 0; channel < 3; ++channel) sum[channel] += rgba[at + channel];
                        }
                    std::uint8_t* pixel = made.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(small_width) + static_cast<std::size_t>(x)) * 4;
                    for (std::size_t channel = 0; channel < 3; ++channel) pixel[channel] = static_cast<std::uint8_t>((sum[channel] + 8) / 16);
                    pixel[3] = all_whole ? 255 : 0;
                }
            return made;
        };
        const std::vector<std::uint8_t> ours_small = quarter(picture->rgba.data());
        const auto of_ours = image::apart({ours_small.data(), small_width, small_height, small_width * 4, image::Alpha::straight}, truth_view);
        Json beside_truth{{"file", beside}, {"width", small_width}, {"height", small_height}, {"rays_a_pixel", static_cast<int>((*count)[0])}, {"bounces", 3}, {"rays", told.rays}, {"followed_ms", followed_in}};
        if (of_ours) beside_truth["drawn"] = {{"compared", of_ours->compared}, {"ratio", of_ours->ratio}, {"decibels", of_ours->decibels()}};
        if (golden) {
            const std::vector<std::uint8_t> theirs_small = quarter(golden->rgba.data());
            if (const auto of_theirs = image::apart({theirs_small.data(), small_width, small_height, small_width * 4, image::Alpha::straight}, truth_view))
                beside_truth["golden"] = {{"compared", of_theirs->compared}, {"ratio", of_theirs->ratio}, {"decibels", of_theirs->decibels()}};
        }
        said["followed"] = std::move(beside_truth);
    }
    return answer(said, 0);
}

// The command line (mcpplibs.cmdline): effect check | preview, cts, device, look.
cmdline::App command_line() {
    using cmdline::App, cmdline::Arg, cmdline::Option;
    return App("xrender")
        .description("The renderer without a GUI, for people, agents and CI: one JSON answer, exit 0 done / 1 failed / 2 a wrong command")
        .subcommand(App("effect")
                        .description("Effects: checked, or previewed on the GPU")
                        .subcommand(App("check").description("each effect read, its shader compiled, registered").arg(Arg("file").required().help("FILE…")))
                        .subcommand(App("preview")
                                        .description("the effect drawn over a picture (a test card when none): before | after")
                                        .arg(Arg("file").required())
                                        .option(Option("out").takes_value().value_name("PNG").help("the picture written"))
                                        .option(Option("image").takes_value().value_name("PNG").help("the picture under it"))
                                        .option(Option("size").takes_value().value_name("WxH").help("8 … 4096 each (default 320x200)"))
                                        .option(Option("param").takes_value().multiple().value_name("NAME=VALUE").help("a parameter's value"))))
        .subcommand(App("cts").description("the render contract's conformance suite on this device").option(Option("report").takes_value().value_name("FILE")))
        .subcommand(App("device").description("the device: backend, name, formats, limits"))
        .subcommand(App("look")
                        .description("a glTF model drawn by the reference as a viewer frames it; beside another renderer's picture, and beside the light followed way by way")
                        .arg(Arg("model").required().help("a .gltf or .glb"))
                        .option(Option("out").takes_value().value_name("PNG").help("the picture written (nothing where the model is not)"))
                        .option(Option("light").takes_value().value_name("HDR").help("a Radiance picture of the surroundings the model is lit by"))
                        .option(Option("orbit").takes_value().value_name("THETA,PHI,RADIUS").help("degrees about the way up from +z, degrees down from it, how far (model-viewer's)"))
                        .option(Option("target").takes_value().value_name("X,Y,Z").help("what the eye looks at"))
                        .option(Option("size").takes_value().value_name("WxH").help("8 … 4096 each (default 768x768)"))
                        .option(Option("upright").takes_value().value_name("DEGREES").help("the angle of view up and down (default 45)"))
                        .option(Option("samples").takes_value().value_name("N").help("N × N samples a pixel, 1 … 4 (default 2)"))
                        .option(Option("golden").takes_value().value_name("PNG").help("another renderer's picture of the same, of the same size"))
                        .option(Option("follow").takes_value().value_name("RAYS").help("follow the light way by way, RAYS a pixel, a quarter as large each way")));
}

}  // namespace

int main(int argc, char** argv) {
    const cmdline::App app = command_line();
    auto parsed = app.parse(argc, argv);
    if (!parsed) return parsed.error().is_error() ? wrong(parsed.error().message) : 0;
    if (!parsed->has_subcommand()) return wrong("a command: effect, cts, device or look");
    const cmdline::ParsedArgs& args = parsed->subcommand()->get();
    if (parsed->subcommand_name() == "cts") return cts(args);
    if (parsed->subcommand_name() == "device") return device_info();
    if (parsed->subcommand_name() == "look") return look(args);
    if (!args.has_subcommand()) return wrong("effect check or effect preview");
    if (args.subcommand_name() == "check") return effect_check(args.subcommand()->get());
    return effect_preview(args.subcommand()->get());
}
