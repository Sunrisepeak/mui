module omui.site;

import std;
import omui;
import mlibs.hash;
import mcpplibs.os;

namespace omui::site {
namespace {

// Bound memory and the deliberately sequential comparison replay. These are
// export limits, independent of the runtime's supported window dimensions.
constexpr int max_dimension = 8192;
constexpr std::int64_t max_pixels = 16 * 1024 * 1024;
constexpr double max_time = 3600;

bool route_has_language(std::string_view route) {
    if (route.starts_with('#')) route.remove_prefix(1);
    while (route.starts_with('/')) route.remove_prefix(1);
    return route == "en" || route == "zh" || route.starts_with("en/") || route.starts_with("zh/");
}

std::expected<SiteRoute, std::string> validated(const FirstFrameOptions& options) {
    if (options.png.empty() || options.png.contains('\0')) return std::unexpected("--first-frame needs a PNG file path");
    auto extension = mcpplibs::os::fs::from_utf8(options.png).extension().generic_u8string();
    for (auto& c : extension) if (c >= u8'A' && c <= u8'Z') c = static_cast<char8_t>(c - u8'A' + u8'a');
    if (extension != u8".png") return std::unexpected("--first-frame output must have a .png extension");
    if (options.lang != "en" && options.lang != "zh") return std::unexpected("--lang must be en or zh");
    if (options.width < 1 || options.width > max_dimension || options.height < 1 || options.height > max_dimension ||
        static_cast<std::int64_t>(options.width) * options.height > max_pixels)
        return std::unexpected("--width and --height must be integers in 1..8192, with at most 16777216 pixels");
    if (!std::isfinite(options.time) || options.time < 0 || options.time > max_time)
        return std::unexpected("--time must be finite seconds in 0..3600");
    const auto route = parse_site_route(options.route, options.lang);
    if (!route) return std::unexpected("--route must name an existing site page or exhibit");
    if (route->language != options.lang) return std::unexpected("the route's language conflicts with --lang");
    if (options.root.contains('\0')) return std::unexpected("--root must be an explicit node id without NUL");
    return *route;
}

Value rectangle(Rect bounds) {
    return Value(Value::List{Value(bounds.x), Value(bounds.y), Value(bounds.width), Value(bounds.height)});
}

std::string region_hash(const xrender::raster::Surface& pixels, PixelRect region) {
    mlibs::hash::Fnv1a hash;
    for (int y = region.y; y < region.y + region.height; ++y)
        hash.bytes(std::as_bytes(std::span(pixels.row(y) + region.x, static_cast<std::size_t>(region.width))));
    return std::format("{:016x}", hash.value());
}

}  // namespace

std::expected<FirstFrameOptions, std::string> parse_first_frame_options(std::span<const std::string_view> args) {
    FirstFrameOptions options;
    constexpr std::array<std::string_view, 8> flags{"--first-frame", "--route", "--lang", "--width", "--height", "--time", "--root", "--reduced-motion"};
    std::array<bool, flags.size()> seen{};
    for (std::size_t i = 0; i < args.size(); ++i) {
        const auto position = std::ranges::find(flags, args[i]);
        if (position == flags.end()) return std::unexpected("unknown first-frame option: " + std::string(args[i]));
        const auto index = static_cast<std::size_t>(position - flags.begin());
        if (seen[index]) return std::unexpected("duplicate first-frame option: " + std::string(args[i]));
        seen[index] = true;
        if (*position == "--reduced-motion") { options.reduce_motion = true; continue; }
        if (++i == args.size() || args[i].starts_with("--")) return std::unexpected("missing value for " + std::string(*position));
        const auto value = args[i];
        if (*position == "--first-frame") options.png = value;
        else if (*position == "--route") options.route = value;
        else if (*position == "--lang") options.lang = value;
        else if (*position == "--root") {
            if (value.empty()) return std::unexpected("--root must not be empty");
            options.root = value;
        } else if (*position == "--time") {
            const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), options.time);
            if (error != std::errc{} || end != value.data() + value.size()) return std::unexpected("--time must be a number of seconds");
            if (options.time == 0) options.time = 0;  // canonicalise negative zero
        } else {
            int& dimension = *position == "--width" ? options.width : options.height;
            const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), dimension);
            if (error != std::errc{} || end != value.data() + value.size()) return std::unexpected(std::string(*position) + " must be an integer");
        }
    }
    if (!seen[0]) return std::unexpected("--first-frame <PNG> is required");
    if (!seen[2] && route_has_language(options.route)) {
        if (const auto route = parse_site_route(options.route)) options.lang = route->language;
    }
    if (auto result = validated(options); !result) return std::unexpected(result.error());
    return options;
}

