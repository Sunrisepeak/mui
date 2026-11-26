// omui-devtools run: a program's output captured, its standard error with it when merged, in the order
// written; a status as a shell reports it; a variable set or left out; a missing program said so; the machine's share
// at least one.
import std;
import mcpplibs.mtest;
import mcpplibs.os;
import omui.devtools.check;
import omui.devtools.consumer;
import omui.devtools.run;

using namespace mcpplibs::mtest;
namespace run = omui::devtools::run;

static UTest cases{"devtools.run", [] {
    "output captured; standard error with it when merged, in order; the status as a shell reports it"_test = [] {
        if constexpr (mcpplibs::os::is_windows) return;
        const auto root = std::filesystem::current_path();
        const auto plain = run::program(root, "sh", {"-c", "echo out; echo err >&2; exit 3"});
        expect(plain.has_value() && eq(plain->status, 3) && eq(plain->out, std::string("out\n")));
        const auto merged = run::program(root, "sh", {"-c", "echo one; echo two >&2; echo three"}, {.merged = true});
        expect(merged.has_value() && eq(merged->out, std::string("one\ntwo\nthree\n"))) << (merged ? merged->out : merged.error());
        const auto only_error = run::program(root, "sh", {"-c", "echo alone >&2; exit 2"}, {.merged = true});
        expect(only_error.has_value() && eq(only_error->status, 2) && eq(only_error->out, std::string("alone\n")))
            << (only_error ? std::format("[{}] {:?}", only_error->status, only_error->out) : only_error.error());
    };

    "a variable set for the program; one it would inherit left out"_test = [] {
        if constexpr (mcpplibs::os::is_windows) return;
        const auto root = std::filesystem::current_path();
        const auto ran = run::program(root, "sh", {"-c", "echo \"[${OMUI_RUN_SET}][${HOME+home}]\""},
                                      {.variables = {{"OMUI_RUN_SET", "set"}}, .unset = {"HOME"}});
        expect(ran.has_value() && eq(ran->out, std::string("[set][]\n"))) << (ran ? ran->out : ran.error());
    };

    "a missing program is said so; the machine's share is at least one compile"_test = [] {
        expect(!run::program(std::filesystem::current_path(), "omui-no-such-program", {}).has_value());
        expect(run::share_of_machine() >= 1);
    };
}};

UTest budget_cases{"devtools.check", [] {
    "bytes held to a budget: each one over it said, an unmeasured metric said, a bad file said"_test = [] {
        namespace check = omui::devtools::check;
        const std::map<std::string, std::uint64_t> measured{{"wasm.shipped", 400}, {"wasm.code", 100}};
        const auto over = check::over_budget(measured, R"({"budgets":[{"metric":"wasm.shipped","max":394},{"metric":"wasm.code","max":261},{"metric":"js.bytes","max":1}]})");
        expect(eq(over.size(), std::size_t{2}));
        expect(over.size() == 2 && over[0] == "wasm.shipped is 400 B, over its budget of 394 B" && over[1] == "js.bytes is not measured") << over.front();
        expect(eq(check::over_budget(measured, "not json").size(), std::size_t{1}));
    };

    "a row says the profile it is measured at: each profile built once, its rows held to what it measured"_test = [] {
        namespace check = omui::devtools::check;
        // The terminal's tiers: compiled size-optimised (`small`); a row that names none is dist's (`size`).
        constexpr std::string_view budget =
            R"({"budgets":[{"metric":"program.shipped","max":1000,"profile":"small"},{"metric":"program.code","max":10},{"metric":"program.data","max":500,"profile":"small"}]})";
        expect(check::budget_profiles(budget, "size") == std::vector<std::string>{"small", "size"});
        expect(check::budget_profiles(R"({"budgets":[{"metric":"program.shipped","max":1}]})", "size") == std::vector<std::string>{"size"});
        expect(check::budget_profiles("not json", "size") == std::vector<std::string>{"size"});
        const std::map<std::string, std::uint64_t> small{{"program.shipped", 1200}, {"program.code", 700}, {"program.data", 500}};
        const auto over_small = check::over_budget(small, budget, "small", "size");
        expect(over_small.size() == 1 && over_small[0] == "program.shipped at the small profile is 1200 B, over its budget of 1000 B")
            << (over_small.empty() ? std::string() : over_small.front());
        const auto over_dist = check::over_budget(small, budget, "size", "size");  // only the row without a profile
        expect(over_dist.size() == 1 && over_dist[0] == "program.code at the size profile is 700 B, over its budget of 10 B");
        expect(check::over_budget({}, budget, "web", "size").empty());  // no row at it: nothing held
    };
}};

