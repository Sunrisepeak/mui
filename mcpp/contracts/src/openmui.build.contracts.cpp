module openmui.build.contracts;

import std;
import mcpp;
import openmui.build.plan;

namespace openmui::build {

namespace {

// spec/registry in the openmui tree this plugin is part of, found by the
// plugin's qualified name (a build may hold other packages called `contracts`).
std::string default_registry() {
    const char* self = mcpp::dep_dir("openmui.build.contracts");
    const std::string dir = self && *self ? std::string(self) : std::string(mcpp::dep_dir("contracts"));
    return utf8((std::filesystem::path(dir) / ".." / ".." / "spec" / "registry").lexically_normal());
}

}  // namespace

std::vector<std::string> contract_names(const std::filesystem::path& file) { return table_names(file, "contract"); }

std::vector<std::string> native_names(const std::filesystem::path& file) { return table_names(file, "native"); }

std::vector<edge> plan(const component_options& opt) {
    const std::string root = mcpp::manifest_dir();
    if (root.empty()) {
        std::println(std::cerr, "openmui.build.contracts: no mcpp build context — this runs from build.mcpp");
        return {};
    }
    if (opt.module.empty()) {
        std::println(std::cerr, "openmui.build.contracts: component_options::module names the package's module (acme.ui)");
        return {};
    }
    const std::string gen = generator();
    if (gen.empty()) {
        std::println(std::cerr, "openmui.build.contracts: omui-gen was not built for this build (openmui.build.contracts depends on "
                                "openmui.gen with tools = [\"omui-gen\"])");
        return {};
    }
    const std::string registry = opt.registry.empty() ? default_registry() : opt.registry;
    if (!std::filesystem::is_directory(registry)) {
        std::println(std::cerr, "openmui.build.contracts: no registry at {} (set component_options::registry to openmui's spec/registry)", registry);
        return {};
    }
    const auto files = files_ending(std::filesystem::path(root) / opt.contracts, ".contract.omui.toml");
    const std::string out = utf8(std::filesystem::path(opt.out_dir) / "openmui");
    const auto registry_files = files_under(registry);

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

bool components(component_options opt) {
    mcpp::rerun_if_changed_glob((opt.contracts + "/**").c_str());
    auto edges = plan(opt);
    if (edges.empty()) {
        std::println(std::cerr, "openmui.build.contracts: no contract in {}/ (files named <name>.contract.omui.toml)", opt.contracts);
        return false;
    }
    return submit(edges);
}

std::vector<edge> plan_natives(const native_options& opt) {
    const std::string root = mcpp::manifest_dir();
    if (root.empty() || opt.module.empty()) {
        std::println(std::cerr, "openmui.build.contracts: natives() runs from build.mcpp, and native_options::module names the package's module");
        return {};
    }
    const std::string gen = generator();
    if (gen.empty()) {
        std::println(std::cerr, "openmui.build.contracts: omui-gen was not built for this build");
        return {};
    }
    const std::string registry = opt.registry.empty() ? default_registry() : opt.registry;
    const std::string ns = opt.ns.empty() ? namespace_of(opt.module) : opt.ns;
    const auto files = files_ending(std::filesystem::path(root) / opt.natives, ".native.omui.toml");
    const std::string out = utf8(std::filesystem::path(opt.out_dir) / "openmui");
    const auto registry_files = files_under(registry);

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

bool natives(native_options opt) {
    mcpp::rerun_if_changed_glob((opt.natives + "/**").c_str());
    auto edges = plan_natives(opt);
    if (edges.empty()) {
        std::println(std::cerr, "openmui.build.contracts: no native module in {}/ (files named <name>.native.omui.toml)", opt.natives);
        return false;
    }
    return submit(edges);
}

}  // namespace openmui::build
