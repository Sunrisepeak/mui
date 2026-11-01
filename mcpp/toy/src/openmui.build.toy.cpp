module openmui.build.toy;

import std;
import mcpp;
import openmui.build.plan;

namespace openmui::build {

bool dist_toy(toy_options opt) {
    mcpp::provides_pack_format("dist-toy");
    if (std::string_view(mcpp::pack_format()) != "dist-toy") return true;
    // Only the package being packed (the site packs; the fighting game it
    // depends on does not).
    if (!is_packed()) return true;
    const std::string gen = generator();
    if (opt.target.empty() || gen.empty()) {
        std::println(std::cerr, "openmui.build.toy: dist_toy() names the program target, and needs omui-gen built for the build");
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
    for (const later_files& l : opt.later) {
        a.arg("--later").arg(std::format("{}|{}|{}", l.dir, l.suffix, l.mount).c_str());
        std::error_code ec;
        for (const auto& e : std::filesystem::directory_iterator(l.dir, ec))
            if (const std::string f = utf8(e.path()); f.ends_with(l.suffix)) a.input(f.c_str());
    }
    for (const font_subset& s : opt.subsets) {
        std::string list;
        for (const std::string& t : s.text) list += (list.empty() ? "" : ",") + t;
        a.arg("--subset").arg(std::format("{}={}", s.font, list).c_str());
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
    a.input(("${mcpp.target_file:" + opt.target + "}").c_str());
    a.input(gen.c_str());
    a.output((out + "/index.html").c_str());
    a.submit();
    return true;
}

}  // namespace openmui::build
