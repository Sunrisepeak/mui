// openmui.rules — contracts to code, inside a build (component-binding §2.3,
// §8, §11): components() for *.contract.omui.toml, natives() for
// *.native.omui.toml, themes() for *.theme.omui.toml, behaviors() for
// *.behavior.omui.toml.
//
// For each contract in `contract/*.contract.omui.toml`, two actions run
// `omui-gen cpp`: one makes the partition `<module>:<name>_contract`
// (declared as the module interface it provides, with its imports, since a
// file that does not exist yet cannot be scanned), one the builder, an
// implementation unit of the package's module. Both join the package's
// compile set. One more runs `omui-gen stubs` for the TS / Python / Lua stubs
// and the view-document schema. Every edge re-runs when a contract, the
// registry or the generator changes: they are its inputs.
export module openmui.rules;

import std;
import mcpp;

// Module linkage (not exported, not an anonymous namespace): the exported
// functions below are inline and may not name a TU-local entity.
namespace openmui::rules {

// UTF-8 text of a path (generic separators for `generic_utf8`), never
// through the process's code page: std::filesystem::path::string() throws on
// Windows for a name the ANSI code page cannot spell (lint rule `paths`).
std::string utf8(const std::filesystem::path& p) {
    const std::u8string u = p.u8string();
    return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}
std::string generic_utf8(const std::filesystem::path& p) {
    const std::u8string u = p.generic_u8string();
    return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}

// This rule's own directory, by its qualified name: a build with another
// package called `rules` (gongfu.rules) shares the bare name.
std::string self_dir() {
    const char* qualified = mcpp::dep_dir("openmui.rules");
    return qualified && *qualified ? std::string(qualified) : std::string(mcpp::dep_dir("rules"));
}

}  // namespace openmui::rules

export namespace openmui::rules {

struct options {
    std::string module;                                   // the package's module (acme.ui)
    std::string ns = "omui::ui";                          // the components' namespace
    std::string contracts = "contract";                   // the directory of *.contract.omui.toml
    std::string registry;                                 // spec/registry (default: beside this rule in the openmui tree)
    std::string out_dir = std::string(mcpp::out_dir());   // generated files go under out_dir/openmui
    bool        stubs = true;                             // TS / Python / Lua stubs and the schema
};

// One planned edge, so a build program can adjust it before submitting.
struct edge {
    std::string              id;
    std::string              description;
    std::vector<std::string> command;
    std::vector<std::string> inputs;
    std::vector<std::string> outputs;
    std::vector<std::string> provides;  // a generated module interface's module
    std::vector<std::string> imports;   // what a generated unit imports (it cannot be scanned before it exists)
};

// The contract names a file declares (its `[contract.<name>]` tables).
inline std::vector<std::string> contract_names(const std::filesystem::path& file) {
    std::vector<std::string> out;
    std::ifstream in(file);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.starts_with("[contract.")) continue;
        const auto end = line.find(']');
        if (end == std::string::npos) continue;
        const std::string name = line.substr(10, end - 10);
        if (name.find('.') == std::string::npos) out.push_back(name);  // [contract.x.props] is a sub-table
    }
    return out;
}

