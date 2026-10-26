// openmui.rules — contracts to code, inside a build (component-binding §2.3).
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
    const std::string gen = std::string(mcpp::dep_bin("openmui-gen", "omui-gen"));
    if (gen.empty()) {
        std::println(std::cerr, "openmui.rules: omui-gen was not built for this build (openmui-rules depends on "
                                "openmui-gen with tools = [\"omui-gen\"])");
        return {};
    }
    std::string registry = opt.registry;
    if (registry.empty()) {
        const std::string self = std::string(mcpp::dep_dir("openmui-rules"));
        registry = (std::filesystem::path(self) / ".." / ".." / "spec" / "registry").lexically_normal().string();
    }
    if (!std::filesystem::is_directory(registry)) {
        std::println(std::cerr, "openmui.rules: no registry at {} (set options::registry to openmui's spec/registry)", registry);
        return {};
    }
    const std::filesystem::path dir = std::filesystem::path(root) / opt.contracts;
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec))
        if (e.path().string().ends_with(".contract.omui.toml")) files.push_back(e.path());
    std::ranges::sort(files);
    const std::string out = (std::filesystem::path(opt.out_dir) / "openmui").string();
    std::vector<std::string> registry_files;
    for (const auto& e : std::filesystem::recursive_directory_iterator(registry, ec))
        if (e.is_regular_file()) registry_files.push_back(e.path().string());
    std::ranges::sort(registry_files);

    std::vector<edge> edges;
    for (const auto& f : files) {
        for (const std::string& name : contract_names(f)) {
            auto make = [&](std::string part, std::string file) {
                edge e;
                e.id = std::format("openmui:{}:{}", part, name);
                e.description = std::format("contract {} → {} ({})", name, file, opt.module);
                e.command = {gen, "cpp", f.string(), "--registry", registry, "--module", opt.module, "--namespace", opt.ns,
                             "--out", out, "--contract", name, "--part", part};
                e.inputs = {f.string(), gen};
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
        for (const auto& f : files) e.command.push_back(f.string());
        e.command.insert(e.command.end(), {"--registry", registry, "--module", opt.module, "--out", out + "/stubs"});
        e.inputs = {gen};
        for (const auto& f : files) e.inputs.push_back(f.string());
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

}  // namespace openmui::rules
