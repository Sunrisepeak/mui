module omui.devtools.check;

import std;
import mcpplibs.os;
import nlohmann.json;
import mlibs.hash;
import omui.devtools.run;
import :support;

namespace omui::devtools::check {

using namespace support;
namespace fs = std::filesystem;
using json = nlohmann::json;

int features(const fs::path& root) {
    const auto [tested, said] = mcpp(root, {"test", "-p", "omui.features"});
    if (tested != 0 || !has_line(said, "sink: stdout", true)) {
        std::cout << said;
        return failed("sink-stdout (the default) did not build and pass");
    }
    std::cout << "ok   sink-stdout (the default) builds and passes\n";

    // The consumer, standalone, in a directory of its own.
    const fs::path consumer = fs::temp_directory_path() / std::format("omui-features-consumer-{}", std::random_device{}());
    std::error_code error;
    fs::create_directories(consumer / "tests", error);
    std::string manifest = mcpplibs::os::fs::read(root / "tools/ci/features-consumer/mcpp.toml.in").value_or("");
    const std::string repository = mcpplibs::os::fs::generic_utf8(fs::absolute(root));
    for (std::size_t at = manifest.find("{{repo}}"); at != std::string::npos; at = manifest.find("{{repo}}", at + repository.size()))
        manifest.replace(at, 8, repository);
    std::ofstream(consumer / "mcpp.toml", std::ios::binary) << manifest;
    fs::copy_file(root / "tools/ci/features-consumer/tests/memory_test.cpp", consumer / "tests" / "memory_test.cpp", fs::copy_options::overwrite_existing, error);
    const auto [chosen, chosen_said] = mcpp(root, {"test"}, consumer);
    fs::remove_all(consumer, error);
    if (chosen != 0 || !has_line(chosen_said, "  ok   features.memory", false)) {  // mtest's line for the suite: every case passed
        std::cout << chosen_said;
        return failed("sink-memory (an application's choice on its dependency edge) did not build and pass");
    }
    std::cout << "ok   sink-memory (an application's choice on its dependency edge) builds and passes\n";

    const auto [both, both_said] = mcpp(root, {"build", "-p", "omui.features", "--features", "sink-memory"});
    if (both == 0) return failed("both sinks at once built");
    if (!both_said.contains("module 'omui.features:sink' is provided by")) {
        std::cout << both_said;
        return failed("both sinks at once failed, but not because the partition is provided twice");
    }
    std::cout << "ok   both at once is refused: the partition would be provided twice\nfeatures: ok\n";
    return 0;
}

std::vector<std::string> over_budget(const std::map<std::string, std::uint64_t>& measured, std::string_view budget_text) {
    std::vector<std::string> out;
    const auto budgets = json::parse(budget_text, nullptr, false);
    if (budgets.is_discarded() || !budgets.contains("budgets")) return {"the budget file is not JSON with its budgets"};
    for (const auto& budget : budgets["budgets"]) {
        const std::string metric = budget.value("metric", "");
        const auto found = measured.find(metric);
        if (found == measured.end()) {
            out.push_back(std::format("{} is not measured", metric));
            continue;
        }
        const auto most = budget.value("max", std::uint64_t{0});
        if (found->second > most) out.push_back(std::format("{} is {} B, over its budget of {} B", metric, found->second, most));
    }
    return out;
}

int size(const fs::path& root, const Size& asked) {
    std::vector<std::string> args{"-p", asked.package, "--target", "wasm32-emscripten", "--profile", "wasm-size"};
    const auto built = run::build(root, std::move(args));
    if (!built) return failed(std::format("{} did not build for the Web with its names kept: {}", asked.package, built.error()));
    const auto program = std::ranges::find_if(*built, [](const run::Artifact& artifact) { return artifact.kind == "bin"; });
    if (program == built->end()) return failed(std::format("{}'s build reported no program", asked.package));
    fs::path wasm = program->path;
    wasm.replace_extension(".wasm");
    const fs::path report = asked.json.empty() ? root / "target" / (asked.package + ".wasm-size.json") : asked.json;
    std::vector<std::string> gen{"run", "-p", "omui.gen", "omui-gen", "--", "wasm-size", mcpplibs::os::fs::utf8(wasm), "--json", mcpplibs::os::fs::utf8(report),
                                 "--top", "40"};
    if (!asked.markdown.empty()) gen.insert(gen.end(), {"--markdown", mcpplibs::os::fs::utf8(asked.markdown)});
    if (const auto [status, said] = mcpp(root, gen); status != 0) {
        std::cout << said;
        return failed("omui-gen wasm-size failed");
    }
    const auto measured_report = json::parse(mcpplibs::os::fs::read(report).value_or(""), nullptr, false);
    if (measured_report.is_discarded()) return failed("the size report is not JSON");
    std::error_code error;
    const std::map<std::string, std::uint64_t> measured{{"wasm.shipped", measured_report.value("shipped", std::uint64_t{0})},
                                                        {"wasm.code", measured_report.value("code", std::uint64_t{0})},
                                                        {"wasm.data", measured_report.value("data", std::uint64_t{0})},
                                                        {"js.bytes", static_cast<std::uint64_t>(fs::file_size(program->path, error))}};
    const double code = std::max<double>(1.0, static_cast<double>(measured.at("wasm.code")));
    std::cout << std::format("{}: {} of Wasm shipped — code {}, data {}; {} functions ({} named); {} of JavaScript\n", asked.package,
                             kib(measured.at("wasm.shipped")), kib(measured.at("wasm.code")), kib(measured.at("wasm.data")),
                             measured_report.value("functions", 0), measured_report.value("named", 0), kib(measured.at("js.bytes")));
    int listed = 0;
    for (const auto& owner : measured_report.value("owners", json::array())) {
        if (listed++ == 12) break;
        const auto bytes = owner.value("bytes", std::uint64_t{0});
        std::cout << std::format("  {:>9}  {:4.1f}%  {}\n", kib(bytes), 100.0 * static_cast<double>(bytes) / code, owner.value("name", ""));
    }
    if (asked.budget.empty()) return 0;
    const auto over = over_budget(measured, mcpplibs::os::fs::read(root / asked.budget).value_or(""));
    for (const auto& message : over) std::cout << "::error::" << message << '\n';
    if (over.empty()) std::cout << std::format("ok   within {}\n", mcpplibs::os::fs::generic_utf8(asked.budget));
    return over.empty() ? 0 : 1;
}

std::expected<std::string, std::string> render(std::string_view text, const std::map<std::string, std::string>& values) {
    std::string out;
    std::size_t at = 0;
    for (std::size_t open = text.find("{{"); open != std::string_view::npos; open = text.find("{{", at)) {
        const std::size_t close = text.find("}}", open + 2);
        if (close == std::string_view::npos) break;
        const std::string name(text.substr(open + 2, close - open - 2));
        const bool plain = !name.empty() && std::ranges::all_of(name, [](char c) { return (c >= 'a' && c <= 'z') || c == '_' || c == '.'; });
        out.append(text.substr(at, open - at));
        if (!plain) {
            out.append(text.substr(open, close + 2 - open));
        } else if (const auto found = values.find(name); found != values.end()) {
            out += found->second;
        } else {
            return std::unexpected(name);
        }
        at = close + 2;
    }
    out.append(text.substr(at));
    return out;
}

namespace {

// A template rendered into `destination` (its omui dependencies and build rules pointed at this checkout).
std::expected<void, std::string> instantiate(const fs::path& root, std::string_view template_name, const fs::path& destination, std::string_view project) {
    const fs::path source = root / "modules/projects/omui/templates" / template_name;
    const std::map<std::string, std::string> values{{"project.name", std::string(project)}, {"self.name", "omui.sdk"}, {"self.version", "0.1.0"},
                                                    {"template.package.namespace", "openmui"}, {"template.package.name", "sdk"}};
    std::error_code error;
    for (auto it = fs::recursive_directory_iterator(source, error); it != fs::recursive_directory_iterator(); it.increment(error)) {
        if (error) return std::unexpected(error.message());
        if (it->is_directory(error) || it->path().filename() == "template.toml") continue;
        fs::path relative = fs::relative(it->path(), source);
        std::string text = mcpplibs::os::fs::read(it->path()).value_or("");
        if (it->path().extension() == ".in") {
            relative.replace_extension();
            auto rendered = render(text, values);
            if (!rendered) return std::unexpected(std::format("{}: no value for {{{{{}}}}}", mcpplibs::os::fs::generic_utf8(relative), rendered.error()));
            text = std::move(*rendered);
        }
        fs::create_directories((destination / relative).parent_path(), error);
        if (!(std::ofstream(destination / relative, std::ios::binary) << text)) return std::unexpected("cannot write " + mcpplibs::os::fs::utf8(destination / relative));
    }
    // This checkout instead of the published packages.
    const fs::path manifest = destination / "mcpp.toml";
    auto text = mcpplibs::os::fs::read(manifest);
    if (!text) return {};
    const std::string here = mcpplibs::os::fs::generic_utf8(fs::absolute(root).lexically_normal());
    constexpr std::array<std::pair<std::string_view, std::string_view>, 6> paths{{{"omui.sdk", "modules/projects/omui/sdk"},
                                                                                   {"omui.entry.player", "modules/projects/omui/entry/player"},
                                                                                   {"omui.player", "modules/projects/omui/player"},
                                                                                   {"omui.lang.js", "modules/projects/omui/lang/js"},
                                                                                   {"omui.native", "modules/projects/omui/native"},
                                                                                   {"omui.theme", "modules/projects/omui/theme"}}};
    std::string out;
    for (auto part : std::views::split(std::string_view(*text), '\n')) {
        std::string line(part.begin(), part.end());
        for (const auto& [package, relative] : paths) {
            // What the line says, its comment aside (`omui.theme = "0.1.0"  # the default theme …`).
            std::string bare = line.substr(0, line.find('#'));
            std::erase(bare, '"');
            std::erase_if(bare, [](char c) { return c == ' ' || c == '\t'; });
            if (bare == std::format("{}=0.1.0", package)) line = std::format("{} = {{ path = \"{}/{}\" }}", package, here, relative);
        }
        // The build plugins (one package, omui.build).
        const std::string published = "omui.build = { version = \"0.1.0\", host-module = true }";
        if (const auto at = line.find(published); at != std::string::npos)
            line.replace(at, published.size(), std::format("omui.build = {{ path = \"{}/mcpp/build\", host-module = true }}", here));
        out += line + "\n";
    }
    out.pop_back();
    std::ofstream(manifest, std::ios::binary) << out;
    return {};
}

// The workspace manifest put back as it was, however the step ends.
struct Restored {
    fs::path path;
    std::string text;
    fs::path scratch;
    ~Restored() {
        std::ofstream(path, std::ios::binary) << text;
        std::error_code error;
        fs::remove_all(scratch, error);
    }
};

}  // namespace

int templates(const fs::path& root) {
    const fs::path work = fs::temp_directory_path() / std::format("omui-templates-{}", std::random_device{}());
    std::error_code error;
    fs::create_directories(work, error);
    static_cast<void>(run::program(root, "mcpp", {"index", "update"}, {.cwd = work, .merged = true}));  // out of the tree a project reads the global index
    const fs::path check = root / ".template-check";
    const Restored restore{root / "mcpp.toml", mcpplibs::os::fs::read(root / "mcpp.toml").value_or(""), check};
    const Removed removed{work};
    const std::string here = mcpplibs::os::fs::generic_utf8(fs::absolute(root).lexically_normal());
    for (const auto& [template_name, project] : std::initializer_list<std::pair<std::string_view, std::string_view>>{
             {"app-cpp", "demo"}, {"app-ts", "tsdemo"}, {"component", "widgets"}, {"native", "wordkit"}, {"theme", "brandkit"}}) {
        std::cout << std::format("== {} ({})\n", template_name, project) << std::flush;
        const bool member = template_name == "component" || template_name == "native" || template_name == "theme";
        const fs::path destination = (member ? check : work) / project;
        if (auto made = instantiate(root, template_name, destination, project); !made) return failed(std::format("{}: {}", template_name, made.error()));
        std::pair<int, std::string> tested;
        if (member) {
            // A temporary member of this workspace, its paths relative to it.
            std::string manifest = mcpplibs::os::fs::read(root / "mcpp.toml").value_or("");
            const std::string anchor = "    \"tests/plugins/embed\",\n";
            const auto at = manifest.find(anchor);
            if (at == std::string::npos) return failed("the workspace's member list has no tests/plugins/embed to add after");
            manifest.insert(at + anchor.size(), std::format("    \".template-check/{}\",\n", project));
            std::ofstream(root / "mcpp.toml", std::ios::binary) << manifest;
            std::string own = mcpplibs::os::fs::read(destination / "mcpp.toml").value_or("");
            for (const auto& [from, to] : {std::pair{std::format("path = \"{}/", here), std::string("path = \"../../")},
                                           std::pair{std::format("path = \"{}\" }}", here), std::string("path = \"../..\" }")}})
                for (std::size_t found = own.find(from); found != std::string::npos; found = own.find(from, found + to.size())) own.replace(found, from.size(), to);
            std::ofstream(destination / "mcpp.toml", std::ios::binary) << own;
            tested = mcpp(root, {"test", "-p", std::string(project)});
        } else {
            const auto [built, built_said] = mcpp(root, {"build"}, destination);
            if (built != 0) {
                std::cout << built_said;
                return failed(std::format("{} did not build", template_name));
            }
            tested = mcpp(root, {"test"}, destination);
        }
        for (auto part : std::views::split(std::string_view(tested.second), '\n')) {
            const std::string_view line(part.begin(), part.end());
            if (line.contains("test result") || line.contains("FAIL") || line.contains("error")) std::cout << line << '\n';
        }
        if (tested.first != 0 || !tested.second.contains("test result ok")) return failed(std::format("{} did not pass its tests", template_name));
    }
    std::cout << "templates: ok\n";
    return 0;
}

int reflect(const fs::path& root, std::string_view profile) {
    const fs::path work = fs::temp_directory_path() / std::format("omui-reflect-canary-{}", std::random_device{}());
    std::error_code error;
    fs::create_directories(work / "src", error);
    const Removed removed{work};
    std::ofstream(work / "mcpp.toml") << R"([package]
name     = "reflect-canary"
version  = "0.1.0"
standard = "c++26"

[targets.reflect-canary]
kind = "bin"
main = "src/main.cpp"

[build]
sources  = ["src/*.cppm", "src/*.cpp"]
cxxflags = ["-freflection"]
)";
    std::ofstream(work / "src/codec.cppm") << R"(export module canary;
import std;
export std::expected<std::string, std::string> encode(int n);
)";
    // A unit the way a native one is: a C header in the global module fragment.
    std::ofstream(work / "src/codec.cpp") << "module;\n#include <stdio.h>\nmodule canary;\nimport std;\n"
                                             "std::expected<std::string, std::string> encode(int n) {\n"
                                             "    if (n < 0) return std::unexpected(std::string(\"negative\"));\n"
                                             "    ::printf(\"%d\\n\", n);\n"
                                             "    return std::string(static_cast<std::size_t>(n), 'x');\n}\n";
    std::ofstream(work / "src/main.cpp") << R"(import std;
