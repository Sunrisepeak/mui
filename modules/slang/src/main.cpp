// omui-slang — one `.slang` file to a C++ module partition of rhi::ShaderDesc
// constants, for a build step (openmui.slang.rules) or by hand:
//
//   omui-slang shaders/quad.slang --out gen/quad_shaders.cppm \
//       --module acme.render --partition quad_shaders --namespace acme::render::shaders::quad \
//       [--formats spirv,msl,dxbc | --target-os OS --host-os OS] [--slangc PATH]
//       [-I DIR]… [-D NAME[=VALUE]]… [--depfile FILE]
//
// Without --formats, the formats are formats_for(target OS, host OS): what a
// build for that target compiles to (SPIR-V; MSL on Apple; DXBC on Windows
// when the host is Windows). Neither given: SPIR-V.
// The depfile names every file the shader read, so editing one it imports
// rebuilds what uses it. Exit 1 with slangc's diagnostics, or the binding
// that breaks the convention, on stderr.
import std;
import mcpplibs.os;
import openmui.rhi;
import openmui.slang;

namespace os = mcpplibs::os;

namespace {

int usage(std::string_view why) {
    std::println(std::cerr, "omui-slang: {}", why);
    std::println(std::cerr, "usage: omui-slang FILE.slang --out FILE.cppm --module M --partition P --namespace NS "
                            "[--formats spirv,msl,dxbc,dxil | --target-os OS --host-os OS] [--slangc PATH] [-I DIR] [-D NAME] [--depfile FILE]");
    return 2;
}

bool write_if_changed(const std::filesystem::path& file, std::string_view text) {
    {
        std::ifstream in(file, std::ios::binary);
        if (in) {
            const std::string old((std::istreambuf_iterator<char>(in)), {});
            if (old == text) return true;
        }
    }
    std::error_code ec;
    if (file.has_parent_path()) std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    return out.good();
}

std::string escape_make(std::string_view path) {
    std::string out;
    for (char c : path) {
        if (c == ' ') out += '\\';
        out += c;
    }
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    std::filesystem::path source, out, depfile;
    omui::slang::Options options;
    omui::slang::EmitOptions emit;
    bool formats_given = false;
    std::string target_os, host_os;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        auto value = [&]() -> std::optional<std::string> {
            if (i + 1 >= args.size()) return std::nullopt;
            return args[++i];
        };
        if (a == "--out" || a == "--module" || a == "--partition" || a == "--namespace" || a == "--formats" || a == "--slangc" ||
            a == "--depfile" || a == "-I" || a == "-D" || a == "--target-os" || a == "--host-os") {
            auto v = value();
            if (!v) return usage(std::format("{} needs a value", a));
            if (a == "--out") out = os::fs::from_utf8(*v);
            else if (a == "--module") emit.module = *v;
            else if (a == "--partition") emit.partition = *v;
            else if (a == "--namespace") emit.ns = *v;
            else if (a == "--slangc") options.slangc = os::fs::from_utf8(*v);
            else if (a == "--depfile") depfile = os::fs::from_utf8(*v);
            else if (a == "-I") options.includes.push_back(os::fs::from_utf8(*v));
            else if (a == "-D") options.defines.push_back(*v);
            else if (a == "--target-os") target_os = *v;
            else if (a == "--host-os") host_os = *v;
            else {
                formats_given = true;
                options.formats = {};
                for (auto part : std::views::split(*v, ',')) {
                    const std::string_view f(part.begin(), part.end());
                    const auto format = omui::rhi::shader_format(f);
                    if (!format) return usage(std::format("`{}` is not a shader format (spirv, msl, metallib, dxbc, dxil)", f));
                    options.formats.add(*format);
                }
            }
        } else if (a.starts_with("-")) {
            return usage(std::format("unknown option {}", a));
        } else if (source.empty()) {
            source = os::fs::from_utf8(a);
        } else {
            return usage("one shader file at a time");
        }
    }
    if (source.empty() || out.empty() || emit.module.empty() || emit.partition.empty() || emit.ns.empty())
        return usage("FILE.slang, --out, --module, --partition and --namespace are needed");
    if (!formats_given)
        options.formats = target_os.empty() ? omui::rhi::formats({omui::rhi::ShaderFormat::spirv})
                                            : omui::slang::formats_for(target_os, host_os.empty() ? target_os : host_os);

    auto program = omui::slang::compile(source, options);
    if (!program) {
        std::println(std::cerr, "{}", program.error());
        return 1;
    }
    if (!write_if_changed(out, omui::slang::emit_cpp(*program, emit))) {
        std::println(std::cerr, "omui-slang: cannot write {}", os::fs::utf8(out));
        return 1;
    }
    if (!depfile.empty()) {
        std::string text = escape_make(os::fs::generic_utf8(out)) + ":";
        for (const auto& d : program->dependencies) text += " \\\n  " + escape_make(os::fs::generic_utf8(d));
        text += "\n";
        if (!write_if_changed(depfile, text)) {
            std::println(std::cerr, "omui-slang: cannot write {}", os::fs::utf8(depfile));
            return 1;
        }
    }
    return 0;
}
