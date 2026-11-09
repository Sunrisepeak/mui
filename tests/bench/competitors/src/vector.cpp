// xrender's side of js/vector.mjs: the same scenes of filled and stroked
// paths drawn by xrender's CPU renderer here, and by Skia (through
// @napi-rs/canvas) and resvg (tiny-skia) there. The scenes are made here,
// once, from a seed, and written for the others as SVG and as canvas calls,
// so all three draw the same numbers.
//
//   omui-bench-vector --out DIR [--runs N] [--only SCENE]
//   omui-bench-vector --phases [N]    where each scene's time goes (outline, rasterise, composite)
//
// --only draws one scene (for a profiler's samples to be of it alone).
//
// DIR/<scene>.svg, DIR/<scene>.scene.json (path data and paints, for a
// canvas), DIR/<scene>.xrender.png and DIR/<scene>.xrender.svg (the display
// list written by xrender.vector.svg, for the others to draw back); one JSON
// line a scene on standard output
// (its median, mean and fastest frame over N runs, after three to warm up).
import std;
import mcpplibs.cmdline;
import mlibs.geom;
import mlibs.math;
import mlibs.image;
import xrender.raster;
import xrender.vector;
import xrender.vector.svg;

namespace {

using mlibs::geom::Point;
constexpr int side = 1024;

struct Rgba {
    std::uint8_t red = 0, green = 0, blue = 0, alpha = 255;
};

// One drawing: a contour of cubics (closed or not), filled, stroked or
// filled with a two-stop linear gradient.
struct Item {
    enum class Kind : std::uint8_t { fill, stroke, gradient } kind = Kind::fill;
    Point start;
    std::vector<std::array<Point, 3>> cubics;
    std::vector<Point> lines;  // instead of cubics: a polygon
    bool closed = true;
    Rgba color, second;
    float width = 1;
    xrender::LineCap cap = xrender::LineCap::round;
    xrender::LineJoin join = xrender::LineJoin::round;
    Point from, to;  // the gradient's line
};

struct Scene {
    std::string name;
    std::string about;
    std::vector<Item> items;
};

struct Random {
    mlibs::math::SplitMix64 stream;
    double unit() { return mlibs::math::unit24(stream.next()); }
    float between(double low, double high) { return static_cast<float>(low + (high - low) * unit()); }
    Rgba color(std::uint8_t alpha_low, std::uint8_t alpha_high) {
        const auto byte = [&](int low, int high) { return static_cast<std::uint8_t>(low + static_cast<int>(unit() * (high - low + 1))); };
        return {byte(0, 255), byte(0, 255), byte(0, 255), byte(alpha_low, alpha_high)};
    }
};

// A closed blob: `lobes` points around a centre at jittered radii, joined by
// cubics whose handles are tangent to the circle.
Item blob(Random& random, Point centre, double radius, int lobes) {
    Item item;
    std::vector<Point> points;
    std::vector<Point> tangents;
    for (int lobe = 0; lobe < lobes; ++lobe) {
        const double angle = 2 * mlibs::math::det::pi * lobe / lobes;
        const double reach = radius * (0.6 + 0.4 * random.unit());
        const double cosine = mlibs::math::det::cos(angle), sine = mlibs::math::det::sin(angle);
        points.push_back({static_cast<float>(centre.x + reach * cosine), static_cast<float>(centre.y + reach * sine)});
        const double handle = reach * 4.0 / 3.0 * mlibs::math::det::sin(mlibs::math::det::pi / (2.0 * lobes)) /
                              mlibs::math::det::cos(mlibs::math::det::pi / (2.0 * lobes)) * (0.7 + 0.6 * random.unit());
        tangents.push_back({static_cast<float>(-sine * handle), static_cast<float>(cosine * handle)});
    }
    item.start = points[0];
    for (int lobe = 0; lobe < lobes; ++lobe) {
        const auto next = static_cast<std::size_t>((lobe + 1) % lobes), here = static_cast<std::size_t>(lobe);
        item.cubics.push_back({Point{points[here].x + tangents[here].x, points[here].y + tangents[here].y},
                               Point{points[next].x - tangents[next].x, points[next].y - tangents[next].y}, points[next]});
    }
    return item;
}

std::vector<Scene> scenes() {
    std::vector<Scene> made;
    {
        Scene scene{"fills-1000", "1000 overlapping translucent blobs (6 cubics each, radius 10–90 px)", {}};
        Random random{{0xf111}};
        for (int index = 0; index < 1000; ++index) {
            const Point centre{random.between(0, side), random.between(0, side)};
            Item item = blob(random, centre, random.between(10, 90), 6);
            item.color = random.color(76, 230);
            scene.items.push_back(std::move(item));
        }
        made.push_back(std::move(scene));
    }
    {
        Scene scene{"strokes-500", "500 open cubic curves stroked 1–12 px wide, round caps and joins, translucent", {}};
        Random random{{0x5170}};
        for (int index = 0; index < 500; ++index) {
            Item item;
            item.kind = Item::Kind::stroke;
            item.closed = false;
            item.start = {random.between(0, side), random.between(0, side)};
            const auto near = [&](Point point, double reach) {
                return Point{static_cast<float>(std::clamp(point.x + random.between(-reach, reach), 0.f, static_cast<float>(side))),
                             static_cast<float>(std::clamp(point.y + random.between(-reach, reach), 0.f, static_cast<float>(side)))};
            };
            const Point end = near(item.start, 300);
            item.cubics.push_back({near(item.start, 250), near(end, 250), end});
            item.width = random.between(1, 12);
            item.color = random.color(128, 255);
            scene.items.push_back(std::move(item));
        }
        made.push_back(std::move(scene));
    }
    {
        Scene scene{"joins-300", "300 polylines of 3–7 sharp corners, a third closed, stroked 2–24 px with each join (miter, round, bevel) and cap (butt, round, square)", {}};
        Random random{{0x7015}};
        constexpr std::array joins{xrender::LineJoin::miter, xrender::LineJoin::round, xrender::LineJoin::bevel};
        constexpr std::array caps{xrender::LineCap::butt, xrender::LineCap::round, xrender::LineCap::square};
        for (int index = 0; index < 300; ++index) {
            Item item;
            item.kind = Item::Kind::stroke;
            item.closed = index % 3 == 0;
            item.start = {random.between(40, side - 40), random.between(40, side - 40)};
            const int corners = 3 + static_cast<int>(random.unit() * 5);
            for (int corner = 0; corner < corners; ++corner)
                item.lines.push_back({std::clamp(item.start.x + random.between(-160, 160), 0.f, static_cast<float>(side)),
                                      std::clamp(item.start.y + random.between(-160, 160), 0.f, static_cast<float>(side))});
            item.width = random.between(2, 24);
            item.join = joins[static_cast<std::size_t>(index) % 3];
            item.cap = caps[static_cast<std::size_t>(index / 3) % 3];
            item.color = random.color(128, 255);
            scene.items.push_back(std::move(item));
        }
        made.push_back(std::move(scene));
    }
    {
        Scene scene{"gradients-200", "200 blobs (8 cubics, radius 30–140 px) filled with two-stop linear gradients", {}};
        Random random{{0x96ad}};
        for (int index = 0; index < 200; ++index) {
            const Point centre{random.between(0, side), random.between(0, side)};
            const double radius = random.between(30, 140);
            Item item = blob(random, centre, radius, 8);
            item.kind = Item::Kind::gradient;
            item.color = random.color(255, 255);
            item.second = random.color(255, 255);
            item.from = {static_cast<float>(centre.x - radius), static_cast<float>(centre.y - radius)};
            item.to = {static_cast<float>(centre.x + radius), static_cast<float>(centre.y + radius)};
            scene.items.push_back(std::move(item));
        }
        made.push_back(std::move(scene));
    }
    {
        Scene scene{"small-5000", "5000 small opaque triangles (2–8 px): what each draw costs", {}};
        Random random{{0x5a11}};
        for (int index = 0; index < 5000; ++index) {
            Item item;
            item.start = {random.between(0, side), random.between(0, side)};
            const float size = random.between(2, 8);
            item.lines = {{item.start.x + size, item.start.y + random.between(0, size)}, {item.start.x + random.between(0, size), item.start.y + size}};
            item.color = random.color(255, 255);
            scene.items.push_back(std::move(item));
        }
        made.push_back(std::move(scene));
    }
    return made;
}

std::string path_data(const Item& item) {
    std::string data = std::format("M{} {}", item.start.x, item.start.y);
    for (const auto& cubic : item.cubics)
        data += std::format("C{} {} {} {} {} {}", cubic[0].x, cubic[0].y, cubic[1].x, cubic[1].y, cubic[2].x, cubic[2].y);
    for (const auto& point : item.lines) data += std::format("L{} {}", point.x, point.y);
    if (item.closed) data += "Z";
    return data;
}
std::string hex(Rgba color) { return std::format("#{:02x}{:02x}{:02x}", color.red, color.green, color.blue); }
std::string opacity(Rgba color) { return std::format("{}", color.alpha / 255.0); }
std::string_view cap_name(xrender::LineCap cap) { return cap == xrender::LineCap::butt ? "butt" : cap == xrender::LineCap::round ? "round" : "square"; }
std::string_view join_name(xrender::LineJoin join) { return join == xrender::LineJoin::miter ? "miter" : join == xrender::LineJoin::round ? "round" : "bevel"; }

std::string svg_of(const Scene& scene) {
    std::string out = std::format(R"(<svg xmlns="http://www.w3.org/2000/svg" width="{0}" height="{0}" viewBox="0 0 {0} {0}">)", side);
    out += std::format(R"(<rect width="{0}" height="{0}" fill="#ffffff"/>)", side);
    for (std::size_t index = 0; index < scene.items.size(); ++index) {
        const Item& item = scene.items[index];
        switch (item.kind) {
        case Item::Kind::fill:
            out += std::format(R"(<path d="{}" fill="{}" fill-opacity="{}"/>)", path_data(item), hex(item.color), opacity(item.color));
            break;
        case Item::Kind::stroke:
            out += std::format(R"(<path d="{}" fill="none" stroke="{}" stroke-opacity="{}" stroke-width="{}" stroke-linecap="{}" stroke-linejoin="{}" stroke-miterlimit="4"/>)",
                               path_data(item), hex(item.color), opacity(item.color), item.width, cap_name(item.cap), join_name(item.join));
            break;
        case Item::Kind::gradient:
            out += std::format(R"(<linearGradient id="g{}" gradientUnits="userSpaceOnUse" x1="{}" y1="{}" x2="{}" y2="{}"><stop offset="0" stop-color="{}"/><stop offset="1" stop-color="{}"/></linearGradient>)",
                               index, item.from.x, item.from.y, item.to.x, item.to.y, hex(item.color), hex(item.second));
            out += std::format(R"svg(<path d="{}" fill="url(#g{})"/>)svg", path_data(item), index);
            break;
        }
    }
    return out + "</svg>\n";
}

// For a canvas: each item's path data and how it is painted.
std::string canvas_of(const Scene& scene) {
    std::string out = std::format(R"({{"name":"{}","about":"{}","width":{},"height":{},"items":[)", scene.name, scene.about, side, side);
    for (std::size_t index = 0; index < scene.items.size(); ++index) {
        const Item& item = scene.items[index];
        const std::string color = std::format("rgba({},{},{},{})", item.color.red, item.color.green, item.color.blue, opacity(item.color));
        if (index) out += ",";
        switch (item.kind) {
        case Item::Kind::fill: out += std::format(R"({{"d":"{}","fill":"{}"}})", path_data(item), color); break;
        case Item::Kind::stroke:
            out += std::format(R"({{"d":"{}","stroke":"{}","width":{},"cap":"{}","join":"{}"}})", path_data(item), color, item.width, cap_name(item.cap),
                               join_name(item.join));
            break;
        case Item::Kind::gradient:
            out += std::format(R"({{"d":"{}","gradient":[{},{},{},{},"{}","{}"]}})", path_data(item), item.from.x, item.from.y, item.to.x, item.to.y,
                               hex(item.color), hex(item.second));
            break;
        }
    }
    return out + "]}\n";
}

xrender::Color color_of(Rgba color) { return xrender::Color::rgba8(color.red, color.green, color.blue, color.alpha); }

void record(const Scene& scene, xrender::DisplayList& list, std::vector<std::array<xrender::GradientStop, 2>>& stops) {
    stops.reserve(scene.items.size());
    for (const Item& item : scene.items) {
        xrender::Path path;
        path.move_to(item.start);
        for (const auto& cubic : item.cubics) path.cubic_to(cubic[0], cubic[1], cubic[2]);
        for (const auto& point : item.lines) path.line_to(point);
        if (item.closed) path.close();
        switch (item.kind) {
        case Item::Kind::fill: list.fill_path(path, xrender::FillRule::nonzero, xrender::Paint::solid(color_of(item.color))); break;
        case Item::Kind::stroke:
            list.stroke_path(path, {.width = item.width, .cap = item.cap, .join = item.join},
                             xrender::Paint::solid(color_of(item.color)));
            break;
        case Item::Kind::gradient: {
            stops.push_back({{{0, color_of(item.color)}, {1, color_of(item.second)}}});
            xrender::Paint paint;
            paint.kind = xrender::Paint::Kind::gradient;
            paint.gradient.start = item.from;
            paint.gradient.end = item.to;
            paint.gradient.stops = stops.back();
            list.fill_path(path, xrender::FillRule::nonzero, paint);
            break;
        }
        }
    }
}

// Where a scene's time goes (`--phases`), drawn straight on the raster
// canvas: making the outline (curves flattened, or the stroke's outline),
// rasterising it to runs of coverage, and the rest of the fill — compositing
// (the whole fill timed, less the two before it, measured again apart).
void phases(const Scene& scene, int runs) {
    namespace raster = xrender::raster;
    raster::Surface surface(side, side);
    raster::Canvas canvas(surface);
    raster::Polygons polygons;
    raster::Rasterizer rasterizer;
    raster::Spans spans;
    std::vector<raster::PathBuilder> paths(scene.items.size());
    std::vector<std::array<raster::Stop, 2>> stops(scene.items.size());
    std::vector<raster::Paint> paints(scene.items.size());
    for (std::size_t index = 0; index < scene.items.size(); ++index) {
        const Item& item = scene.items[index];
        auto& path = paths[index];
        path.move_to(item.start);
        for (const auto& cubic : item.cubics) path.cubic_to(cubic[0], cubic[1], cubic[2]);
        for (const auto& point : item.lines) path.line_to(point);
        if (item.closed) path.close();
        const auto color = [](Rgba c) { return mlibs::geom::Color::rgba8(c.red, c.green, c.blue, c.alpha); };
        paints[index] = raster::Paint::solid(color(item.color));
        if (item.kind == Item::Kind::gradient) {
            stops[index] = {{{0, color(item.color)}, {1, color(item.second)}}};
            paints[index].kind = raster::Paint::Kind::gradient;
            paints[index].start = item.from;
            paints[index].end = item.to;
            paints[index].stops = stops[index];
        }
    }
    const raster::PixelRect clip{0, 0, side, side};
    const auto stroke_of = [](const Item& item) {
        return raster::Stroke{.width = item.width, .cap = static_cast<raster::Cap>(item.cap), .join = static_cast<raster::Join>(item.join)};
    };
    std::vector<double> outline_ms, raster_ms, total_ms;
    std::size_t edges = 0, runs_made = 0;
    for (int run = 0; run < 3 + runs; ++run) {
        double outline = 0, rasterise = 0;
        edges = runs_made = 0;
        for (std::size_t index = 0; index < scene.items.size(); ++index) {
            const Item& item = scene.items[index];
            polygons.clear();
            const auto start = std::chrono::steady_clock::now();
            if (item.kind == Item::Kind::stroke) raster::stroke_outline(paths[index].data(), {}, stroke_of(item), polygons);
            else raster::flatten(paths[index].data(), {}, polygons);
            const auto made = std::chrono::steady_clock::now();
            rasterizer.rasterize(polygons, raster::FillRule::nonzero, clip, spans);
            const auto done = std::chrono::steady_clock::now();
            outline += std::chrono::duration<double, std::milli>(made - start).count();
            rasterise += std::chrono::duration<double, std::milli>(done - made).count();
            edges += polygons.points.size();
            runs_made += spans.runs.size();
        }
        surface.clear(raster::Rgba8{255, 255, 255, 255});
        const auto start = std::chrono::steady_clock::now();
        for (std::size_t index = 0; index < scene.items.size(); ++index) {
            const Item& item = scene.items[index];
            if (item.kind == Item::Kind::stroke) canvas.stroke_path(paths[index].data(), stroke_of(item), paints[index]);
            else canvas.fill_path(paths[index].data(), raster::FillRule::nonzero, paints[index]);
        }
        const double total = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        if (run >= 3) outline_ms.push_back(outline), raster_ms.push_back(rasterise), total_ms.push_back(total);
    }
    const auto median = [](std::vector<double> values) { std::ranges::sort(values); return values[values.size() / 2]; };
    const double outline = median(outline_ms), rasterise = median(raster_ms), total = median(total_ms);
    std::println(R"({{"scene":"{}","total_ms":{:.3f},"outline_ms":{:.3f},"rasterise_ms":{:.3f},"composite_ms":{:.3f},"points":{},"runs":{}}})", scene.name, total,
                 outline, rasterise, std::max(0.0, total - outline - rasterise), edges, runs_made);
}

bool write(const std::filesystem::path& file, std::string_view text) {
    std::ofstream out(file, std::ios::binary);
    out << text;
    return static_cast<bool>(out);
}

}  // namespace

int main(int argc, char** argv) {
    using mcpplibs::cmdline::App, mcpplibs::cmdline::Arg, mcpplibs::cmdline::Option;
    const auto line = App("omui-bench-vector")
        .description("xrender's CPU raster against the competitors' scenes")
        .arg(Arg("count").help("with --phases: runs a scene (default 9)"))
        .option(Option("out").takes_value().value_name("DIR").help("where the pictures and report go"))
        .option(Option("runs").value_type<int>().help("measured runs (default 15)"))
        .option(Option("only").takes_value().value_name("SCENE").help("only this scene"))
        .option(Option("phases").help("where each scene's time goes"));
    auto parsed = line.parse(argc, argv);
    if (!parsed) {
        if (!parsed.error().is_error()) return 0;
        std::println(std::cerr, "omui-bench-vector: {}", parsed.error().message);
        return 2;
    }
    if (parsed->is_flag_set("phases")) {
        int runs = 9;
        if (const std::string count = parsed->positional(0); !count.empty()) std::from_chars(count.data(), count.data() + count.size(), runs);
        for (const Scene& scene : scenes()) phases(scene, std::max(1, runs));
        return 0;
    }
    const std::filesystem::path out = parsed->value("out").value_or("");
    const std::string only = parsed->value("only").value_or("");
    const int runs = std::max(1, parsed->value_as<int>("runs").value_or(15));
    if (out.empty()) {
        std::println(std::cerr, "omui-bench-vector: --out DIR (omui-bench-vector --help)");
        return 2;
    }
    std::filesystem::create_directories(out);
    xrender::CpuRenderer renderer;
    const xrender::Resources resources{};
    for (const Scene& scene : scenes()) {
        if (!only.empty() && scene.name != only) continue;
        if (!write(out / (scene.name + ".svg"), svg_of(scene)) || !write(out / (scene.name + ".scene.json"), canvas_of(scene))) return 1;
        xrender::DisplayList list;
        std::vector<std::array<xrender::GradientStop, 2>> stops;
        record(scene, list, stops);
        xrender::raster::Surface surface(side, side);
        std::vector<double> times;
        for (int run = 0; run < 3 + runs; ++run) {
            const auto start = std::chrono::steady_clock::now();
            renderer.render(list, surface, resources, {.background = xrender::Color::white()});
            const auto spent = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            if (run >= 3) times.push_back(spent);
        }
        const mlibs::image::View view{surface.bytes().data(), side, side, side * 4, mlibs::image::Alpha::premultiplied};
        if (const auto saved = mlibs::image::save_png(out / (scene.name + ".xrender.png"), view); !saved) {
            std::println(std::cerr, "{}: {}", scene.name, saved.error());
            return 1;
        }
        const auto picture = xrender::svg::write(list, resources, {.view = {0, 0, side, side}, .background = xrender::Color::white(),
            .encode_png = [](std::span<const std::uint8_t> rgba, int width, int height) {
                return mlibs::image::encode_png({rgba.data(), width, height, width * 4, mlibs::image::Alpha::premultiplied});
            }});
        if (!picture || !write(out / (scene.name + ".xrender.svg"), picture->text)) return 1;
        double mean = 0;
        for (const double time : times) mean += time;
        mean /= static_cast<double>(times.size());
        std::ranges::sort(times);
        std::println(R"({{"renderer":"xrender","scene":"{}","items":{},"runs":{},"median_ms":{:.3f},"mean_ms":{:.3f},"min_ms":{:.3f}}})", scene.name,
                     scene.items.size(), runs, times[times.size() / 2], mean, times.front());
    }
    return 0;
}
