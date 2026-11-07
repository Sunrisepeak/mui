// gongfu.rules — a program's fighters, inside its build (G-5).
//
// One action runs `gongfu-studio embed <dir>`: every
// `<name>.fighter.gongfu.toml` of the directory, with its look and baked
// hurtboxes, becomes the partition `<module>:gongfu_fighters` the package
// compiles. It re-runs when any file of the directory changes, and fails —
// the build with it — when a boxes file is not what its fighter and look make
// now, naming the command that bakes it again.
export module gongfu.rules;

import std;
import mcpp;

export namespace gongfu::rules {

struct options {
    std::string module;                                  // the package's module (acme.game)
    std::string ns;                                      // the namespace of the texts (acme::files)
    std::string dir = "fighters";                        // where the fighters' files are
    std::string out_dir = std::string(mcpp::out_dir());  // the partition goes under out_dir/gongfu
};

inline bool fighters(const options& o) {
    mcpp::rerun_if_changed_glob((o.dir + "/**").c_str());
    const std::string root = mcpp::manifest_dir();
    const std::string tool = std::string(mcpp::dep_bin("gongfu.studio", "gongfu-studio"));
    if (root.empty() || o.module.empty() || o.ns.empty() || tool.empty()) {
        std::println(std::cerr, "gongfu.rules: fighters() runs from build.mcpp, with a module and a namespace, and gongfu-studio built for the build");
        return false;
    }
    const auto u8 = [](const std::filesystem::path& p) {
        const std::u8string s = p.u8string();
        return std::string(reinterpret_cast<const char*>(s.data()), s.size());
    };
    const std::filesystem::path dir = std::filesystem::path(root) / o.dir;
    const std::string out = u8(std::filesystem::path(o.out_dir) / "gongfu" / "gongfu_fighters.cppm");
    mcpp::action a;
    a.id = "gongfu:fighters";
    a.role = mcpp::roles::source;  // C++ the package compiles: every compile waits for it
    const std::string description = std::format("gongfu fighters {}/ → gongfu_fighters.cppm ({})", o.dir, o.module);
    a.description = description.c_str();
    // Kept until the action is submitted: it holds their text, not copies.
    const std::vector<std::string> args{tool, "embed", u8(dir), "--module", o.module, "--namespace", o.ns, "--out", out};
    for (const std::string& arg : args) a.arg(arg.c_str());
    std::vector<std::string> inputs{tool};
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec))
        if (u8(e.path()).ends_with(".gongfu.toml")) inputs.push_back(u8(e.path()));
    std::ranges::sort(inputs);
    for (const auto& i : inputs) a.input(i.c_str());
    a.output(out.c_str());
    const std::string provides = o.module + ":gongfu_fighters";
    a.provides(provides.c_str());
    a.imports("std");
    a.submit();
    return true;
}

}  // namespace gongfu::rules
