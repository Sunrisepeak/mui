module openmui.build.embed;

import std;
import mcpp;
import openmui.build.plan;

namespace openmui::build {

std::vector<edge> plan_embed(const embed_options& opt) {
    const std::string root = mcpp::manifest_dir();
    const std::string gen = generator();
    if (root.empty() || opt.module.empty() || opt.suffix.empty() || opt.tag.empty() || gen.empty()) {
        std::println(std::cerr, "openmui.build.embed: embed() runs from build.mcpp, with a module, a suffix and a tag, and omui-gen built for the build");
        return {};
    }
    const std::string ns = opt.ns.empty() ? namespace_of(opt.module) : opt.ns;
    const std::string out = utf8(std::filesystem::path(opt.out_dir) / "openmui");
    std::vector<edge> edges;
    for (const auto& f : opt.left_out ? std::vector<std::filesystem::path>{} : files_ending(std::filesystem::path(root) / opt.dir, opt.suffix)) {
        std::string stem = utf8(f.filename());
        stem = stem.substr(0, stem.size() - opt.suffix.size());
        for (char& c : stem)
            if (c == '-' || c == '.') c = '_';
        const std::string name = stem + "_" + opt.tag;
        edge e;
        e.id = std::format("openmui:{}:{}", opt.tag, stem);
        e.description = std::format("{} {} → {}.cppm ({})", opt.tag, utf8(f.filename()), name, opt.module);
        e.command = {gen, "embed", utf8(f), "--module", opt.module, "--namespace", ns, "--name", name, "--out", out};
        e.inputs = {utf8(f), gen};
        e.outputs = {out + "/" + name + ".cppm"};
        e.provides = {opt.module + ":" + name};
        e.imports = {"std"};
        edges.push_back(std::move(e));
    }
    // And, asked for, one table of them all, `<module>:<tag>_files`: (the file's name, its text).
    if (opt.index && (!edges.empty() || opt.left_out)) {
        edge index;
        index.id = std::format("openmui:{}:index", opt.tag);
        index.description = std::format("{} files → {}_files.cppm ({})", opt.tag, opt.tag, opt.module);
        index.command = {gen, "embed-index", "--module", opt.module, "--namespace", ns, "--name", opt.tag + "_files", "--out", out};
        index.inputs = {gen};
        index.imports = {"std"};
        for (const edge& e : edges) {
            const std::string part = e.provides.front().substr(opt.module.size() + 1);
            index.command.push_back(utf8(std::filesystem::path(e.inputs.front()).filename()) + "=" + part);
            index.inputs.push_back(e.inputs.front());
            index.imports.push_back(e.provides.front());
        }
        index.outputs = {out + "/" + opt.tag + "_files.cppm"};
        index.provides = {opt.module + ":" + opt.tag + "_files"};
        edges.push_back(std::move(index));
    }
    return edges;
}

bool embed(embed_options opt) {
    mcpp::rerun_if_changed_glob((opt.dir + "/**").c_str());
    auto edges = plan_embed(opt);
    if (edges.empty()) {
        if (opt.left_out) return true;  // nothing asked for
        std::println(std::cerr, "openmui.build.embed: no {} file in {}/", opt.suffix, opt.dir);
        return false;
    }
    return submit(edges);
}

bool themes(theme_options opt) {
    return embed({.module = std::move(opt.module), .ns = std::move(opt.ns), .dir = std::move(opt.themes), .suffix = ".theme.omui.toml",
                  .tag = "theme", .out_dir = std::move(opt.out_dir)});
}

bool behaviors(behavior_options opt) {
    return embed({.module = std::move(opt.module), .ns = std::move(opt.ns), .dir = std::move(opt.behaviors), .suffix = ".behavior.omui.toml",
                  .tag = "behavior", .out_dir = std::move(opt.out_dir)});
}

}  // namespace openmui::build