import canary;
struct Settings { int volume; std::string name; };
int main() {
    std::string names;
    template for (constexpr auto m : std::define_static_array(
                      std::meta::nonstatic_data_members_of(^^Settings, std::meta::access_context::unchecked())))
        names += std::string(std::meta::identifier_of(m)) + " ";
    return names == "volume name " && encode(3) == "xxx" ? 0 : 1;
}
)";
    const auto [canary, canary_said] = mcpp(root, {"run", "--profile", std::string(profile)}, work);
    std::error_code made;
    fs::create_directories(root / "target", made);
    std::ofstream(root / "target" / "reflect-canary.log", std::ios::binary) << canary_said;
    if (canary == 0) {
        std::cout << "the toolchain builds reflection beside C headers: running omui.reflect's suite\n" << std::flush;
        const auto [suite, suite_said] = mcpp(root, {"test", "-p", "omui.reflect", "--profile", std::string(profile)});
        std::cout << suite_said;
        if (suite != 0 || !suite_said.contains("test result ok")) return failed("omui.reflect failed");
        return 0;
    }
    for (auto part : std::views::split(std::string_view(canary_said), '\n')) {
        const std::string_view line(part.begin(), part.end());
        if (!line.contains("conflicting imported declaration")) continue;
        std::cout << line << "\n::warning::this GCC cannot build C++26 reflection in a graph with C headers (V-08: libstdc++'s std built with "
                             "-freflection does not merge __mbstate_t); omui.reflect's suite waits for a toolchain that can\n";
        return 0;
    }
    std::cout << canary_said.substr(canary_said.size() > 4000 ? canary_said.size() - 4000 : 0);
    return failed("the reflection canary failed for another reason than V-08");
}