std::expected<Value, std::string> export_first_frame(const FirstFrameOptions& options) {
    const auto parsed = validated(options);
    if (!parsed) return std::unexpected(parsed.error());
    const std::string route = canonical_route(parsed->place, parsed->language, parsed->component, parsed->example);
    auto application = app(route, parsed->language, route);
    Headless host(HeadlessOptions{.size = {static_cast<float>(options.width), static_cast<float>(options.height)},
                                 .scale = 1, .fps = {60, 1}, .reduce_motion = options.reduce_motion, .parallel_raster = false, .tier = mlibs::pipeline::Tier::t3});
    auto& runtime = host.runtime();
    runtime.set_theme(application.theme);
    application.setup(runtime);
    for (auto& verb : application.verbs) (void)runtime.verbs().add(verb.desc, verb.fn);
    host.mount("main", application.view());
    const auto first = host.step();  // Headless's first frame is precisely t=0
    if (!first.failure.empty()) return std::unexpected("first frame failed: " + first.failure);
    const auto frames = static_cast<std::int64_t>(std::floor(options.time * 60));
    for (std::int64_t frame = 1; frame <= frames; ++frame) {
        const auto output = host.step();
        if (!output.failure.empty()) return std::unexpected("frame replay failed: " + output.failure);
    }
    std::int64_t sampled_frames = host.frame_index() + 1;
    if (runtime.now().count() != options.time) {
        const auto output = runtime.frame({.time = seconds(options.time)});
        if (!output.failure.empty()) return std::unexpected("comparison frame failed: " + output.failure);
        ++sampled_frames;
    }

    const Rect viewport{0, 0, static_cast<float>(options.width), static_cast<float>(options.height)};
    Rect crop = viewport;
    Value root_bounds;
    if (!options.root.empty()) {
        const auto node = host.tree().node(options.root);
        if (!node) return std::unexpected("--root does not name a mounted node at the requested time");
        if (node->states.has(State::hidden)) return std::unexpected("--root is hidden at the requested time");
        if (node->states.has(State::offscreen)) return std::unexpected("--root has no visible area inside its viewport");
        if (!node->bounds) return std::unexpected("--root has no rendered bounds");
        const auto& bounds = *node->bounds;
        const Rect actual{bounds.x, bounds.y, bounds.width, bounds.height};
        root_bounds = rectangle(actual);
        crop = actual.intersection(viewport);
        for (auto parent = runtime.parent(runtime.find(options.root)); runtime.valid(parent); parent = runtime.parent(parent)) {
            const Value overflow = runtime.get(parent, "overflow");
            if (runtime.type(parent).clips || runtime.type(parent).scroll_offset || runtime.get(parent, "clip").as_bool() || overflow.as_string() == "clip" || overflow.as_string() == "scroll")
                crop = crop.intersection(runtime.screen_bounds(parent));
        }
        if (crop.empty()) return std::unexpected("--root has no visible area inside the viewport");
    }
    const auto region = mlibs::geom::snap(crop, 1);
    crop = {static_cast<float>(region.x), static_cast<float>(region.y), static_cast<float>(region.width), static_cast<float>(region.height)};
    if (region.width <= 0 || region.height <= 0) return std::unexpected("the requested pixel region is empty");

    Value::List nodes, unplaced;
    for (const auto& node : host.tree().snapshot().nodes) {
        Value entry = Value{}.set("id", node.id).set("role", std::string(spec::name(node.role)));
        Value::List states;
        for (const auto state : {State::hidden, State::offscreen, State::animating}) if (node.states.has(state)) states.emplace_back(std::string(spec::name(state)));
        entry.set("state", Value(std::move(states)));
        if (!node.bounds) { unplaced.push_back(std::move(entry)); continue; }
        const auto& bounds = *node.bounds;
        entry.set("bounds", rectangle({bounds.x, bounds.y, bounds.width, bounds.height}));
        nodes.push_back(std::move(entry));
    }
    const auto& pixels = host.pixels();
    auto png = mcpplibs::os::fs::from_utf8(options.png);
    auto sidecar = png; sidecar.replace_extension(".frame.omui.json");
    Value report = Value{}.set("schema", "openmui.site.frame@1").set("command", "first-frame").set("ok", true)
        .set("route", route).set("lang", parsed->language).set("viewport", Value(Value::List{Value(options.width), Value(options.height)}))
        .set("renderer", "cpu").set("tier", std::string(mlibs::pipeline::name(host.options().tier))).set("scale", 1).set("raster_bands", 1)
        .set("reduce_motion", options.reduce_motion)
        .set("time", runtime.now().count()).set("fps", 60).set("sampled_frames", sampled_frames).set("root", options.root).set("crop", rectangle(crop)).set("root_bounds", std::move(root_bounds))
        .set("image", Value{}.set("width", region.width).set("height", region.height))
        .set("pixel_hash", region_hash(pixels, region)).set("viewport_pixel_hash", std::format("{:016x}", frame_hash(pixels)))
        .set("png", mcpplibs::os::fs::generic_utf8(png)).set("sidecar", mcpplibs::os::fs::generic_utf8(sidecar))
        .set("nodes", Value(std::move(nodes))).set("unplaced_nodes", Value(std::move(unplaced)));
    if (auto saved = host.save_png(png, crop); !saved) return std::unexpected("PNG export failed: " + saved.error());
    if (auto written = mcpplibs::os::fs::write_atomic(sidecar, report.json()); !written) return std::unexpected("frame metadata export failed: " + written.error().message());
    return report;
}

}  // namespace omui::site
