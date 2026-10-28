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
//     gongfu-studio versus <dir> [--rounds <n>] [--seed <s>] [--system <file>]
//                                   every pair of its fighters played by bots, <n> rounds each
//                                   (sides swapped every other): wins, damage, round length
import std;
import gongfu;
import gongfu.ai;
import gongfu.data;
import gongfu.studio;
import nlohmann.json;
import openmui.image;

namespace {

std::filesystem::path path_of(std::string_view s) { return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size())); }

std::string utf8(const std::filesystem::path& p) {
    const std::u8string u = p.generic_u8string();
    return {reinterpret_cast<const char*>(u.data()), u.size()};
}

std::string read(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

int usage() {
    std::println(std::cerr, "gongfu-studio bake <dir> | check <dir> | embed <dir> --module <m> --namespace <ns> --out <file> | cut <sheet.png> --out <dir> | "
                            "sheet <dir> <name> --out <png> | "
                            "versus <dir> [--rounds <n>] [--seed <s>] [--system <file>]");
    return 2;
}

int cut(const std::filesystem::path& file, int argc, char** argv) {
    std::filesystem::path out = ".";
    omui::imageio::Key key;
    std::int32_t gap = 8;
    std::int64_t min_area = 400;
    std::uint8_t solid = 96;  // a piece's pixels: alpha above this (a sheet's noise stays below)
    double scale = 1;         // the atlas's size: the pieces at this scale, packed
    for (int i = 3; i + 1 < argc; ++i) {
        const std::string_view a = argv[i];
        if (a == "--out") out = path_of(argv[++i]);
        else if (a == "--gap") gap = std::stoi(argv[++i]);
        else if (a == "--min") min_area = std::stoll(argv[++i]);
        else if (a == "--scale") scale = std::clamp(std::stod(argv[++i]), 0.05, 1.0);
        else if (a == "--alpha") solid = static_cast<std::uint8_t>(std::clamp(std::stoi(argv[++i]), 0, 254));
        else if (a == "--key") {
            std::array<int, 3> c{};
            if (std::sscanf(argv[++i], "%d,%d,%d", &c[0], &c[1], &c[2]) == 3)
                key.r = static_cast<std::uint8_t>(c[0]), key.g = static_cast<std::uint8_t>(c[1]), key.b = static_cast<std::uint8_t>(c[2]);
        }
    }
    nlohmann::json report = {{"command", "cut"}, {"sheet", utf8(file)}};
    auto img = omui::imageio::load_png(file);
    if (!img) {
        report["error"] = img.error();
        std::println("{}", report.dump(2));
        return 1;
    }
    omui::imageio::key_out(*img, key);
    std::array<std::int64_t, 4> alphas{};  // clear, faint, half, solid: how well the background keyed
    for (std::size_t i = 3; i < img->rgba.size(); i += 4) ++alphas[img->rgba[i] == 0 ? 0 : img->rgba[i] <= solid ? 1 : img->rgba[i] < 255 ? 2 : 3];
    report["alpha"] = {{"clear", alphas[0]}, {"faint", alphas[1]}, {"partial", alphas[2]}, {"solid", alphas[3]}};
    std::vector<std::int32_t> labels;
    const auto found = omui::imageio::pieces(*img, solid, gap, min_area, &labels);
    std::error_code ec;
    std::filesystem::create_directories(out, ec);
    report["pieces"] = nlohmann::json::array();
    // Each piece alone (its own pixels: a neighbour reaching into its bounds is left out), at the size asked, then packed.
    std::vector<omui::imageio::Image> parts;
    for (std::size_t k = 0; k < found.size(); ++k) {
        const auto& p = found[k];
        auto part = omui::imageio::crop(*img, p.x, p.y, p.width, p.height);
        for (std::int32_t y = 0; y < p.height; ++y)
            for (std::int32_t x = 0; x < p.width; ++x)
                if (labels[static_cast<std::size_t>(p.y + y) * img->width + (p.x + x)] != static_cast<std::int32_t>(k))
                    part.rgba[(static_cast<std::size_t>(y) * p.width + x) * 4 + 3] = 0;
        const auto name = std::format("piece-{:02}.png", k);
        if (auto saved = omui::imageio::save_png(out / name, omui::imageio::view(part)); !saved) report["error"] = saved.error();
        if (scale < 1)
            part = omui::imageio::scaled_down(part, std::max(1, static_cast<std::int32_t>(std::lround(p.width * scale))),
                                              std::max(1, static_cast<std::int32_t>(std::lround(p.height * scale))));
        parts.push_back(std::move(part));
        report["pieces"].push_back({{"file", name}, {"region", {p.x, p.y, p.width, p.height}}, {"pixels", p.area}});
    }
    const auto packed = omui::imageio::pack(parts, static_cast<std::int32_t>(std::lround(2048 * scale)), 4);
    (void)omui::imageio::save_png(out / "atlas.png", omui::imageio::view(packed.atlas));
    report["atlas"] = {{"file", "atlas.png"}, {"size", {packed.atlas.width, packed.atlas.height}}};
    for (std::size_t k = 0; k < found.size(); ++k)
        report["pieces"][k]["atlas_region"] = {packed.at[k].x, packed.at[k].y, packed.at[k].width, packed.at[k].height};
    report["ok"] = !report.contains("error");
    std::println("{}", report.dump(2));
    return report["ok"] ? 0 : 1;
}

int sheet(const std::filesystem::path& dir, int argc, char** argv) {
    if (argc < 4) return usage();
    const std::string name = argv[3];
    std::filesystem::path out;
    std::size_t palette = 0;
    for (int i = 4; i + 1 < argc; ++i) {
        const std::string_view a = argv[i];
        if (a == "--out") out = path_of(argv[++i]);
        else if (a == "--palette") palette = static_cast<std::size_t>(std::stoul(argv[++i]));
    }
    if (out.empty()) return usage();
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
    auto s = gongfu::studio::sheet(*fighter, *look);
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

int embed(const std::filesystem::path& dir, int argc, char** argv) {
    gongfu::studio::EmbedOptions o;
    std::filesystem::path out;
    for (int i = 3; i + 1 < argc; ++i) {
        const std::string_view a = argv[i];
        if (a == "--module") o.module = argv[++i];
        else if (a == "--namespace") o.ns = argv[++i];
        else if (a == "--out") out = path_of(argv[++i]);
    }
    if (o.module.empty() || o.ns.empty() || out.empty()) return usage();
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
int versus(const std::filesystem::path& dir, int argc, char** argv) {
    std::int32_t rounds = 200;
    std::uint64_t seed = 1;
    std::filesystem::path system_file;
    for (int i = 3; i + 1 < argc; ++i) {
        const std::string_view a = argv[i];
        if (a == "--rounds") rounds = std::max(1, std::stoi(argv[++i]));
        else if (a == "--seed") seed = std::stoull(argv[++i]);
        else if (a == "--system") system_file = path_of(argv[++i]);
    }
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

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) return usage();
    const std::string_view command = argv[1];
    const std::filesystem::path dir = path_of(argv[2]);
    if (command == "embed") return embed(dir, argc, argv);
    if (command == "sheet") return sheet(dir, argc, argv);
    if (command == "cut") return cut(dir, argc, argv);
    if (command == "versus") return versus(dir, argc, argv);
    if (command != "bake" && command != "check") return usage();

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
