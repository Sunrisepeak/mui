// gongfu.studio's subject `gongfu` (tool.cppm): the commands that make fighters, each answering its report (JSON) as a
// record, and its exit code — 0 done, 1 failed, 2 a request that lacks what it needs.
module gongfu.studio;

import std;
import gongfu;
import gongfu.ai;
import gongfu.data;
import nlohmann.json;
import mlibs.image;
import xrender.raster;
import xrender.vector;

namespace {

using mlibs::agent::Arguments;

// A number given as a parameter (as written: `--gap 8`), when it reads as one.
template <class T>
std::optional<T> number(const Arguments& args, std::string_view name) {
    const auto text = args.value(name);
    if (!text) return std::nullopt;
    T value{};
    const auto [end, error] = std::from_chars(text->data(), text->data() + text->size(), value);
    if (error != std::errc{} || end != text->data() + text->size()) return std::nullopt;
    return value;
}

// A colour key `r,g,b`.
std::optional<mlibs::image::Key> key_of(const Arguments& args) {
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

// What a request lacks, said: the failure's text (the catalog answers what it needs before it runs).
int usage(std::ostream& err, std::string_view what) {
    err << what << '\n';
    return 2;
}

int cut(const std::filesystem::path& file, const Arguments& args, std::ostream& records, std::ostream& err) {
    const std::filesystem::path out = path_of(args.value("out").value_or("."));
    const mlibs::image::Key key = key_of(args).value_or(mlibs::image::Key{});
    const std::int32_t gap = number<std::int32_t>(args, "gap").value_or(8);
    const std::int64_t min_area = number<std::int64_t>(args, "min").value_or(400);
    // A piece's pixels: alpha above this (a sheet's noise stays below).
    const auto solid = static_cast<std::uint8_t>(std::clamp(number<int>(args, "alpha").value_or(96), 0, 254));
    // The atlas's size: the pieces at this scale, packed.
    const double scale = std::clamp(number<double>(args, "scale").value_or(1.0), 0.05, 1.0);
    // > 0: the atlas as lossy WebP at this quality, else PNG.
    const float webp = args.value("webp") ? std::clamp(number<float>(args, "webp").value_or(85.0f), 1.0f, 100.0f) : 0.0f;
    nlohmann::json report = {{"command", "cut"}, {"sheet", utf8(file)}};
    auto img = mlibs::image::load(file);
    if (!img) {
        report["error"] = img.error();
        records << report.dump() << '\n';
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
    records << report.dump() << '\n';
    return report["ok"] ? 0 : 1;
}

// A picture made ready for a look: keyed out (a plain background), trimmed to
// what it shows, scaled down to a width, written as PNG or WebP by its name.
int picture(const std::filesystem::path& file, const Arguments& args, std::ostream& records, std::ostream& err) {
    const std::filesystem::path out = path_of(args.value("out").value_or(""));
    const std::optional<mlibs::image::Key> key = key_of(args);
    const bool trim = args.flag("trim");
    const std::int32_t width = args.value("width") ? std::max(1, number<std::int32_t>(args, "width").value_or(1)) : 0;
    const float quality = std::clamp(number<float>(args, "quality").value_or(85.0f), 1.0f, 100.0f);
    if (out.empty()) return usage(err, "picture needs --out");
    nlohmann::json report = {{"command", "picture"}, {"from", utf8(file)}};
    auto img = mlibs::image::load(file);
    if (!img) {
        report["ok"] = false;
        report["error"] = img.error();
        records << report.dump() << '\n';
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
    records << report.dump() << '\n';
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

int sheet(const std::filesystem::path& dir, const Arguments& args, std::ostream& records, std::ostream& err) {
    const std::string name = args.value("name").value_or("");
    const std::filesystem::path out = path_of(args.value("out").value_or(""));
    const std::size_t palette = number<std::size_t>(args, "palette").value_or(0);
    gongfu::studio::SheetOptions options;
    if (auto clips = args.value("clips")) options.clips = list_of(*clips);
    if (auto frames = args.value("frames"))
        for (const std::string& frame : list_of(*frames)) {
            int index = 0;
            if (std::from_chars(frame.data(), frame.data() + frame.size(), index).ec == std::errc{}) options.frames.push_back(index);
        }
    if (auto cell = number<float>(args, "cell")) options.cell = *cell;
    if (auto columns = number<int>(args, "columns")) options.columns = *columns;
    if (auto boxes = args.value("boxes")) options.boxes = *boxes != "false";
    if (name.empty() || out.empty()) return usage(err, "sheet needs a fighter's name and --out");
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
        records << report.dump() << '\n';
        return 1;
    }
    gongfu::present2d::use_palette(*look, palette);
    // The look's drawn art: its pictures, from beside the look file.
    xrender::ImageStore images;
    if (auto loaded = load_pictures(look->pictures, dir, images); !loaded) {
        report["ok"] = false;
        report["problems"] = {loaded.error()};
        records << report.dump() << '\n';
        return 1;
    }
    options.images = &images;
    auto s = gongfu::studio::sheet(*fighter, *look, options);
    if (!s) {
        report["ok"] = false;
        report["problems"] = {s.error()};
        records << report.dump() << '\n';
        return 1;
    }
    std::ofstream f(out, std::ios::binary);
    f.write(reinterpret_cast<const char*>(s->png.data()), static_cast<std::streamsize>(s->png.size()));
    report["ok"] = static_cast<bool>(f);
    report["png"] = utf8(out);
    report["size"] = {s->width, s->height};
    report["cells"] = s->cells;
    records << report.dump() << '\n';
    return f ? 0 : 1;
}

int embed(const std::filesystem::path& dir, const Arguments& args, std::ostream& records, std::ostream& err) {
    gongfu::studio::EmbedOptions o;
    o.module = args.value("module").value_or("");
    o.ns = args.value("namespace").value_or("");
    const std::filesystem::path out = path_of(args.value("out").value_or(""));
    if (o.module.empty() || o.ns.empty() || out.empty()) return usage(err, "embed needs --module, --namespace and --out");
    auto text = gongfu::studio::partition(dir, o);
    if (!text) {
        for (const auto& p : text.error()) err << "gongfu-studio: " << p << '\n';
        return 1;
    }
    std::error_code ec;
    std::filesystem::create_directories(out.parent_path(), ec);
    std::ofstream f(out, std::ios::binary);
    f << *text;
    return f ? 0 : 1;
}

// Every pair of a directory's fighters, played by bots.
int stage(const std::filesystem::path& dir, const Arguments& args, std::ostream& records, std::ostream& err) {
    const std::string name = args.value("name").value_or("");
    const std::filesystem::path out = path_of(args.value("out").value_or(""));
    const std::filesystem::path fighters_dir = path_of(args.value("fighters").value_or(""));
    const std::vector<std::string> who = list_of(args.value("who").value_or(""));
    const double at = number<double>(args, "at").value_or(960), apart = number<double>(args, "apart").value_or(300);
    const std::int32_t frame = number<std::int32_t>(args, "frame").value_or(0);
    if (name.empty() || out.empty()) return usage(err, "stage needs a stage's name and --out");
    nlohmann::json report = {{"command", "stage"}, {"stage", name}};
    auto fail = [&](std::vector<std::string> problems) {
        report["ok"] = false;
        report["problems"] = problems;
        records << report.dump() << '\n';
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
    records << report.dump() << '\n';
    return f ? 0 : 1;
}

int versus(const std::filesystem::path& dir, const Arguments& args, std::ostream& records, std::ostream& err) {
    const std::int32_t rounds = std::max(1, number<std::int32_t>(args, "rounds").value_or(200));
    const std::uint64_t seed = number<std::uint64_t>(args, "seed").value_or(1);
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
            records << report.dump() << '\n';
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
        records << report.dump() << '\n';
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
    records << report.dump() << '\n';
    return 0;
}

// fighter.check and fighter.bake: each fighter's baked hurtboxes, as the files make them now — said current or
// stale (check), or written (bake).
int bake_or_check(bool bake, const std::filesystem::path& dir, std::ostream& records, std::ostream& err) {
    nlohmann::json report = {{"command", bake ? "fighter.bake" : "fighter.check"}, {"dir", utf8(dir)}, {"fighters", nlohmann::json::array()}};
    bool ok = true;
    for (const auto& f : gongfu::studio::fighters_in(dir)) {
        nlohmann::json one = {{"name", f.name}, {"boxes", utf8(f.boxes.filename())}};
        auto text = gongfu::studio::baked_text(f);
        if (!text) {
            one["status"] = "problems";
            one["problems"] = text.error();
            ok = false;
        } else if (bake) {
            std::ofstream written(f.boxes, std::ios::binary);
            written << *text;
            one["status"] = written ? "written" : "not written";
            ok = ok && static_cast<bool>(written);
        } else {
            const bool same = std::filesystem::exists(f.boxes) && read(f.boxes) == *text;
            one["status"] = same ? "current" : "stale";
            if (!same) one["fix"] = std::format("gongfu-studio fighter.bake {}", utf8(dir));
            ok = ok && same;
        }
        report["fighters"].push_back(std::move(one));
    }
    report["ok"] = ok;
    records << report.dump() << '\n';
    if (!ok && !bake)
        err << std::format("baked hurtboxes in {} are not what the looks and moves make now — run `gongfu-studio fighter.bake {}`\n", utf8(dir), utf8(dir));
    return ok ? 0 : 1;
}

}  // namespace

namespace gongfu::studio {

mlibs::control::Guide Studio::guide() const noexcept {
    static constexpr std::array<std::string_view, 2> keep{R"({"op":"fighter.check","dir":"showcase/fighting/fighters"})",
                                                          R"({"op":"fighter.bake","dir":"showcase/fighting/fighters"})"};
    static constexpr std::array<mlibs::control::Task, 1> tasks{{{"keep-the-bakes-current", "Whether the baked hurtboxes are current, then bake them again", keep}}};
    return {ops::guide.about, tasks};
}

void Studio::operate(std::string_view op, const mlibs::agent::Request& request, mlibs::agent::Answer& answer) {
    std::ostringstream records, err;
    const Arguments args(request);
    const std::filesystem::path dir = path_of(args.value("dir").value_or(""));
    int code = 2;
    if (op == "fighter.check" || op == "fighter.bake") code = ::bake_or_check(op == "fighter.bake", dir, records, err);
    else if (op == "fighter.embed") code = ::embed(dir, args, records, err);
    else if (op == "fighter.sheet") code = ::sheet(dir, args, records, err);
    else if (op == "stage.picture") code = ::stage(dir, args, records, err);
    else if (op == "art.cut") code = ::cut(path_of(args.value("sheet").value_or("")), args, records, err);
    else if (op == "art.picture") code = ::picture(path_of(args.value("file").value_or("")), args, records, err);
    else if (op == "roster.versus") code = ::versus(dir, args, records, err);
    std::istringstream lines(records.str());
    for (std::string line; std::getline(lines, line);)
        if (!line.empty()) answer.data(line);
    if (code != 0) {
        std::string said = err.str();
        while (!said.empty() && said.back() == '\n') said.pop_back();
        answer.fail(code == 2 ? std::string_view("invalid-argument") : std::string_view("failed"), said.empty() ? std::format("{} failed (its report says why)", op) : said);
    }
}

}  // namespace gongfu::studio
