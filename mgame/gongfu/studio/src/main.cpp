// gongfu-studio — making fighters from the command line (D-61: JSON out and
// an exit code, for people, agents and CI alike).
//
//     gongfu-studio bake  <dir>     writes each <name>.boxes.gongfu.toml in <dir>
//     gongfu-studio check <dir>     whether they are what the files make now (exit 1 if not)
//     gongfu-studio embed <dir> --module <m> --namespace <ns> --out <file>
//                                   the fighters as the module partition <m>:gongfu_fighters,
//                                   refused when a boxes file is stale (what gongfu.rules runs)
//     gongfu-studio sheet <dir> <name> --out <png> [--palette <n>]
//                                   every clip of <name>'s look with its boxes, as a contact sheet
import std;
import gongfu.studio;
import nlohmann.json;

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
    std::println(std::cerr, "gongfu-studio bake <dir> | check <dir> | embed <dir> --module <m> --namespace <ns> --out <file> | sheet <dir> <name> --out <png>");
    return 2;
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

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) return usage();
    const std::string_view command = argv[1];
    const std::filesystem::path dir = path_of(argv[2]);
    if (command == "embed") return embed(dir, argc, argv);
    if (command == "sheet") return sheet(dir, argc, argv);
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
