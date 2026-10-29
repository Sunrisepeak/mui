module openmui.build.toy;

import std;
import mcpp;
import openmui.build.plan;

namespace openmui::build {

bool dist_toy(toy_options opt) {
    mcpp::provides_pack_format("dist-toy");
    if (std::string_view(mcpp::pack_format()) != "dist-toy") return true;
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
    a.input(("${mcpp.target_file:" + opt.target + "}").c_str());
    a.input(gen.c_str());
    a.output((out + "/index.html").c_str());
    a.submit();
    return true;
}

}  // namespace openmui::build
