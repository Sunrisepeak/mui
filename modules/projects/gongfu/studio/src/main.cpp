// gongfu-studio — making fighters from the command line (D-61: JSON out and
// an exit code, for people, agents and CI alike).
//
//     gongfu-studio bake  <dir>     writes each <name>.boxes.gongfu.toml in <dir>
//     gongfu-studio check <dir>     whether they are what the files make now (exit 1 if not)
//     gongfu-studio embed <dir> --module <m> --namespace <ns> --out <file>
//                                   the fighters as the module partition <m>:gongfu_fighters,
//                                   refused when a boxes file is stale (what gongfu.rules runs)
//     gongfu-studio cut <sheet.png> --out <dir> [--key r,g,b] [--gap px] [--min px] [--alpha a] [--scale s]
//                                   a sheet of drawn or generated parts on a flat background: keyed out,
//                                   each piece written as its own PNG (piece-NN.png) and listed with its
//                                   region (JSON); a picture of the pieces numbered over the sheet
//     gongfu-studio sheet <dir> <name> --out <png> [--palette <n>]
//                                   every clip of <name>'s look with its boxes, as a contact sheet
//     gongfu-studio stage <dir> <name> --out <png> [--fighters <dir> --who a,b] [--at x] [--apart u] [--frame n]
//                                   a stage (<dir>/<name>.stage.gongfu.toml, its pictures beside it) as a
//                                   frame: two fighters for scale `apart` units apart about `at`, the
//                                   camera framing them as the game's does (none: the layers alone)
//     gongfu-studio versus <dir> [--rounds <n>] [--seed <s>] [--system <file>]
//                                   every pair of its fighters played by bots, <n> rounds each
//                                   (sides swapped every other): wins, damage, round length
import std;
import mcpplibs.cmdline;
import gongfu;
import gongfu.ai;
import gongfu.data;
import gongfu.studio;
import nlohmann.json;
import mlibs.image;
import xrender.raster;
import xrender.vector;

namespace {

namespace cmdline = mcpplibs::cmdline;

// A colour key `r,g,b`.
std::optional<mlibs::image::Key> key_of(const cmdline::ParsedArgs& args) {
    const auto text = args.value("key");
    if (!text) return std::nullopt;
    std::array<int, 3> channels{};
    if (std::sscanf(text->c_str(), "%d,%d,%d", &channels[0], &channels[1], &channels[2]) != 3) return std::nullopt;
    mlibs::image::Key key;
    key.r = static_cast<std::uint8_t>(channels[0]), key.g = static_cast<std::uint8_t>(channels[1]), key.b = static_cast<std::uint8_t>(channels[2]);
    return key;
}

std::vector<std::string> list_of(std::string_view text) {
    std::vector<std::string> out;
    for (auto part : text | std::views::split(',')) out.emplace_back(part.begin(), part.end());
    return out;
}

std::filesystem::path path_of(std::string_view s) { return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size())); }

std::string utf8(const std::filesystem::path& p) {
    const std::u8string u = p.generic_u8string();
    return {reinterpret_cast<const char*>(u.data()), u.size()};
}