inline std::vector<edge> plan(const options& opt) {
    const std::string root = mcpp::manifest_dir();
    if (root.empty()) {
        std::println(std::cerr, "openmui.rules: no mcpp build context — this runs from build.mcpp");
        return {};
    }
    if (opt.module.empty()) {
        std::println(std::cerr, "openmui.rules: options::module names the package's module (acme.ui)");
        return {};
    }
    const std::string gen = std::string(mcpp::dep_bin("openmui.gen", "omui-gen"));
    if (gen.empty()) {
        std::println(std::cerr, "openmui.rules: omui-gen was not built for this build (openmui.rules depends on "
                                "openmui.gen with tools = [\"omui-gen\"])");
        return {};
    }
    std::string registry = opt.registry;
    if (registry.empty()) {
        const std::string self = self_dir();
        registry = utf8((std::filesystem::path(self) / ".." / ".." / "spec" / "registry").lexically_normal());
    }
    if (!std::filesystem::is_directory(registry)) {
        std::println(std::cerr, "openmui.rules: no registry at {} (set options::registry to openmui's spec/registry)", registry);
        return {};
    }
    const std::filesystem::path dir = std::filesystem::path(root) / opt.contracts;
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec))
        if (utf8(e.path()).ends_with(".contract.omui.toml")) files.push_back(e.path());
    std::ranges::sort(files);
    const std::string out = utf8(std::filesystem::path(opt.out_dir) / "openmui");
    std::vector<std::string> registry_files;
    for (const auto& e : std::filesystem::recursive_directory_iterator(registry, ec))
        if (e.is_regular_file()) registry_files.push_back(utf8(e.path()));
    std::ranges::sort(registry_files);

    std::vector<edge> edges;
    for (const auto& f : files) {
        for (const std::string& name : contract_names(f)) {
            auto make = [&](std::string part, std::string file) {
                edge e;
                e.id = std::format("openmui:{}:{}", part, name);
                e.description = std::format("contract {} → {} ({})", name, file, opt.module);
                e.command = {gen, "cpp", utf8(f), "--registry", registry, "--module", opt.module, "--namespace", opt.ns,
                             "--out", out, "--contract", name, "--part", part};
                e.inputs = {utf8(f), gen};
                e.inputs.insert(e.inputs.end(), registry_files.begin(), registry_files.end());
                e.outputs = {out + "/" + file};
                e.imports = {"std", "openmui.core", "openmui.view"};
                return e;
            };
            edge contract = make("contract", name + "_contract.cppm");
            contract.provides = {opt.module + ":" + name + "_contract"};
            edges.push_back(std::move(contract));
            edge builder = make("builder", name + "_builder.cpp");
            builder.imports.push_back(opt.module);  // an implementation unit of the package's module
            edges.push_back(std::move(builder));
        }
    }
    if (opt.stubs && !files.empty()) {
        edge e;
        e.id = "openmui:stubs";
        e.description = std::format("contracts → TS / Python / Lua stubs and the view-document schema ({})", opt.module);
        e.command = {gen, "stubs"};
        for (const auto& f : files) e.command.push_back(utf8(f));
        e.command.insert(e.command.end(), {"--registry", registry, "--module", opt.module, "--out", out + "/stubs"});
        e.inputs = {gen};
        for (const auto& f : files) e.inputs.push_back(utf8(f));
        e.inputs.insert(e.inputs.end(), registry_files.begin(), registry_files.end());
        for (const char* ext : {".d.ts", ".pyi", ".lua", ".components.json"}) e.outputs.push_back(out + "/stubs/" + opt.module + ext);
        edges.push_back(std::move(e));
    }
    return edges;
}

inline bool submit(std::span<const edge> edges) {
    for (const edge& e : edges) {
        mcpp::action a;
        a.id = e.id.c_str();
        a.role = mcpp::roles::source;  // C++ the package compiles; every compile edge waits for it
        a.description = e.description.c_str();
        for (const auto& c : e.command) a.arg(c.c_str());
        for (const auto& i : e.inputs) a.input(i.c_str());
        for (const auto& o : e.outputs) a.output(o.c_str());
        for (const auto& p : e.provides) a.provides(p.c_str());
        for (const auto& i : e.imports) a.imports(i.c_str());
        a.submit();
    }
    return true;
}

// Plans and submits; false (and a message) when the build cannot go on.
inline bool components(options opt) {
    mcpp::rerun_if_changed_glob((opt.contracts + "/**").c_str());
    auto edges = plan(opt);
    if (edges.empty()) {
        std::println(std::cerr, "openmui.rules: no contract in {}/ (files named <name>.contract.omui.toml)", opt.contracts);
        return false;
    }
    return submit(edges);
}

// ── native modules (component-binding §8) ───────────────────────────────────

struct native_options {
    std::string module;                                   // the package's module (acme.finder)
    std::string ns;                                       // its C++ namespace (default: the module's, acme::finder)
    std::string natives = "native";                       // the directory of *.native.omui.toml
    std::string registry;                                 // spec/registry (default: beside this rule in the openmui tree)
    std::string out_dir = std::string(mcpp::out_dir());   // generated files go under out_dir/openmui
    bool        stubs = true;                             // TS / Python / Lua stubs
};

// The native modules a file declares (its `[native.<name>]` tables).
inline std::vector<std::string> native_names(const std::filesystem::path& file) {
    std::vector<std::string> out;
    std::ifstream in(file);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.starts_with("[native.")) continue;
        const auto end = line.find(']');
        if (end == std::string::npos) continue;
        const std::string name = line.substr(8, end - 8);
        if (name.find('.') == std::string::npos) out.push_back(name);  // [native.x.types…] is a sub-table
    }
    return out;
}