UTest render_cases{"devtools.check.render", [] {
    "a template's {{name}} replaced; other braces kept; a name without a value said"_test = [] {
        namespace check = omui::devtools::check;
        const std::map<std::string, std::string> values{{"project.name", "demo"}, {"self.version", "0.1.0"}};
        const auto done = check::render("[targets.{{project.name}}]\nv = \"{{self.version}}\"\nx = \"{{ Not A Name }}\" {{\n", values);
        expect(done.has_value() && eq(*done, std::string("[targets.demo]\nv = \"0.1.0\"\nx = \"{{ Not A Name }}\" {{\n"))) << done.value_or("");
        const auto missing = check::render("{{project.name}} {{template.package.name}}", values);
        expect(!missing.has_value() && missing.error() == "template.package.name");
    };
}};

UTest shader_cases{"devtools.check.shaders", [] {
    "a shader partition's WGSL and ESSL entries, with each ESSL entry's stage, in the page's script"_test = [] {
        namespace check = omui::devtools::check;
        const auto scratch = std::filesystem::temp_directory_path() / "omui-devtools-shaders-test";
        std::filesystem::create_directories(scratch);
        const auto bytes = [](std::string_view text) {
            std::string out;
            for (const char c : text) out += std::format("0x{:02x}, ", static_cast<unsigned char>(c));
            return out;
        };
        std::ofstream(scratch / "pass_shaders.cppm") << "inline constexpr mlibs::rhi::ShaderDesc vs_main{\n    .stage = mlibs::rhi::Stage::vertex,\n};\n"
                                                     << "inline constexpr std::array<std::byte, 11> vs_main_wgsl{" << bytes("fn vs() {}\n") << "};\n"
                                                     << "inline constexpr std::array<std::byte, 9> vs_main_essl{" << bytes("void m(){}") << "};\n"
                                                     << "inline constexpr std::array<std::byte, 2> other_spirv{0x01, 0x02};\n";
        const std::vector<std::filesystem::path> partitions{scratch / "pass_shaders.cppm"};
        const auto script = check::shader_check_script(partitions);
        std::filesystem::remove_all(scratch);
        expect(script.has_value()) << script.error_or("");
        if (!script) return;
        expect(script->contains(R"(var wgsl = {"pass_shaders:vs_main":"fn vs() {}\n"};)")) << script->substr(0, 400);
        expect(script->contains(R"("pass_shaders:vs_main":{"code":"void m(){}","file":"pass_shaders","stage":"vertex"})")) << script->substr(0, 400);
        expect(script->contains("1 WGSL and 1 ESSL entry points") && !script->contains("spirv"));
        expect(!check::shader_check_script(std::vector<std::filesystem::path>{}).has_value());
    };
}};

UTest consumer_cases{"devtools.consumer", [] {
    "a PNG's size from its header; build.ninja's outputs; the probes by name"_test = [] {
        namespace consumer = omui::devtools::consumer;
        std::string png("\x89PNG\r\n\x1a\n\0\0\0\rIHDR\0\0\x01\x40\0\0\0\xf0", 24);
        expect(consumer::png_size(png) == std::optional<std::pair<std::uint32_t, std::uint32_t>>{{320, 240}});
        expect(!consumer::png_size("GIF89a").has_value());
        const auto outputs = consumer::ninja_outputs("rule cxx\nbuild obj/a.o obj/a.d: cxx ../src/a.cpp\nbuild bin/app: link obj/a.o\n  flags = x\n");
        expect(outputs == std::set<std::string>{"obj/a.o", "obj/a.d", "bin/app"});
        expect(eq(consumer::names().size(), std::size_t{6}) && std::ranges::contains(consumer::names(), std::string_view("meng")));  // with meng's (EN3)
    };
}};