std::string read(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

int usage(std::string_view what) {
    std::println(std::cerr, "gongfu-studio: {} (gongfu-studio <command> --help)", what);
    return 2;
}

int cut(const std::filesystem::path& file, const cmdline::ParsedArgs& args) {
    const std::filesystem::path out = path_of(args.value("out").value_or("."));
    const mlibs::image::Key key = key_of(args).value_or(mlibs::image::Key{});
    const std::int32_t gap = args.value_as<std::int32_t>("gap").value_or(8);
    const std::int64_t min_area = args.value_as<std::int64_t>("min").value_or(400);
    // A piece's pixels: alpha above this (a sheet's noise stays below).
    const auto solid = static_cast<std::uint8_t>(std::clamp(args.value_as<int>("alpha").value_or(96), 0, 254));
    // The atlas's size: the pieces at this scale, packed.
    const double scale = std::clamp(args.value_as<double>("scale").value_or(1.0), 0.05, 1.0);
    // > 0: the atlas as lossy WebP at this quality, else PNG.
    const float webp = args.value("webp") ? std::clamp(args.value_as<float>("webp").value_or(85.0f), 1.0f, 100.0f) : 0.0f;
    nlohmann::json report = {{"command", "cut"}, {"sheet", utf8(file)}};
    auto img = mlibs::image::load(file);
    if (!img) {
        report["error"] = img.error();
        std::println("{}", report.dump(2));
        return 1;
    }
    mlibs::image::key_out(*img, key);
    std::array<std::int64_t, 4> alphas{};  // clear, faint, half, solid: how well the background keyed
    for (std::size_t i = 3; i < img->rgba.size(); i += 4) ++alphas[img->rgba[i] == 0 ? 0 : img->rgba[i] <= solid ? 1 : img->rgba[i] < 255 ? 2 : 3];
    report["alpha"] = {{"clear", alphas[0]}, {"faint", alphas[1]}, {"partial", alphas[2]}, {"solid", alphas[3]}};
    std::vector<std::int32_t> labels;
    const auto found = mlibs::image::pieces(*img, solid, gap, min_area, &labels);
    std::error_code ec;
    std::filesystem::create_directories(out, ec);
    report["pieces"] = nlohmann::json::array();
    // Each piece alone (its own pixels: a neighbour reaching into its bounds is left out), at the size asked, then packed.
    std::vector<mlibs::image::Image> parts;
    for (std::size_t k = 0; k < found.size(); ++k) {
        const auto& p = found[k];
        auto part = mlibs::image::crop(*img, p.x, p.y, p.width, p.height);
        for (std::int32_t y = 0; y < p.height; ++y)
            for (std::int32_t x = 0; x < p.width; ++x)
                if (labels[static_cast<std::size_t>(p.y + y) * img->width + (p.x + x)] != static_cast<std::int32_t>(k))
                    part.rgba[(static_cast<std::size_t>(y) * p.width + x) * 4 + 3] = 0;
        const auto name = std::format("piece-{:02}.png", k);
        if (auto saved = mlibs::image::save_png(out / name, mlibs::image::view(part)); !saved) report["error"] = saved.error();
        if (scale < 1)
            part = mlibs::image::scaled_down(part, std::max(1, static_cast<std::int32_t>(std::lround(p.width * scale))),
                                              std::max(1, static_cast<std::int32_t>(std::lround(p.height * scale))));
        const auto ax = mlibs::image::principal_axis(part, solid);
        parts.push_back(std::move(part));
        // Its long axis (the piece's own pixels, as packed): a limb's joint lies near one end, its tip near the other.
        report["pieces"].push_back({{"file", name}, {"region", {p.x, p.y, p.width, p.height}}, {"pixels", p.area},
                                    {"axis_local", {std::lround(ax.from_x), std::lround(ax.from_y), std::lround(ax.to_x), std::lround(ax.to_y)}},
                                    {"half_width", std::lround(ax.half_width)}});
    }
    const auto packed = mlibs::image::pack(parts, static_cast<std::int32_t>(std::lround(2048 * scale)), 4);
    const std::string atlas = webp > 0 ? "atlas.webp" : "atlas.png";
    const auto saved = webp > 0 ? mlibs::image::save_webp(out / atlas, mlibs::image::view(packed.atlas), {.quality = webp})
                                : mlibs::image::save_png(out / atlas, mlibs::image::view(packed.atlas));
    if (!saved) report["error"] = saved.error();
    report["atlas"] = {{"file", atlas}, {"size", {packed.atlas.width, packed.atlas.height}}};
    for (std::size_t k = 0; k < found.size(); ++k)
    {
        report["pieces"][k]["atlas_region"] = {packed.at[k].x, packed.at[k].y, packed.at[k].width, packed.at[k].height};
        const auto& l = report["pieces"][k]["axis_local"];
        report["pieces"][k]["axis"] = {packed.at[k].x + l[0].get<long>(), packed.at[k].y + l[1].get<long>(), packed.at[k].x + l[2].get<long>(), packed.at[k].y + l[3].get<long>()};
    }
    report["ok"] = !report.contains("error");
    std::println("{}", report.dump(2));
    return report["ok"] ? 0 : 1;
}

// A picture made ready for a look: keyed out (a plain background), trimmed to
// what it shows, scaled down to a width, written as PNG or WebP by its name.
int picture(const std::filesystem::path& file, const cmdline::ParsedArgs& args) {
    const std::filesystem::path out = path_of(args.value("out").value_or(""));
    const std::optional<mlibs::image::Key> key = key_of(args);
    const bool trim = args.is_flag_set("trim");
    const std::int32_t width = args.value("width") ? std::max(1, args.value_as<std::int32_t>("width").value_or(1)) : 0;
    const float quality = std::clamp(args.value_as<float>("quality").value_or(85.0f), 1.0f, 100.0f);
    if (out.empty()) return usage("picture needs --out");
    nlohmann::json report = {{"command", "picture"}, {"from", utf8(file)}};
    auto img = mlibs::image::load(file);
    if (!img) {
        report["ok"] = false;
        report["error"] = img.error();
        std::println("{}", report.dump(2));
        return 1;
    }
    report["source_size"] = {img->width, img->height};
    if (key) mlibs::image::key_out(*img, *key);
    if (trim) {
        std::int32_t x0 = img->width, y0 = img->height, x1 = -1, y1 = -1;
        for (std::int32_t y = 0; y < img->height; ++y)
            for (std::int32_t x = 0; x < img->width; ++x)
                if (img->rgba[(static_cast<std::size_t>(y) * img->width + x) * 4 + 3] > 8)
                    x0 = std::min(x0, x), y0 = std::min(y0, y), x1 = std::max(x1, x), y1 = std::max(y1, y);
        if (x1 >= x0) *img = mlibs::image::crop(*img, x0, y0, x1 - x0 + 1, y1 - y0 + 1);
    }
    if (width > 0 && width < img->width)
        *img = mlibs::image::scaled_down(*img, width, std::max(1, static_cast<std::int32_t>(std::lround(static_cast<double>(img->height) * width / img->width))));
    const bool webp = out.extension() == ".webp";
    const auto saved = webp ? mlibs::image::save_webp(out, mlibs::image::view(*img), {.quality = quality}) : mlibs::image::save_png(out, mlibs::image::view(*img));
    report["ok"] = saved.has_value();
    if (!saved) report["error"] = saved.error();
    report["file"] = utf8(out);
    report["size"] = {img->width, img->height};
    std::error_code ec;
    report["bytes"] = std::filesystem::file_size(out, ec);
    std::println("{}", report.dump(2));
    return saved ? 0 : 1;
}

// A look's or a stage's pictures, from beside its file, registered with
// `images` (premultiplied), each with its relief when it has one, as the
// game makes it.
std::expected<void, std::string> load_pictures(std::vector<gongfu::present2d::Picture>& pictures, const std::filesystem::path& dir,
                                               xrender::ImageStore& images) {
    auto pixels = [](const mlibs::image::Image& i) {
        std::vector<xrender::raster::Rgba8> px(static_cast<std::size_t>(i.width) * static_cast<std::size_t>(i.height));
        std::memcpy(px.data(), i.rgba.data(), i.rgba.size());
        return px;
    };
    for (gongfu::present2d::Picture& pic : pictures) {
        auto img = mlibs::image::load(dir / path_of(pic.file));
        if (!img) return std::unexpected(std::format("picture `{}` ({}): {}", pic.name, pic.file, img.error()));
        if (const auto& r = pic.relief) {
            const auto n = mlibs::image::normal_map(*img, {.bevel = r->bevel, .detail = r->detail, .smooth = r->smooth, .strength = r->strength});
            pic.normal = images.add(n.width, n.height, pixels(n));
        }
        mlibs::image::premultiply(*img);
        pic.ref = images.add(img->width, img->height, pixels(*img));
    }
    return {};
}

int sheet(const std::filesystem::path& dir, const cmdline::ParsedArgs& args) {
    const std::string name = args.positional(1);
    const std::filesystem::path out = path_of(args.value("out").value_or(""));
    const std::size_t palette = args.value_as<std::size_t>("palette").value_or(0);
    gongfu::studio::SheetOptions options;
    if (auto clips = args.value("clips")) options.clips = list_of(*clips);
    if (auto frames = args.value("frames"))
        for (const std::string& frame : list_of(*frames)) {
            int index = 0;
            if (std::from_chars(frame.data(), frame.data() + frame.size(), index).ec == std::errc{}) options.frames.push_back(index);
        }
    if (auto cell = args.value_as<float>("cell")) options.cell = *cell;
    if (auto columns = args.value_as<int>("columns")) options.columns = *columns;
    if (auto boxes = args.value("boxes")) options.boxes = *boxes != "false";
    if (name.empty() || out.empty()) return usage("sheet needs a fighter's name and --out");
    const auto file = [&](std::string_view kind) { return dir / path_of(std::format("{}.{}.gongfu.toml", name, kind)); };
    const std::string boxes = std::filesystem::exists(file("boxes")) ? read(file("boxes")) : std::string();
    auto fighter = gongfu::data::read_fighter(read(file("fighter")), utf8(file("fighter").filename()), boxes, utf8(file("boxes").filename()));
    auto look = gongfu::present2d::read_look(read(file("look")), utf8(file("look").filename()));
    nlohmann::json report = {{"command", "sheet"}, {"fighter", name}};
    if (!fighter || !look) {
        std::vector<std::string> problems;
        if (!fighter)
            for (const auto& d : fighter.error()) problems.push_back(d.text());
        if (!look)
            for (const auto& d : look.error()) problems.push_back(d.text());
        report["ok"] = false;
        report["problems"] = problems;
        std::println("{}", report.dump(2));
        return 1;
    }
    gongfu::present2d::use_palette(*look, palette);
    // The look's drawn art: its pictures, from beside the look file.
    xrender::ImageStore images;
    if (auto loaded = load_pictures(look->pictures, dir, images); !loaded) {
        report["ok"] = false;
        report["problems"] = {loaded.error()};
        std::println("{}", report.dump(2));
        return 1;
    }
    options.images = &images;
    auto s = gongfu::studio::sheet(*fighter, *look, options);
    if (!s) {
        report["ok"] = false;
        report["problems"] = {s.error()};
        std::println("{}", report.dump(2));
        return 1;
    }
    std::ofstream f(out, std::ios::binary);
    f.write(reinterpret_cast<const char*>(s->png.data()), static_cast<std::streamsize>(s->png.size()));
    report["ok"] = static_cast<bool>(f);
    report["png"] = utf8(out);
    report["size"] = {s->width, s->height};
    report["cells"] = s->cells;
    std::println("{}", report.dump(2));
    return f ? 0 : 1;
}

int embed(const std::filesystem::path& dir, const cmdline::ParsedArgs& args) {
    gongfu::studio::EmbedOptions o;
    o.module = args.value("module").value_or("");
    o.ns = args.value("namespace").value_or("");
    const std::filesystem::path out = path_of(args.value("out").value_or(""));
    if (o.module.empty() || o.ns.empty() || out.empty()) return usage("embed needs --module, --namespace and --out");
    auto text = gongfu::studio::partition(dir, o);
    if (!text) {
        for (const auto& p : text.error()) std::println(std::cerr, "gongfu-studio: {}", p);
        return 1;
    }
    std::error_code ec;
    std::filesystem::create_directories(out.parent_path(), ec);
    std::ofstream f(out, std::ios::binary);
    f << *text;
    return f ? 0 : 1;
}

// Every pair of a directory's fighters, played by bots.
int stage(const std::filesystem::path& dir, const cmdline::ParsedArgs& args) {
    const std::string name = args.positional(1);
    const std::filesystem::path out = path_of(args.value("out").value_or(""));
    const std::filesystem::path fighters_dir = path_of(args.value("fighters").value_or(""));
    const std::vector<std::string> who = list_of(args.value("who").value_or(""));
    const double at = args.value_as<double>("at").value_or(960), apart = args.value_as<double>("apart").value_or(300);
    const std::int32_t frame = args.value_as<std::int32_t>("frame").value_or(0);
    if (name.empty() || out.empty()) return usage("stage needs a stage's name and --out");
    nlohmann::json report = {{"command", "stage"}, {"stage", name}};
    auto fail = [&](std::vector<std::string> problems) {
        report["ok"] = false;
        report["problems"] = problems;
        std::println("{}", report.dump(2));
        return 1;
    };
    const auto file = dir / path_of(std::format("{}.stage.gongfu.toml", name));
    auto st = gongfu::present2d::read_stage(read(file), utf8(file.filename()));
    if (!st) {
        std::vector<std::string> problems;
        for (const auto& d : st.error()) problems.push_back(d.text());
        return fail(problems);
    }
    xrender::ImageStore images;
    if (auto loaded = load_pictures(st->pictures, dir, images); !loaded) return fail({loaded.error()});
    // Two fighters for scale (each its first palette), else the first fighter twice, standing.
    std::vector<gongfu::Fighter> fighters;
    std::vector<gongfu::present2d::Look> looks;
    for (const std::string& f : who) {
        const auto path = [&](std::string_view kind) { return fighters_dir / path_of(std::format("{}.{}.gongfu.toml", f, kind)); };
        const std::string boxes = std::filesystem::exists(path("boxes")) ? read(path("boxes")) : std::string();
        auto fighter = gongfu::data::read_fighter(read(path("fighter")), utf8(path("fighter").filename()), boxes, utf8(path("boxes").filename()));
        auto look = gongfu::present2d::read_look(read(path("look")), utf8(path("look").filename()));
        if (!fighter || !look) return fail({std::format("fighter `{}` does not read", f)});
        gongfu::present2d::bind(*look, *fighter);
        if (auto loaded = load_pictures(look->pictures, fighters_dir, images); !loaded) return fail({loaded.error()});
        fighters.push_back(std::move(*fighter));
        looks.push_back(std::move(*look));
    }
    xrender::DisplayList dl;
    gongfu::present2d::Scene scene{.stage = &*st};
    gongfu::present2d::Painter painter;
    if (!fighters.empty()) {
        const std::size_t b = fighters.size() > 1 ? 1 : 0;
        gongfu::SystemDef system;
        gongfu::Match m(system, fighters[0], fighters[b]);
        gongfu::State s = m.state();
        s.f[0].pos.x = gongfu::Fixed::of(static_cast<std::int32_t>(std::lround(at - apart / 2)));
        s.f[1].pos.x = gongfu::Fixed::of(static_cast<std::int32_t>(std::lround(at + apart / 2)));
        s.frame = frame;
        m.restore(s);
        gongfu::present2d::Effects effects;
        painter.draw(m, looks[0], looks[b], effects, scene, dl);
    } else {
        const gongfu::present2d::Camera cam{at, 0, 1};
        dl.fill_rect({0, 0, scene.width, scene.height}, st->sky_bottom);
        painter.stage_layers(*st, false, scene, cam, 960, frame, 1, dl);
        painter.stage_layers(*st, true, scene, cam, 960, frame, 1, dl);
    }
    xrender::raster::Surface px(static_cast<int>(scene.width), static_cast<int>(scene.height));
    xrender::CpuRenderer{}.render(dl, px, {&images, nullptr}, {.scale = 1});
    const mlibs::image::View v{reinterpret_cast<const std::uint8_t*>(px.row(0)), px.width(), px.height(), px.width() * 4, mlibs::image::Alpha::premultiplied};
    auto png = mlibs::image::encode_png(v);
    if (!png) return fail({"the frame did not encode as PNG"});
    std::ofstream f(out, std::ios::binary);
    f.write(reinterpret_cast<const char*>(png->data()), static_cast<std::streamsize>(png->size()));
    report["ok"] = static_cast<bool>(f);
    report["png"] = utf8(out);
    std::println("{}", report.dump(2));
    return f ? 0 : 1;
}

int versus(const std::filesystem::path& dir, const cmdline::ParsedArgs& args) {
    const std::int32_t rounds = std::max(1, args.value_as<std::int32_t>("rounds").value_or(200));
    const std::uint64_t seed = args.value_as<std::uint64_t>("seed").value_or(1);
    const std::filesystem::path system_file = path_of(args.value("system").value_or(""));
    nlohmann::json report = {{"command", "versus"}, {"dir", utf8(dir)}, {"rounds", rounds}, {"seed", seed}};
    gongfu::SystemDef system;
    if (!system_file.empty()) {
        auto read_system = gongfu::data::read_system(read(system_file), utf8(system_file.filename()));
        if (!read_system) {
            std::vector<std::string> problems;
            for (const auto& d : read_system.error()) problems.push_back(d.text());
            report["ok"] = false;
            report["problems"] = problems;
            std::println("{}", report.dump(2));
            return 1;
        }
        system = *read_system;
    }
    std::vector<gongfu::Fighter> fighters;
    std::vector<std::string> names, problems;
    for (const auto& f : gongfu::studio::fighters_in(dir)) {
        const std::string boxes = std::filesystem::exists(f.boxes) ? read(f.boxes) : std::string();
        auto one = gongfu::data::read_fighter(read(f.fighter), utf8(f.fighter.filename()), boxes, utf8(f.boxes.filename()));
        if (!one) {
            for (const auto& d : one.error()) problems.push_back(d.text());
            continue;
        }
        fighters.push_back(std::move(*one));
        names.push_back(f.name);
    }
    if (!problems.empty() || fighters.size() < 2) {
        report["ok"] = false;
        report["problems"] = problems.empty() ? std::vector<std::string>{"two fighters or more are played against each other"} : problems;
        std::println("{}", report.dump(2));
        return 1;
    }
    // Each pair on its own thread: a pair's rounds depend on nothing else, so the report is the same however they run.
    struct Pair {
        std::size_t a, b;
        gongfu::ai::Record record;
    };
    std::vector<Pair> pairs;
    for (std::size_t a = 0; a < fighters.size(); ++a)
        for (std::size_t b = a + 1; b < fighters.size(); ++b) pairs.push_back({a, b, {}});
    {
        std::vector<std::jthread> workers;
        for (Pair& p : pairs)
            workers.emplace_back([&, at = &p] { at->record = gongfu::ai::versus(system, fighters[at->a], fighters[at->b], rounds, seed); });
    }
    std::vector<double> wins(fighters.size(), 0.0);
    std::vector<std::int32_t> played(fighters.size(), 0);
    report["pairs"] = nlohmann::json::array();
    for (const Pair& p : pairs) {
        const auto& r = p.record;
        report["pairs"].push_back({{"a", names[p.a]},
                                   {"b", names[p.b]},
                                   {"wins", {r.wins[0], r.wins[1]}},
                                   {"draws", r.draws},
                                   {"win_rate_a", r.win_rate(0)},
                                   {"damage_per_round", {static_cast<double>(r.damage[0]) / r.rounds, static_cast<double>(r.damage[1]) / r.rounds}},
                                   {"supers_per_round", {static_cast<double>(r.supers[0]) / r.rounds, static_cast<double>(r.supers[1]) / r.rounds}},
                                   {"seconds_per_round", static_cast<double>(r.frames) / r.rounds / 60.0}});
        wins[p.a] += r.win_rate(0) * r.rounds;
        wins[p.b] += r.win_rate(1) * r.rounds;
        played[p.a] += r.rounds;
        played[p.b] += r.rounds;
    }
    // Where each fighter's damage comes from, over all its rounds: its moves, most first.
    std::vector<std::vector<std::int64_t>> damage(fighters.size());
    for (std::size_t i = 0; i < fighters.size(); ++i) damage[i].assign(fighters[i].def.moves.size(), 0);
    for (const Pair& p : pairs)
        for (std::size_t m = 0; m < damage[p.a].size(); ++m) damage[p.a][m] += p.record.by_move[0][m], damage[p.b][m < damage[p.b].size() ? m : 0] += 0;
    for (const Pair& p : pairs)
        for (std::size_t m = 0; m < damage[p.b].size(); ++m) damage[p.b][m] += p.record.by_move[1][m];
    report["fighters"] = nlohmann::json::array();
    for (std::size_t i = 0; i < fighters.size(); ++i) {
        std::vector<std::pair<std::int64_t, std::string>> from;
        std::int64_t total = 0;
        for (std::size_t m = 0; m < damage[i].size(); ++m) total += damage[i][m];
        for (std::size_t m = 0; m < damage[i].size(); ++m)
            if (damage[i][m] > 0) from.emplace_back(damage[i][m], fighters[i].def.moves[m].name);
        std::ranges::sort(from, std::greater<>{});
        nlohmann::json moves = nlohmann::json::array();
        for (std::size_t k = 0; k < std::min<std::size_t>(from.size(), 8); ++k)
            moves.push_back({{"move", from[k].second}, {"share", total > 0 ? static_cast<double>(from[k].first) / static_cast<double>(total) : 0.0}});
        report["fighters"].push_back({{"name", names[i]}, {"win_rate", wins[i] / played[i]}, {"damage_from", moves}});
    }
    report["ok"] = true;
    std::println("{}", report.dump(2));
    return 0;
}

// The command line (mcpplibs.cmdline): a subcommand each, its options.
cmdline::App command_line() {
    using cmdline::App, cmdline::Arg, cmdline::Option;
    const auto fighters = Arg("dir").required().help("a directory of fighters");
    return App("gongfu-studio")
        .description("Making fighters from the command line: JSON out, exit 0 done / 1 failed / 2 a wrong command")
        .subcommand(App("bake").description("Write each <name>.boxes.gongfu.toml in the directory").arg(fighters))
        .subcommand(App("check").description("Whether the baked boxes are what the files make now (exit 1 if not)").arg(fighters))
        .subcommand(App("embed")
                        .description("The fighters as the module partition <m>:gongfu_fighters, refused while a bake is stale")
                        .arg(fighters)
                        .option(Option("module").takes_value().value_name("M").help("the program's module"))
                        .option(Option("namespace").takes_value().value_name("NS").help("its namespace"))
                        .option(Option("out").takes_value().value_name("FILE").help("the .cppm written")))
        .subcommand(App("cut")
                        .description("A sheet of parts on a flat background: keyed out, each piece a PNG with its region (JSON)")
                        .arg(Arg("sheet").required().help("the sheet (PNG)"))
                        .option(Option("out").takes_value().value_name("DIR").help("where the pieces go (default: here)"))
                        .option(Option("key").takes_value().value_name("R,G,B").help("the background's colour"))
                        .option(Option("gap").value_type<std::int32_t>().value_name("PX").help("pixels between pieces (default 8)"))
                        .option(Option("min").value_type<std::int64_t>().value_name("PX").help("the least area of a piece (default 400)"))
                        .option(Option("alpha").value_type<int>().value_name("A").help("a piece's pixels: alpha above this (default 96)"))
                        .option(Option("scale").value_type<double>().value_name("S").help("the atlas at this scale, 0.05 … 1"))
                        .option(Option("webp").value_type<float>().value_name("Q").help("the atlas as lossy WebP at this quality")))
        .subcommand(App("picture")
                        .description("A picture made ready for a look: keyed out, trimmed, scaled, as PNG or WebP")
                        .arg(Arg("file").required().help("the picture"))
                        .option(Option("out").takes_value().value_name("FILE").help("the picture written (.png or .webp)"))
                        .option(Option("key").takes_value().value_name("R,G,B").help("the background's colour"))
                        .option(Option("trim").help("trimmed to what it shows"))
                        .option(Option("width").value_type<std::int32_t>().value_name("PX").help("scaled down to this width"))
                        .option(Option("quality").value_type<float>().value_name("Q").help("WebP's quality (default 85)")))
        .subcommand(App("sheet")
                        .description("Every clip of a fighter's look with its boxes, as a contact sheet")
                        .arg(fighters)
                        .arg(Arg("name").required().help("the fighter"))
                        .option(Option("out").takes_value().value_name("PNG").help("the sheet written"))
                        .option(Option("palette").value_type<std::size_t>().value_name("I").help("which palette"))
                        .option(Option("clips").takes_value().value_name("A,B").help("only these clips"))
                        .option(Option("frames").takes_value().value_name("0,12").help("only these frames"))
                        .option(Option("cell").value_type<float>().value_name("PX").help("a cell's size"))
                        .option(Option("columns").value_type<int>().help("cells a row"))
                        .option(Option("boxes").takes_value().value_name("BOOL").help("false: the look alone")))
        .subcommand(App("stage")
                        .description("A stage as a frame: two fighters for scale, framed as the game's camera does")
                        .arg(Arg("dir").required().help("the stages' directory"))
                        .arg(Arg("name").required().help("the stage (<dir>/<name>.stage.gongfu.toml)"))
                        .option(Option("out").takes_value().value_name("PNG").help("the frame written"))
                        .option(Option("fighters").takes_value().value_name("DIR").help("a directory of fighters"))
                        .option(Option("who").takes_value().value_name("A,B").help("the two fighters from it"))
                        .option(Option("at").value_type<double>().value_name("X").help("about where (default 960)"))
                        .option(Option("apart").value_type<double>().value_name("U").help("units apart (default 300)"))
                        .option(Option("frame").value_type<std::int32_t>().help("the stage's animation frame")))
        .subcommand(App("versus")
                        .description("Every pair of the fighters played by bots: wins, damage, round length")
                        .arg(fighters)
                        .option(Option("rounds").value_type<std::int32_t>().help("rounds a pair (default 200)"))
                        .option(Option("seed").value_type<std::uint64_t>().value_name("S").help("the bots' seed (default 1)"))
                        .option(Option("system").takes_value().value_name("FILE").help("a system file to play under")));
}

}  // namespace