inline std::vector<edge> plan_natives(const native_options& opt) {
    const std::string root = mcpp::manifest_dir();
    if (root.empty() || opt.module.empty()) {
        std::println(std::cerr, "openmui.rules: natives() runs from build.mcpp, and native_options::module names the package's module");
        return {};
    }
    const std::string gen = std::string(mcpp::dep_bin("openmui.gen", "omui-gen"));
    if (gen.empty()) {
        std::println(std::cerr, "openmui.rules: omui-gen was not built for this build");
        return {};
    }
    std::string registry = opt.registry;
    if (registry.empty()) {
        const std::string self = self_dir();
        registry = utf8((std::filesystem::path(self) / ".." / ".." / "spec" / "registry").lexically_normal());
    }
    std::string ns = opt.ns;
    if (ns.empty())
        for (char c : opt.module) ns += c == '.' ? std::string("::") : std::string(1, c);
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(std::filesystem::path(root) / opt.natives, ec))
        if (utf8(e.path()).ends_with(".native.omui.toml")) files.push_back(e.path());
    std::ranges::sort(files);
    const std::string out = utf8(std::filesystem::path(opt.out_dir) / "openmui");
    std::vector<std::string> registry_files;
    for (const auto& e : std::filesystem::recursive_directory_iterator(registry, ec))
        if (e.is_regular_file()) registry_files.push_back(utf8(e.path()));
    std::ranges::sort(registry_files);

    std::vector<edge> edges;
    std::vector<std::string> names;
    for (const auto& f : files)
        for (const std::string& name : native_names(f)) {
            names.push_back(name);
            edge e;
            e.id = std::format("openmui:native:{}", name);
            e.description = std::format("native module {} → {}_native.cppm ({})", name, name, opt.module);
            e.command = {gen, "native", utf8(f), "--registry", registry, "--module", opt.module, "--namespace", ns, "--out", out,
                         "--native", name, "--part", "cpp"};
            e.inputs = {utf8(f), gen};
            e.inputs.insert(e.inputs.end(), registry_files.begin(), registry_files.end());
            e.outputs = {out + "/" + name + "_native.cppm"};
            e.provides = {opt.module + ":" + name + "_native"};
            e.imports = {"std", "nlohmann.json", "openmui.native"};
            edges.push_back(std::move(e));
        }
    if (opt.stubs && !files.empty()) {
        edge e;
        e.id = "openmui:native-stubs";
        e.description = std::format("native modules → TS / Python / Lua stubs ({})", opt.module);
        e.command = {gen, "native"};
        for (const auto& f : files) e.command.push_back(utf8(f));
        e.command.insert(e.command.end(), {"--registry", registry, "--module", opt.module, "--out", out + "/stubs", "--part", "stubs"});
        e.inputs = {gen};
        for (const auto& f : files) e.inputs.push_back(utf8(f));
        e.inputs.insert(e.inputs.end(), registry_files.begin(), registry_files.end());
        e.outputs = {out + "/stubs/" + opt.module + ".natives.d.ts", out + "/stubs/" + opt.module + ".natives.lua"};
        for (const auto& n : names) e.outputs.push_back(out + "/stubs/" + n + ".pyi");
        edges.push_back(std::move(e));
    }
    return edges;
}

// Plans and submits the native modules' edges; false (and a message) when
// the build cannot go on.
inline bool natives(native_options opt) {
    mcpp::rerun_if_changed_glob((opt.natives + "/**").c_str());
    auto edges = plan_natives(opt);
    if (edges.empty()) {
        std::println(std::cerr, "openmui.rules: no native module in {}/ (files named <name>.native.omui.toml)", opt.natives);
        return false;
    }
    return submit(edges);
}

// ── theme packages (openmui.theme) ──────────────────────────────────────────

struct theme_options {
    std::string module;                                   // the package's module (acme.themes)
    std::string ns;                                       // the namespace of the embedded texts (default: the module's)
    std::string themes = "themes";                        // the directory of *.theme.omui.toml
    std::string out_dir = std::string(mcpp::out_dir());   // generated files go under out_dir/openmui
};