int sweep(const fs::path& root, const std::vector<std::string>& profile_args) {
    struct Program {
        std::string_view package, binary, directory;  // where it runs: a player reads its mcpp.toml there
    };
    constexpr std::array<Program, 14> programs{{
        {"omui.example.counter", "tally", "examples/03-counter"},
        {"omui.example.rating", "reading-list", "examples/04-rating"},
        {"omui.example.contrast", "contrast", "examples/05-contrast"},
        {"omui.example.theming", "theme-preview", "examples/06-theming"},
        {"omui.example.headless-and-agent", "notes-agent", "examples/07-headless-and-agent"},
        {"omui.example.markdown-viewer", "md-view", "examples/08-markdown-viewer"},
        {"omui.example.markdown-stream", "md-stream", "examples/09-markdown-stream"},
        {"omui.example.space", "space", "examples/10-canvas-basics"},
        {"omui.example.notes", "notes", "examples/12-notes"},
        {"omui.showcase.style_gallery", "omui-styles", "showcase/style-gallery"},
        {"omui.showcase.widget_breakout", "omui-breakout", "showcase/widget-breakout"},
        {"omui.showcase.markdown_wall", "omui-wall", "showcase/markdown-wall"},
        {"omui.showcase.fighting", "omui-fight", "showcase/fighting"},
        {"omui.site", "omui-site", "site"},
    }};
    std::vector<std::string> args{"-p", "omui.cli"};
    for (const auto& program : programs) args.insert(args.end(), {"-p", std::string(program.package)});
    args.insert(args.end(), profile_args.begin(), profile_args.end());
    const auto built = run::build(root, std::move(args));
    if (!built) return failed(std::format("the programs did not build: {}", built.error()));
    std::map<std::string, fs::path> binary;
    for (const auto& artifact : *built)
        if (artifact.kind == "bin") binary[mcpplibs::os::fs::utf8(artifact.path.filename())] = artifact.path;
    if (!binary.contains("omui")) return failed("the build reported no omui");
    const fs::path out = root / "target" / "sweep";
    std::error_code error;
    fs::create_directories(out, error);
    const fs::path agents = fs::temp_directory_path() / std::format("omui-sweep-{}", std::random_device{}());
    const Removed removed{agents};
    int failures = 0;
    namespace process = mcpplibs::os::process;
    for (const auto& program : programs) {
        const std::string name(program.binary);
        fs::remove_all(agents, error);
        fs::create_directories(agents, error);
        if (!binary.contains(name)) {
            std::cout << std::format("FAIL {}: not built\n", name);
            ++failures;
            continue;
        }
        // The program, its output to its log; the endpoint it writes is how the sweep finds it.
        process::spawn_options options;
        options.program = binary[name];
        options.args = {"--size", "1440x900", "--serve", "--agent-input", "--agent-dir", mcpplibs::os::fs::utf8(agents)};
        options.cwd = root / program.directory;
        options.in = process::stdio::null;
        options.out = process::stdio::pipe;
        options.err = process::stdio::output;
        auto started = process::process::spawn(options);
        if (!started) {
            std::cout << std::format("FAIL {}: did not start: {}\n", name, started.error().message());
            ++failures;
            continue;
        }
        std::jthread logger([&started, log = out / (name + ".log")] {
            std::ofstream file(log, std::ios::binary);
            for (;;) {
                auto chunk = started->read_output();
                if (!chunk || chunk->empty()) break;
                file << *chunk << std::flush;
            }
        });
        std::string app;
        for (int look = 0; look < 200 && app.empty(); ++look) {
            for (const auto& entry : fs::directory_iterator(agents, error)) {
                const std::string file = mcpplibs::os::fs::utf8(entry.path().filename());
                if (file.ends_with(".endpoint.omui.json")) app = file.substr(0, file.size() - std::string_view(".endpoint.omui.json").size());
            }
            if (app.empty()) std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (app.empty()) {
            std::cout << std::format("FAIL {}: no endpoint (see target/sweep/{}.log)\n", name, name);
            ++failures;
        } else {
            auto swept = run::program(root, mcpplibs::os::fs::utf8(binary["omui"]), {"agent", "sweep", app, "--dir", mcpplibs::os::fs::utf8(agents)});
            const std::string verdicts = swept ? swept->out : std::string{};
            std::ofstream(out / (name + ".sweep.omui.jsonl"), std::ios::binary) << verdicts;
            std::string summary;
            for (auto part : std::views::split(std::string_view(verdicts), '\n'))
                if (const std::string_view line(part.begin(), part.end()); line.contains("\"nodes\"")) summary = line;
            if (!swept || swept->status != 0) {
                std::cout << std::format("FAIL {} ({}): {}\n", name, app, summary);
                for (auto part : std::views::split(std::string_view(verdicts), '\n'))
                    if (const std::string_view line(part.begin(), part.end()); line.contains("\"verdict\":\"failed\"")) std::cout << line << '\n';
                ++failures;
            } else {
                std::cout << std::format("ok   {} ({}): {}\n", name, app, summary);
            }
        }
        std::cout << std::flush;
        started->kill();
        static_cast<void>(started->wait());
    }
    if (failures == 0) std::cout << "sweep: ok\n";
    return failures == 0 ? 0 : 1;
}

int toy(const fs::path& root, std::vector<std::string> packages) {
    constexpr std::string_view revision = "ae156ecb3391dfb22bdd93d45d6b0067e1e5b723";
    constexpr std::string_view digest = "cbbf1244fff1179cfa48272cddb2cbcbd870da73d5fff1c5f30720231d6eeb0c";
    const fs::path out = root / "target" / "toy";
    std::error_code error;
    fs::create_directories(out, error);
    const fs::path doctor = out / std::format("toy_doctor-{}.py", revision.substr(0, 7));
    const auto sha256 = [](const fs::path& file) { return mlibs::hash::sha256_hex(mcpplibs::os::fs::read(file).value_or("")); };
    if (!fs::exists(doctor) || sha256(doctor) != digest) {
        fs::path part = doctor;
        part += ".part";
        const auto fetched = run::output(root, "curl", {"-fsSL", "--retry", "3",
                                                        std::format("https://raw.githubusercontent.com/bilibili/toy/{}/skills/toy/scripts/toy_doctor.py", revision),
                                                        "-o", mcpplibs::os::fs::utf8(part)});
        if (!fetched) return failed(std::format("toy_doctor.py could not be fetched: {}", fetched.error()));
        if (sha256(part) != digest) return failed("toy_doctor.py is not the pinned file");
        fs::rename(part, doctor, error);
    }
    if (packages.empty()) packages = {"omui.showcase.fighting", "omui.showcase.widget_breakout"};
    int failures = 0;
    for (const auto& package : packages) {
        std::cout << "toy: " << package << '\n' << std::flush;
        const std::optional<fs::path> index =
            run::packed(root, {"-p", package, "--target", "wasm32-emscripten", "--release", "--pkg-format", "dist-toy"}, "index.html");
        if (!index) {
            std::cout << std::format("::error::{} did not pack for Toy\n", package);
            ++failures;
            continue;
        }
        const fs::path built = index->parent_path();
        const std::string name = mcpplibs::os::fs::utf8(built.filename());
        fs::remove_all(out / name, error);
        fs::copy(built, out / name, fs::copy_options::recursive, error);
        fs::path listing = built;
        listing += ".toy.omui.json";
        fs::copy_file(listing, out / (name + ".toy.omui.json"), fs::copy_options::overwrite_existing, error);
        const auto about = json::parse(mcpplibs::os::fs::read(out / (name + ".toy.omui.json")).value_or(""), nullptr, false);
        if (!about.is_discarded()) {
            const auto mib = [](std::uint64_t bytes) { return std::format("{:.2f} MiB", static_cast<double>(bytes) / 1048576.0); };
            auto files = about.value("files", json::array());
            std::cout << std::format("  index.html {} bytes (limit {}); total {} in {} files\n", about.value("index_bytes", 0), about.value("index_limit", 0),
                                     mib(about.value("total_bytes", std::uint64_t{0})), files.size());
            std::vector<json> largest(files.begin(), files.end());
            std::ranges::sort(largest, std::greater{}, [](const json& file) { return file.value("bytes", std::uint64_t{0}); });
            for (std::size_t at = 0; at < std::min<std::size_t>(4, largest.size()); ++at)
                std::cout << std::format("    {:>10}  {}\n", mib(largest[at].value("bytes", std::uint64_t{0})), largest[at].value("path", ""));
        }
        const auto checked = run::program(root, "python3", {mcpplibs::os::fs::utf8(doctor), mcpplibs::os::fs::utf8(out / name)}, {.shown = true});
        if (!checked || checked->status != 0) {
            std::cout << std::format("::error::Toy's doctor refused {}\n", name);
            ++failures;
        }
    }
    return failures == 0 ? 0 : 1;
}

int atspi(const fs::path& root, const std::vector<std::string>& profile_args) {
    std::vector<std::string> args{"-p", "omui.example.counter", "-p", "omui.example.headless-and-agent"};
    args.insert(args.end(), profile_args.begin(), profile_args.end());
    const auto built = run::build(root, std::move(args));
    if (!built) return failed(std::format("the examples did not build: {}", built.error()));
    std::map<std::string, fs::path> program;
    for (const auto& artifact : *built)
        if (artifact.kind == "bin") program[mcpplibs::os::fs::utf8(artifact.path.filename())] = artifact.path;
    if (!program.contains("tally") || !program.contains("notes-agent")) return failed("the build reported no tally or notes-agent");
    const auto self = mcpplibs::os::env::image();
    if (!self) return failed("this program's own image is unknown");
    // A private session bus, the accessibility bus turned on there as Orca turns it on; the registry's own line
    // comes before the report.
    auto done = run::program(root, "dbus-run-session", {"--", mcpplibs::os::fs::utf8(*self), "--root", mcpplibs::os::fs::utf8(root), "atspi",
                                                        mcpplibs::os::fs::utf8(program["tally"]), mcpplibs::os::fs::utf8(program["notes-agent"])});
    if (!done) return failed(done.error());
    const auto start = done->out.starts_with("{") ? 0 : done->out.find("\n{");
    std::cout << (start == std::string::npos ? done->out : done->out.substr(start == 0 ? 0 : start + 1));
    return done->status;
}

}  // namespace omui::devtools::check