int main(int argc, char** argv) {
    const cmdline::App app = command_line();
    auto parsed = app.parse(argc, argv);
    if (!parsed) {
        if (!parsed.error().is_error()) return 0;  // --help
        return usage(parsed.error().message);
    }
    if (!parsed->has_subcommand()) return usage("a command: bake, check, embed, cut, picture, sheet, stage or versus");
    const std::string command(parsed->subcommand_name());
    const cmdline::ParsedArgs& args = parsed->subcommand()->get();
    const std::filesystem::path dir = path_of(args.positional(0));
    if (command == "embed") return embed(dir, args);
    if (command == "sheet") return sheet(dir, args);
    if (command == "cut") return cut(dir, args);
    if (command == "picture") return picture(dir, args);
    if (command == "versus") return versus(dir, args);
    if (command == "stage") return stage(dir, args);

    nlohmann::json report = {{"command", command}, {"dir", utf8(dir)}, {"fighters", nlohmann::json::array()}};
    bool ok = true;
    for (const auto& f : gongfu::studio::fighters_in(dir)) {
        nlohmann::json one = {{"name", f.name}, {"boxes", utf8(f.boxes.filename())}};
        auto text = gongfu::studio::baked_text(f);
        if (!text) {
            one["status"] = "problems";
            one["problems"] = text.error();
            ok = false;
        } else if (command == "bake") {
            std::ofstream out(f.boxes, std::ios::binary);
            out << *text;
            one["status"] = out ? "written" : "not written";
            ok = ok && static_cast<bool>(out);
        } else {
            const bool same = std::filesystem::exists(f.boxes) && read(f.boxes) == *text;
            one["status"] = same ? "current" : "stale";
            if (!same) one["fix"] = std::format("gongfu-studio bake {}", utf8(dir));
            ok = ok && same;
        }
        report["fighters"].push_back(std::move(one));
    }
    report["ok"] = ok;
    std::println("{}", report.dump(2));
    if (!ok && command == "check")
        std::println(std::cerr, "gongfu-studio: baked hurtboxes in {} are not what the looks and moves make now — run `gongfu-studio bake {}`", utf8(dir), utf8(dir));
    return ok ? 0 : 1;
}
