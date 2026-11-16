module omui.devtools.check;

import std;
import mcpplibs.os;
import nlohmann.json;
import omui.devtools.run;

namespace omui::devtools::check {

namespace {

namespace fs = std::filesystem;
using json = nlohmann::json;

std::optional<std::string> read_text(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    return std::string(std::istreambuf_iterator<char>(in), {});
}

int failed(std::string_view message) {
    std::cout << "::error::" << message << '\n';
    return 1;
}

// `mcpp ARGS…` in `cwd`, standard error with standard output: its status and what it said.
std::pair<int, std::string> mcpp(const fs::path& root, std::vector<std::string> args, const fs::path& cwd = {}) {
    auto done = run::program(root, "mcpp", std::move(args), {.cwd = cwd, .merged = true});
    return done ? std::pair{done->status, std::move(done->out)} : std::pair{127, done.error()};
}

bool has_line(std::string_view text, std::string_view wanted, bool prefix) {
    for (auto part : std::views::split(text, '\n')) {
        const std::string_view line(part.begin(), part.end());
        if (prefix ? line.starts_with(wanted) : line == wanted) return true;
    }
    return false;
}

std::string kib(std::uint64_t bytes) { return std::format("{:.0f} KiB", static_cast<double>(bytes) / 1024.0); }

}  // namespace

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
    std::string manifest = read_text(root / "tools/ci/features-consumer/mcpp.toml.in").value_or("");
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
    const auto measured_report = json::parse(read_text(report).value_or(""), nullptr, false);
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
    const auto over = over_budget(measured, read_text(root / asked.budget).value_or(""));
    for (const auto& message : over) std::cout << "::error::" << message << '\n';
    if (over.empty()) std::cout << std::format("ok   within {}\n", mcpplibs::os::fs::generic_utf8(asked.budget));
    return over.empty() ? 0 : 1;
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
