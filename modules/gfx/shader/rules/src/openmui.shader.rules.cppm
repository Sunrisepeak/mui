// openmui.shader.rules — shaders(): for each `<dir>/*.slang`, one action runs
// omui-shader, writing the partition `<module>:<stem>_shaders` (namespace
// `<ns>::<stem>`) into the build's output. The partition joins the
// package's compile set; the action re-runs when the shader, a file it
// imports (its depfile) or omui-shader changes. The formats are the target's
// (omui-shader --target-os/--host-os), unless options::formats names them.
export module openmui.shader.rules;

import std;
import mcpp;

// Module linkage, not exported: the exported inline functions name them.
namespace openmui::shader::rules {

std::string utf8(const std::filesystem::path& p) {
    const std::u8string u = p.generic_u8string();
    return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}

// The OS of a target triple, as MCPP_TARGET_OS spells it.
std::string os_of(std::string_view triple) {
    if (triple.contains("windows") || triple.contains("mingw")) return "windows";
    if (triple.contains("apple") || triple.contains("darwin") || triple.contains("macos")) return "macos";
    if (triple.contains("android")) return "android";
    if (triple.contains("emscripten")) return "emscripten";
    return "linux";
}

}  // namespace openmui::shader::rules

export namespace openmui::shader::rules {

struct options {
    std::string              module;             // the package's module: acme.render
    std::string              ns;                 // default: the module's, then ::shaders (acme::render::shaders)
    std::string              shaders = "shaders";  // the directory of *.slang
    std::string              formats;            // "spirv,msl" …; empty: the target's
    std::vector<std::string> includes;           // -I, relative to the package
    std::vector<std::string> defines;            // -D
    std::string              out_dir = std::string(mcpp::out_dir());
};

inline bool shaders(const options& opt) {
    const std::string root = mcpp::manifest_dir();
    if (root.empty() || opt.module.empty()) {
        std::println(std::cerr, "openmui.shader.rules: shaders() runs from build.mcpp, and options::module names the package's module");
        return false;
    }
    const std::string tool = std::string(mcpp::dep_bin("openmui.shader", "omui-shader"));
    if (tool.empty()) {
        std::println(std::cerr, "openmui.shader.rules: omui-shader was not built for this build (the rule depends on openmui.shader with "
                                "tools = [\"omui-shader\"])");
        return false;
    }
    std::string ns = opt.ns;
    if (ns.empty()) {
        for (char c : opt.module) ns += c == '.' ? std::string("::") : std::string(1, c);
        ns += "::shaders";
    }
    mcpp::rerun_if_changed_glob((opt.shaders + "/**").c_str());
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(std::filesystem::path(root) / opt.shaders, ec))
        if (e.path().extension() == ".slang") files.push_back(e.path());
    std::ranges::sort(files);
    if (files.empty()) {
        std::println(std::cerr, "openmui.shader.rules: no shader in {}/ (files named <name>.slang)", opt.shaders);
        return false;
    }
    const std::filesystem::path out = std::filesystem::path(opt.out_dir) / "openmui" / "shader";
    std::filesystem::create_directories(out, ec);
    const std::string target_os = *mcpp::target_os() ? std::string(mcpp::target_os()) : os_of(mcpp::target());
    const std::string host_os = os_of(mcpp::host());
    for (const auto& f : files) {
        const std::string stem = utf8(f.stem());
        const std::string partition = stem + "_shaders";
        const std::string cppm = utf8(out / (partition + ".cppm"));
        const std::string dep = cppm + ".d";
        const std::string id = std::format("openmui.shader:{}", stem);
        const std::string description = std::format("slang {} → {}:{}", utf8(f.filename()), opt.module, partition);
        mcpp::action a;
        a.id = id.c_str();
        a.role = mcpp::roles::source;  // C++ the package compiles; every compile edge waits for it
        a.description = description.c_str();
        std::vector<std::string> args{tool, utf8(f), "--out", cppm, "--module", opt.module, "--partition", partition,
                                      "--namespace", ns + "::" + stem, "--depfile", dep};
        if (opt.formats.empty()) args.insert(args.end(), {"--target-os", target_os, "--host-os", host_os});
        else args.insert(args.end(), {"--formats", opt.formats});
        for (const auto& i : opt.includes) args.insert(args.end(), {"-I", utf8(std::filesystem::path(root) / i)});
        for (const auto& d : opt.defines) args.insert(args.end(), {"-D", d});
        for (const auto& x : args) a.arg(x.c_str());
        a.input(utf8(f).c_str());
        a.input(tool.c_str());
        a.output(cppm.c_str());
        a.depfile = dep.c_str();
        const std::string provides = opt.module + ":" + partition;
        a.provides(provides.c_str());
        a.imports("std");
        a.imports("openmui.rhi");
        a.submit();
    }
    return true;
}

}  // namespace openmui::shader::rules
