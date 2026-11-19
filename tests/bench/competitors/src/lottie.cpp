// hanim's side of py/lottie.py: one animation, made here from a seed, written as a Lottie file for ThorVG and rlottie,
// Lottie's native players, there, and played by hanim here (its objects and programs, the engine sought, the frame
// recorded as a display list and drawn by xrender's CPU renderer). 200 rounded rectangles, each moving through three places, turning, scaling and fading, on
// the same cubic-bezier eases, 2 s at 60 frames a second on a 512 × 512 stage; every frame drawn whole on one thread,
// the frames visited in a scrambled order (frame 37·k mod 120: a seek each time, never the next frame), the clock round
// the seek and the drawing.
//
//   omui-bench-lottie --out DIR [--runs N]      DIR/lottie-200.json, DIR/lottie-200.hanim.rgba (frame 60); one JSON line
//   omui-bench-lottie --compare DIR             frame 60 of each player (DIR/lottie-200.<player>.rgba) beside hanim's
//   omui-bench-lottie --out DIR --phases        where a frame's time goes: hanim's seek and evaluation, the display
//                                               list recorded, xrender's drawing (each part's median over the frames)
import std;
import mcpplibs.cmdline;
import mlibs.math;
import mlibs.geom;
import hanim;
import hanim.render.vector;
import xrender.raster;
import xrender.vector;