// Each theme file as a partition `<module>:<stem>_theme` holding its text
// (`omui-gen embed`): the package adds it to the program's library with
// omui::theme::library().add(<stem>_theme, "<stem>.theme.omui.toml").
inline std::vector<edge> plan_themes(const theme_options& opt) {
    const std::string root = mcpp::manifest_dir();
    const std::string gen = std::string(mcpp::dep_bin("openmui.gen", "omui-gen"));
    if (root.empty() || opt.module.empty() || gen.empty()) {
        std::println(std::cerr, "openmui.rules: themes() runs from build.mcpp, theme_options::module names the package's module, "
                                "and omui-gen is built for the build");
        return {};
    }
    std::string ns = opt.ns;
    if (ns.empty())
        for (char c : opt.module) ns += c == '.' ? std::string("::") : std::string(1, c);
    const std::string out = utf8(std::filesystem::path(opt.out_dir) / "openmui");
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(std::filesystem::path(root) / opt.themes, ec))
        if (utf8(e.path()).ends_with(".theme.omui.toml")) files.push_back(e.path());
    std::ranges::sort(files);
    std::vector<edge> edges;
    for (const auto& f : files) {
        std::string stem = utf8(f.filename());
        stem = stem.substr(0, stem.find('.'));
        edge e;
        e.id = std::format("openmui:theme:{}", stem);
        e.description = std::format("theme {} → {}_theme.cppm ({})", utf8(f.filename()), stem, opt.module);
        e.command = {gen, "embed", utf8(f), "--module", opt.module, "--namespace", ns, "--name", stem + "_theme", "--out", out};
        e.inputs = {utf8(f), gen};
        e.outputs = {out + "/" + stem + "_theme.cppm"};
        e.provides = {opt.module + ":" + stem + "_theme"};
        e.imports = {"std"};
        edges.push_back(std::move(e));
    }
    return edges;
}

inline bool themes(theme_options opt) {
    mcpp::rerun_if_changed_glob((opt.themes + "/**").c_str());
    auto edges = plan_themes(opt);
    if (edges.empty()) {
        std::println(std::cerr, "openmui.rules: no theme in {}/ (files named <name>.theme.omui.toml)", opt.themes);
        return false;
    }
    return submit(edges);
}


// ── declarative behaviours (canvas-kit) ─────────────────────────────────────

struct behavior_options {
    std::string module;                                   // the package's module (acme.wall)
    std::string ns;                                       // the namespace of the embedded texts (default: the module's)
    std::string behaviors = "behaviors";                  // the directory of *.behavior.omui.toml
    std::string out_dir = std::string(mcpp::out_dir());   // generated files go under out_dir/openmui
};

// Each behaviour file as a partition `<module>:<stem>_behavior` holding its
// text (`-` in the stem becomes `_`): the program loads it with
// omui::world::load_behaviors(<stem>_behavior, "<stem>.behavior.omui.toml"),
// so a shipped program carries its behaviours and reads no file to have them.
inline std::vector<edge> plan_behaviors(const behavior_options& opt) {
    const std::string root = mcpp::manifest_dir();
    const std::string gen = std::string(mcpp::dep_bin("openmui.gen", "omui-gen"));
    if (root.empty() || opt.module.empty() || gen.empty()) {
        std::println(std::cerr, "openmui.rules: behaviors() runs from build.mcpp, behavior_options::module names the package's module, "
                                "and omui-gen is built for the build");
        return {};
    }
    std::string ns = opt.ns;
    if (ns.empty())
        for (char c : opt.module) ns += c == '.' ? std::string("::") : std::string(1, c);
    const std::string out = utf8(std::filesystem::path(opt.out_dir) / "openmui");
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(std::filesystem::path(root) / opt.behaviors, ec))
        if (utf8(e.path()).ends_with(".behavior.omui.toml")) files.push_back(e.path());
    std::ranges::sort(files);
    std::vector<edge> edges;
    for (const auto& f : files) {
        std::string stem = utf8(f.filename());
        stem = stem.substr(0, stem.find('.'));
        for (char& c : stem)
            if (c == '-') c = '_';
        edge e;
        e.id = std::format("openmui:behavior:{}", stem);
        e.description = std::format("behaviour {} → {}_behavior.cppm ({})", utf8(f.filename()), stem, opt.module);
        e.command = {gen, "embed", utf8(f), "--module", opt.module, "--namespace", ns, "--name", stem + "_behavior", "--out", out};
        e.inputs = {utf8(f), gen};
        e.outputs = {out + "/" + stem + "_behavior.cppm"};
        e.provides = {opt.module + ":" + stem + "_behavior"};
        e.imports = {"std"};
        edges.push_back(std::move(e));
    }
    return edges;
}

