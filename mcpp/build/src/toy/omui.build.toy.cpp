module omui.build.toy;

import std;
import mcpp;
import omui.build.plan;

using namespace mlibs::build;

namespace omui::build {

bool dist_toy(toy_options opt) {
    mcpp::provides_pack_format("dist-toy");
    if (std::string_view(mcpp::pack_format()) != "dist-toy") return true;
    // Only the package being packed (the site packs; the fighting game it
    // depends on does not).
    if (!is_packed()) return true;
    const std::string gen = generator();
    if (opt.target.empty() || gen.empty()) {
        std::println(std::cerr, "omui.build.toy: dist_toy() names the program target, and needs omui-gen built for the build");
        return false;
    }
    const std::string out = opt.out_dir + "/toy/" + opt.target;
    std::string fonts;
    for (const std::string& f : opt.fonts) fonts += (fonts.empty() ? "" : ",") + f;
    mcpp::action a;
    a.id = "dist-toy";
    a.role = mcpp::roles::artifact;
    a.description = "a Toy package";
    for (const std::string& arg : {gen, std::string("dist-toy"), std::string("${mcpp.stage_dir}"), std::string("--target"), opt.target,
                                   std::string("--out"), out, std::string("--title"), opt.title, std::string("--background"), opt.background,
                                   std::string("--graph"), std::string(mcpp::graph_file())})
        a.arg(arg.c_str());
    if (!fonts.empty()) a.arg("--fonts").arg(fonts.c_str());
    if (!opt.sdk) a.arg("--no-sdk");
    if (opt.world) a.arg("--world");
    if (!opt.background_dark.empty()) a.arg("--background-dark").arg(opt.background_dark.c_str());
    if (opt.load != "eager") a.arg("--load").arg(opt.load.c_str());
    for (const later_files& l : opt.later) {
        a.arg("--later").arg(std::format("{}|{}|{}", l.dir, l.suffix, l.mount).c_str());
        std::error_code ec;
        for (const auto& e : std::filesystem::directory_iterator(l.dir, ec))
            if (const std::string f = utf8(e.path()); f.ends_with(l.suffix)) a.input(f.c_str());
    }
    for (const auto& [font, langs] : opt.font_languages) {
        std::string list;
        for (const std::string& l : langs) list += (list.empty() ? "" : ",") + l;
        a.arg("--font-languages").arg(std::format("{}={}", font, list).c_str());
    }
    for (const auto& [font, text] : opt.font_initial)
        a.arg("--font-initial").arg(std::format("{}={}", font, text).c_str());
    for (const std::string& font : opt.fonts_on_demand) a.arg("--font-on-demand").arg(font.c_str());
    std::vector<std::pair<std::string_view, const font_subset*>> texts;
    for (const font_subset& s : opt.subsets) texts.emplace_back("--subset", &s);
    for (const font_subset& s : opt.font_initial_text) texts.emplace_back("--font-initial-text", &s);
    for (const auto& [flag, subset] : texts) {
        const font_subset& s = *subset;
        std::string list;
        for (const std::string& t : s.text) list += (list.empty() ? "" : ",") + t;
        a.arg(flag.data()).arg(std::format("{}={}", s.font, list).c_str());  // the flags are literals
        // Their text is the subset's input: a character added, the font is cut again.
        for (const std::string& t : s.text) {
            std::error_code ec;
            if (std::filesystem::is_directory(t, ec)) {
                for (const auto& e : std::filesystem::recursive_directory_iterator(t, ec))
                    if (e.is_regular_file()) a.input(utf8(e.path()).c_str());
            } else {
                a.input(t.c_str());
            }
        }
    }
    if (!opt.static_html.empty()) {
        std::filesystem::path projection = std::filesystem::path(opt.static_html);
        if (projection.is_relative()) projection = std::filesystem::path(mcpp::manifest_dir()) / projection;
        std::error_code ec;
        if (std::filesystem::is_regular_file(projection, ec)) {
            a.arg("--static-html").arg(utf8(projection).c_str());
            a.input(utf8(projection).c_str());
            for (const auto& [flag, value] : {std::pair{"--lang", &opt.lang}, {"--description", &opt.description}, {"--canonical", &opt.canonical},
                                              {"--image", &opt.image}})
                if (!value->empty()) a.arg(flag).arg(value->c_str());
            for (const auto& [lang, address] : opt.alternates) a.arg("--alternate").arg(std::format("{}={}", lang, address).c_str());
        } else {
            std::println(std::cerr, "omui.build.toy: no {} yet: the page is packed without its document (omui build --web makes it first)", utf8(projection));
        }
    }
    for (const std::string& named : opt.files) {
        const std::filesystem::path package(mcpp::manifest_dir());
        const std::filesystem::path path = package / std::filesystem::path(named);
        std::error_code ec;
        std::vector<std::filesystem::path> found;
        if (std::filesystem::is_directory(path, ec)) {
            for (const auto& e : std::filesystem::recursive_directory_iterator(path, ec))
                if (e.is_regular_file()) found.push_back(e.path());
        } else if (std::filesystem::is_regular_file(path, ec)) {
            found.push_back(path);
        } else {
            continue;  // an optional file (omui.config.ts) the application does not have
        }
        std::ranges::sort(found);
        for (const std::filesystem::path& file : found) {
            std::string inside = "/" + std::filesystem::relative(file, package, ec).generic_string();
            // A page reads data; it does not evaluate data modules (mlibs.ts.not-here): omui.config.ts and *.xdef.ts
            // go as their snapshots, evaluated as the page is made (omui.config.json, *.xdef.json).
            const bool data = inside.ends_with("/omui.config.ts") || inside.ends_with(".xdef.ts");
            if (data) inside.replace(inside.size() - 3, 3, ".json");
            a.arg(data ? "--data" : "--file").arg(std::format("{}={}", inside, utf8(file)).c_str());
            a.input(utf8(file).c_str());
        }
    }
    if (!opt.part.first.empty()) {
        // The whole program's symbol map and the first part's, each beside its program (--emit-symbol-map).
        a.arg("--part-program").arg(("${mcpp.target_file:" + opt.target + "}").c_str());
        a.arg("--part-first").arg(("${mcpp.target_file:" + opt.part.first + "}").c_str());
        if (!opt.part.needs.empty()) a.arg("--part-needs").arg(opt.part.needs.c_str());
        a.input(("${mcpp.target_file:" + opt.part.first + "}").c_str());
    }
    a.input(("${mcpp.target_file:" + opt.target + "}").c_str());
    a.input(gen.c_str());
    a.output((out + "/index.html").c_str());
    a.submit();
    return true;
}

}  // namespace omui::build
