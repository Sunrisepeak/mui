module omui.build.embed;

import std;
import mcpp;
import omui.build.plan;
import mlibs.build.embed;

namespace omui::build {

// Plain files are the foundation's (mlibs.build.embed, `xdef embed`); a data module (`*.xdef.ts`) is carried as the
// JSON it evaluates to with omui's kinds, so its edge runs omui-gen's embed, which takes the same words.
std::vector<mlibs::build::edge> plan_embed(const embed_options& opt) {
    const bool data_modules = opt.suffix.ends_with(".xdef.ts");
    const std::string gen = generator();
    if (data_modules && gen.empty()) {
        std::println(std::cerr, "omui.build.embed: {} files are data modules, evaluated by omui-gen: build it for the build (omui.gen with tools = [\"omui-gen\"])", opt.suffix);
        return {};
    }
    auto edges = mlibs::build::plan_embed({.module = opt.module, .ns = opt.ns, .dir = opt.dir, .suffix = opt.suffix, .tag = opt.tag, .index = opt.index,
                                           .left_out = opt.left_out, .out_dir = opt.out_dir, .subdir = "openmui"});
    if (data_modules)
        for (auto& one : edges)
            if (one.command.size() > 1 && one.command[1] == "embed") {
                one.command[0] = gen;
                one.inputs.back() = gen;
            }
    return edges;
}

bool embed(embed_options opt) {
    mcpp::rerun_if_changed_glob((opt.dir + "/**").c_str());
    auto edges = plan_embed(opt);
    if (edges.empty()) {
        if (opt.left_out) return true;  // nothing asked for
        std::println(std::cerr, "omui.build.embed: no {} file in {}/", opt.suffix, opt.dir);
        return false;
    }
    return mlibs::build::submit(edges);
}

bool themes(theme_options opt) {
    return embed({.module = std::move(opt.module), .ns = std::move(opt.ns), .dir = std::move(opt.themes), .suffix = ".theme.xdef.ts",
                  .tag = "theme", .out_dir = std::move(opt.out_dir)});
}

bool behaviors(behavior_options opt) {
    return embed({.module = std::move(opt.module), .ns = std::move(opt.ns), .dir = std::move(opt.behaviors), .suffix = ".behavior.xdef.ts",
                  .tag = "behavior", .out_dir = std::move(opt.out_dir)});
}

}  // namespace omui::build