namespace {

constexpr int side = 512, frames = 120, rate = 60, objects = 200;
constexpr std::string_view scene = "lottie-200";

struct Thing {
    double width, height, radius;
    std::array<double, 3> color;
    std::array<std::array<double, 2>, 3> at;  // where it is at frames 0, 60 and 120
    double turn;                              // degrees, by frame 120
    double scale;                             // at frame 60, back to 1 at 120
    double opacity;                           // at frame 120, from 1
};

std::vector<Thing> things() {
    mlibs::math::SplitMix64 stream(0x10771e);
    const auto between = [&](double low, double high) { return low + (high - low) * mlibs::math::unit24(stream.next()); };
    std::vector<Thing> out;
    for (int index = 0; index < objects; ++index) {
        Thing thing{};
        thing.width = between(16, 64), thing.height = between(16, 64), thing.radius = between(2, 8);
        thing.color = {between(0.2, 1), between(0.2, 1), between(0.2, 1)};
        for (auto& place : thing.at) place = {between(40, side - 40), between(40, side - 40)};
        thing.turn = between(-180, 180), thing.scale = between(0.6, 1.6), thing.opacity = between(0.3, 1);
        out.push_back(thing);
    }
    return out;
}

// easeInOutCubic as a cubic bezier, for both.
constexpr std::string_view handles_out = R"("o":{"x":[0.65],"y":[0]},"i":{"x":[0.35],"y":[1]})";

std::string keyed(std::string_view first, std::string_view middle, std::string_view last) {
    return std::format(R"({{"a":1,"k":[{{"t":0,"s":{},{}}},{{"t":60,"s":{},{}}},{{"t":120,"s":{}}}]}})", first, handles_out, middle, handles_out, last);
}

std::string pair(std::array<double, 2> value) { return std::format("[{:.3f},{:.3f}]", value[0], value[1]); }

// The Lottie file: a shape layer a thing, the first thing at the bottom (Lottie draws its first layer on top).
std::string lottie_of(const std::vector<Thing>& all) {
    std::string layers;
    for (std::size_t index = all.size(); index-- > 0;) {
        const Thing& thing = all[index];
        const std::string scale = std::format("[{0:.3f},{0:.3f},100]", thing.scale * 100);
        if (!layers.empty()) layers += ",";
        layers += std::format(
            R"({{"ty":4,"ind":{},"ip":0,"op":{},"st":0,"ks":{{"a":{{"a":0,"k":[0,0]}},"p":{},"r":{},"s":{},"o":{}}},)"
            R"("shapes":[{{"ty":"gr","it":[{{"ty":"rc","p":{{"a":0,"k":[0,0]}},"s":{{"a":0,"k":[{:.3f},{:.3f}]}},"r":{{"a":0,"k":{:.3f}}}}},)"
            R"({{"ty":"fl","c":{{"a":0,"k":[{:.4f},{:.4f},{:.4f},1]}},"o":{{"a":0,"k":100}}}},)"
            R"({{"ty":"tr","p":{{"a":0,"k":[0,0]}},"a":{{"a":0,"k":[0,0]}},"s":{{"a":0,"k":[100,100]}},"r":{{"a":0,"k":0}},"o":{{"a":0,"k":100}}}}]}}]}})",
            index + 1, frames, keyed(pair(thing.at[0]), pair(thing.at[1]), pair(thing.at[2])),
            keyed("[0]", std::format("[{:.3f}]", thing.turn / 2), std::format("[{:.3f}]", thing.turn)), keyed("[100,100,100]", scale, "[100,100,100]"),
            keyed("[100]", std::format("[{:.3f}]", (1 + thing.opacity) * 50), std::format("[{:.3f}]", thing.opacity * 100)), thing.width, thing.height,
            thing.radius, thing.color[0], thing.color[1], thing.color[2]);
    }
    return std::format(R"({{"v":"5.7.0","fr":{},"ip":0,"op":{},"w":{},"h":{},"layers":[{}]}})", rate, frames, side, side, layers);
}

// The same animation in hanim: an object a thing (a rounded rectangle about its origin), its four changes in parallel,
// each two eased halves of a second; the first thing at the bottom.
struct Played {
    hanim::Engine engine;
    hanim::PreparedProgram plan;
    hanim::PlaybackRef playback;
    hanim::vector_render::PreparedVectorProgram drawing;
};

hanim::Contour rounded(double width, double height, double radius) {
    using hanim::ShapeSegment;
    constexpr double handle = 0.5522847498307936;
    const double w = width / 2, h = height / 2, r = radius, k = r * (1 - handle);
    const std::array segments{ShapeSegment::line({w - r, -h}), ShapeSegment::cubic({w - k, -h}, {w, -h + k}, {w, -h + r}), ShapeSegment::line({w, h - r}),
                              ShapeSegment::cubic({w, h - k}, {w - k, h}, {w - r, h}), ShapeSegment::line({-w + r, h}), ShapeSegment::cubic({-w + k, h}, {-w, h - k}, {-w, h - r}),
                              ShapeSegment::line({-w, -h + r}), ShapeSegment::cubic({-w, -h + k}, {-w + k, -h}, {-w + r, -h})};
    return *hanim::Contour::copy({{-w + r, -h}, segments, true});
}

std::expected<Played, std::string> play(const std::vector<Thing>& all) {
    auto engine = hanim::Engine::make();
    if (!engine) return std::unexpected(engine.error().text());
    const hanim::Ease ease = hanim::Ease::bezier(0.65, 0, 0.35, 1);
    const hanim::seconds half(1);
    std::vector<hanim::Program> each;
    for (const Thing& thing : all) {
        const hanim::Rgba fill{static_cast<float>(thing.color[0]), static_cast<float>(thing.color[1]), static_cast<float>(thing.color[2]), 1};
        auto made = engine->create_object({{{.id = "shape", .local = {.tx = thing.at[0][0], .ty = thing.at[0][1]}, .contours = {rounded(thing.width, thing.height, thing.radius)},
                                             .fill = fill}}});
        if (!made) return std::unexpected(std::string(describe(made.error()).name));
        const auto two = [&](hanim::anim::BoundAnimation first, hanim::anim::BoundAnimation second) {
            return hanim::sequence(std::move(first).duration(half).easing(ease), std::move(second).duration(half).easing(ease));
        };
        hanim::Rgba middle = fill, last = fill;
        middle.a = static_cast<float>((1 + thing.opacity) / 2), last.a = static_cast<float>(thing.opacity);
        each.push_back(hanim::parallel(
            two(hanim::anim::move_to(*made, {thing.at[1][0], thing.at[1][1]}), hanim::anim::move_to(*made, {thing.at[2][0], thing.at[2][1]})),
            two(hanim::anim::rotate_to(*made, thing.turn / 2 * std::numbers::pi / 180), hanim::anim::rotate_to(*made, thing.turn * std::numbers::pi / 180)),
            two(hanim::anim::scale_to(*made, {thing.scale, thing.scale}), hanim::anim::scale_to(*made, {1, 1})),
            two(hanim::anim::fill_to(*made, middle), hanim::anim::fill_to(*made, last))));
    }
    hanim::ProgramLimits limits;
    limits.max_nodes = limits.max_segments = 16384;
    auto plan = engine->prepare(hanim::parallel(std::span<const hanim::Program>(each)), limits);
    if (!plan) return std::unexpected(plan.error().text());
    auto drawing = hanim::vector_render::PreparedVectorProgram::bind(*plan);
    if (!drawing) return std::unexpected(drawing.error().text());
    auto playback = engine->play(*plan);
    if (!playback) return std::unexpected(playback.error().text());
    if (auto paused = engine->pause(*playback); !paused) return std::unexpected(paused.error().text());
    return Played{std::move(*engine), std::move(*plan), *playback, std::move(*drawing)};
}

bool write(const std::filesystem::path& file, std::string_view text) {
    std::ofstream out(file, std::ios::binary);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    return static_cast<bool>(out);
}

std::optional<std::vector<std::uint8_t>> read(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;
    return std::vector<std::uint8_t>{std::istreambuf_iterator<char>(in), {}};
}

// Frame 60 of each player beside hanim's: the mean difference of a channel, and the pixels a channel of which is more
// than 16 of 255 away (straight RGBA on white, as each player writes it).
int compare(const std::filesystem::path& dir) {
    const auto ours = read(dir / std::format("{}.hanim.rgba", scene));
    if (!ours) return 1;
    for (const std::string_view player : {"thorvg", "rlottie"}) {
        const auto theirs = read(dir / std::format("{}.{}.rgba", scene, player));
        if (!theirs || theirs->size() != ours->size()) continue;
        double sum = 0;
        std::size_t off = 0;
        for (std::size_t pixel = 0; pixel < ours->size(); pixel += 4) {
            int worst = 0;
            for (int channel = 0; channel < 3; ++channel) {
                const int difference = std::abs(int((*ours)[pixel + channel]) - int((*theirs)[pixel + channel]));
                sum += difference, worst = std::max(worst, difference);
            }
            off += worst > 16;
        }
        std::println(R"({{"scene":"{}","player":"{}","mean_difference":{:.3f},"pixels_off_percent":{:.3f}}})", scene, player, sum / (ours->size() / 4 * 3),
                     100.0 * off / (ours->size() / 4));
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    using mcpplibs::cmdline::App, mcpplibs::cmdline::Option;
    const auto line = App("omui-bench-lottie")
        .description("hanim playing the animation Lottie's players play")
        .option(Option("out").takes_value().value_name("DIR").help("where the animation, the document and frame 60 go"))
        .option(Option("runs").value_type<int>().help("passes over the 120 frames, after one to warm up (default 3)"))
        .option(Option("compare").takes_value().value_name("DIR").help("frame 60 of each player beside hanim's"))
        .option(Option("phases").help("with --out: where a frame's time goes (evaluation, recording, drawing)"));
    auto parsed = line.parse(argc, argv);
    if (!parsed) {
        if (!parsed.error().is_error()) return 0;
        std::println(std::cerr, "omui-bench-lottie: {}", parsed.error().message);
        return 2;
    }
    if (const auto dir = parsed->value("compare")) return compare(*dir);
    const std::filesystem::path out = parsed->value("out").value_or("");
    if (out.empty()) {
        std::println(std::cerr, "omui-bench-lottie: --out DIR (omui-bench-lottie --help)");
        return 2;
    }
    const int runs = std::max(1, parsed->value_as<int>("runs").value_or(3));
    std::filesystem::create_directories(out);
    const auto all = things();
    if (!write(out / std::format("{}.json", scene), lottie_of(all))) return 1;
    auto played = play(all);
    if (!played) {
        std::println(std::cerr, "omui-bench-lottie: {}", played.error());
        return 1;
    }
    const auto needs = played->drawing.requirements();
    std::vector<mlibs::geom::Affine> world(needs.buffers.world_transforms);
    std::vector<float> parameters(needs.buffers.backend_parameters);
    std::vector<mlibs::geom::Rect> layers(needs.buffers.effect_layers), bounds(needs.tracks + needs.scopes);
    std::vector<std::size_t> members(needs.scopes), order(needs.tracks), stack(needs.scope_depth);
    xrender::DisplayList list, staging;
    xrender::CpuRenderer renderer;
    xrender::raster::Surface surface(side, side);
    // A frame: sought, recorded, drawn; with `parts`, each part's time in milliseconds (hanim's seek and evaluation,
    // the display list recorded, xrender's drawing).
    const auto draw = [&](int frame, std::array<double, 3>* parts = nullptr) -> bool {
        using clock = std::chrono::steady_clock;
        const auto begun = clock::now();
        if (!played->engine.seek(played->playback, hanim::seconds(static_cast<double>(frame) / rate))) return false;
        const auto now = played->engine.program_frame(played->playback);
        if (!now) return false;
        const auto evaluated = clock::now();
        list.clear();
        if (!played->drawing.record(now->scene, list, {{&staging, world, parameters, layers}, bounds, members, order, stack})) return false;
        const auto recorded = clock::now();
        const auto report = renderer.render(list, surface, {}, {.background = mlibs::geom::Color::white()});
        if (parts) {
            const auto spent = [](clock::time_point from, clock::time_point to) { return std::chrono::duration<double, std::milli>(to - from).count(); };
            *parts = {spent(begun, evaluated), spent(evaluated, recorded), spent(recorded, clock::now())};
        }
        return report.invalid_paints == 0 && report.effects_skipped == 0;
    };
    if (parsed->is_flag_set("phases")) {
        std::array<std::vector<double>, 3> parts;
        for (int pass = 0; pass < 1 + runs; ++pass)
            for (int visit = 0; visit < frames; ++visit) {
                std::array<double, 3> spent{};
                if (!draw(visit * 37 % frames, &spent)) {
                    std::println(std::cerr, "omui-bench-lottie: frame {} was not drawn", visit * 37 % frames);
                    return 1;
                }
                if (pass > 0)
                    for (std::size_t part = 0; part < parts.size(); ++part) parts[part].push_back(spent[part]);
            }
        const auto median = [](std::vector<double> values) { std::ranges::sort(values); return values[values.size() / 2]; };
        std::println(R"({{"player":"hanim","scene":"{}","evaluate_ms":{:.3f},"record_ms":{:.3f},"draw_ms":{:.3f}}})", scene, median(parts[0]),
                     median(parts[1]), median(parts[2]));
        return 0;
    }
    std::vector<double> times;
    for (int pass = 0; pass < 1 + runs; ++pass)
        for (int visit = 0; visit < frames; ++visit) {
            const int frame = visit * 37 % frames;
            const auto start = std::chrono::steady_clock::now();
            const bool drawn = draw(frame);
            const auto spent = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            if (!drawn) {
                std::println(std::cerr, "omui-bench-lottie: frame {} was not drawn", frame);
                return 1;
            }
            if (pass > 0) times.push_back(spent);
        }
    // Frame 60, straight RGBA on white, for --compare.
    if (!draw(60)) return 1;
    std::vector<std::uint8_t> rgba;
    for (int y = 0; y < side; ++y)
        for (int x = 0; x < side; ++x) {
            const auto& pixel = surface.row(y)[x];
            rgba.insert(rgba.end(), {pixel.r, pixel.g, pixel.b, pixel.a});
        }
    if (!write(out / std::format("{}.hanim.rgba", scene), std::string_view(reinterpret_cast<const char*>(rgba.data()), rgba.size()))) return 1;
    double mean = 0;
    for (const double time : times) mean += time;
    mean /= static_cast<double>(times.size());
    std::ranges::sort(times);
    const auto at = [&](double percent) { return times[std::min(times.size() - 1, static_cast<std::size_t>(std::ceil(percent / 100 * times.size())) - 1)]; };
    std::println(R"({{"player":"hanim","scene":"{}","objects":{},"frames":{},"median_ms":{:.3f},"p95_ms":{:.3f},"mean_ms":{:.3f}}})", scene, objects, times.size(), at(50),
                 at(95), mean);
    return 0;
}