inline bool behaviors(behavior_options opt) {
    mcpp::rerun_if_changed_glob((opt.behaviors + "/**").c_str());
    auto edges = plan_behaviors(opt);
    if (edges.empty()) {
        std::println(std::cerr, "openmui.rules: no behaviour in {}/ (files named <name>.behavior.omui.toml)", opt.behaviors);
        return false;
    }
    return submit(edges);
}


// ── embedded data files (D-73's embed plugin) ───────────────────────────────

struct embed_options {
    std::string module;                                   // the package's module
    std::string ns;                                       // the namespace of the texts (default: the module's)
    std::string dir;                                      // where the files are
    std::string suffix;                                   // which: ".style.omui.toml"
    std::string tag;                                      // the partitions' and constants' suffix: <stem>_<tag>
    std::string out_dir = std::string(mcpp::out_dir());
};

// Each file `<stem><suffix>` in `dir` as a partition `<module>:<stem>_<tag>`
// holding its text (`-` in the stem becomes `_`): a program carries its data
// files and reads none to have them.
inline std::vector<edge> plan_embed(const embed_options& opt) {
    const std::string root = mcpp::manifest_dir();
    const std::string gen = std::string(mcpp::dep_bin("openmui.gen", "omui-gen"));
    if (root.empty() || opt.module.empty() || opt.suffix.empty() || opt.tag.empty() || gen.empty()) {
        std::println(std::cerr, "openmui.rules: embed() runs from build.mcpp, with a module, a suffix and a tag, and omui-gen built for the build");
        return {};
    }
    std::string ns = opt.ns;
    if (ns.empty())
        for (char c : opt.module) ns += c == '.' ? std::string("::") : std::string(1, c);
    const std::string out = utf8(std::filesystem::path(opt.out_dir) / "openmui");
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(std::filesystem::path(root) / opt.dir, ec))
        if (utf8(e.path()).ends_with(opt.suffix)) files.push_back(e.path());
    std::ranges::sort(files);
    std::vector<edge> edges;
    for (const auto& f : files) {
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
    return edges;
}

inline bool embed(embed_options opt) {
    mcpp::rerun_if_changed_glob((opt.dir + "/**").c_str());
    auto edges = plan_embed(opt);
    if (edges.empty()) {
        std::println(std::cerr, "openmui.rules: no {} file in {}/", opt.suffix, opt.dir);
        return false;
    }
    return submit(edges);
}

// ── dist-toy: a program packaged for Bilibili Toy (D-84 §6.5) ───────────────
//
// The pack format `dist-toy` (mcpp's pack_format mechanism): a program built
// for wasm32-emscripten becomes a Toy package — a small index.html, the
// program and the fonts under content-hashed names, relative paths, hash
// routes as the program's flags (omui-gen dist-toy says the rest).
//
//     // build.mcpp
//     openmui::rules::dist_toy({.target = "omui-fight", .title = "格斗"});
//
//     mcpp pack -p <package> --target wasm32-emscripten --release --pkg-format dist-toy
//
// The package lands in <out_dir>/toy/<target>/, what is in it beside it in
// <target>.toy.omui.json. Checking it with Toy's toy_doctor.py is
// tools/ci/toy.sh's; publishing is the maintainer's (`toy create`).

struct toy_options {
    std::string              target;                 // the program target
    std::string              title;                  // the page's <title>
    std::vector<std::string> fonts;                  // openmui.text's default fonts by stem (Inter, NotoSansSC); empty: all
    std::string              background = "#000000"; // behind the canvas while it loads
    bool                     sdk = true;             // the page loads Toy's SDK (window.toy)
    std::string              out_dir = std::string(mcpp::out_dir());
};

// Declares the format (always: so `--pkg-format` can name what is
// available) and, when the pack asks for it, the edge that makes the package.
inline bool dist_toy(toy_options opt) {
    mcpp::provides_pack_format("dist-toy");
    if (std::string_view(mcpp::pack_format()) != "dist-toy") return true;
    const std::string gen = std::string(mcpp::dep_bin("openmui.gen", "omui-gen"));
    if (opt.target.empty() || gen.empty()) {
        std::println(std::cerr, "openmui.rules: dist_toy() names the program target, and needs omui-gen built for the build");
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

}  // namespace openmui::rules
