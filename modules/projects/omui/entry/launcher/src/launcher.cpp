// The shared runtime's launcher (omui.entry.launcher; E3b, D-250).
module omui.entry.launcher;

import std;
import mcpplibs.os;
import mcpplibs.cmdline;
import mlibs.json;

namespace omui::launcher {

namespace {

namespace fs = std::filesystem;
namespace os = mcpplibs::os;

std::string joined(std::span<const std::string> words) {
    std::string out;
    for (const std::string& word : words) out += (out.empty() ? "" : ", ") + word;
    return out;
}

// A line on this program's standard error or output, as bytes (mcpplibs.os: no C++ stream is linked for it).
void refused(std::string line) { (void)os::stdio::write_error(line.append("\n")); }
void answered(std::string line) { (void)os::stdio::write_output(line.append("\n")); }

// "0.4" of "0.4", "0.4.2" or "0.4.2-dev.1": the specification's major and minor.
std::string major_minor(std::string_view version) {
    const auto first = version.find('.');
    if (first == std::string_view::npos) return std::string(version);
    const auto second = version.find_first_of(".-+", first + 1);
    return std::string(version.substr(0, second));
}

// The specification, the runtime line and that line's capabilities this launcher was released with: where they
// satisfy an application, the command installs that line; else the newest runtime (its runtime.json says).
constexpr std::string_view released_openmui = "0.4";
constexpr std::string_view released_runtime = "0.1";
constexpr std::array<std::string_view, 4> released_capabilities{"accessibility-bridge", "agent", "generate", "gpu"};

}  // namespace

std::strong_ordering compare_versions(std::string_view left, std::string_view right) {
    const auto split = [](std::string_view version) {
        const auto dash = version.find('-');
        return std::pair{version.substr(0, dash), dash == std::string_view::npos ? std::string_view{} : version.substr(dash + 1)};
    };
    const auto [left_core, left_pre] = split(left);
    const auto [right_core, right_pre] = split(right);
    // Part by part, as numbers where both are (a missing part is 0); else as text.
    const auto parts = [](std::string_view text) {
        std::vector<std::string_view> out;
        for (const auto part : std::views::split(text, '.')) out.emplace_back(part.begin(), part.end());
        return out;
    };
    const auto number = [](std::string_view part) -> std::optional<std::uint64_t> {
        std::uint64_t value = 0;
        const auto [end, failure] = std::from_chars(part.data(), part.data() + part.size(), value);
        if (failure != std::errc{} || end != part.data() + part.size()) return std::nullopt;
        return value;
    };
    const auto compare_parts = [&](std::string_view first, std::string_view second) {
        const auto first_parts = parts(first), second_parts = parts(second);
        for (std::size_t index = 0; index < std::max(first_parts.size(), second_parts.size()); ++index) {
            const std::string_view mine = index < first_parts.size() ? first_parts[index] : "0";
            const std::string_view theirs = index < second_parts.size() ? second_parts[index] : "0";
            const auto mine_number = number(mine), their_number = number(theirs);
            const std::strong_ordering order = mine_number && their_number ? *mine_number <=> *their_number : mine.compare(theirs) <=> 0;
            if (order != 0) return order;
        }
        return std::strong_ordering::equal;
    };
    if (const auto order = compare_parts(left_core, right_core); order != 0) return order;
    if (left_pre.empty() || right_pre.empty()) return left_pre.empty() <=> right_pre.empty();  // a release after its pre-releases
    return compare_parts(left_pre, right_pre);
}

// Read in place by mlibs.json's own scanner: the launcher links no JSON parser (red 145).
std::expected<Runtime, std::string> read_runtime(std::string_view text, const fs::path& directory) {
    const auto described = mlibs::json::Scan::read(text, os::fs::utf8(directory / "runtime.json"));
    if (!described) return std::unexpected(described.error().text());
    const auto fail = [&](std::string_view at, std::string message) {
        return std::unexpected(described->problem(at, std::move(message), "omui-player --runtime writes one").text());
    };
    if (described->text("/$format") != std::optional<std::string>("runtime@1"))
        return fail("/$format", "a runtime's description names its format: \"$format\": \"runtime@1\"");
    Runtime out{.directory = directory, .version = described->text("/runtime/version").value_or(""), .openmui = described->text("/runtime/openmui").value_or("")};
    if (out.version.empty()) return fail("/runtime/version", "a runtime says its version: \"version\": \"0.1.0\"");
    if (out.openmui.empty()) return fail("/runtime/openmui", "a runtime says the specification it implements: \"openmui\": \"0.4\"");
    out.capabilities = described->texts("/runtime/capabilities").value_or(std::vector<std::string>{});
    return out;
}

std::vector<Runtime> installed(const fs::path& xlings_home) {
    std::vector<Runtime> out;
    std::error_code error;
    // <namespace>-x-omui-runtime/<version>/: the index's (xim) or a recipe registered beside it (local).
    for (const auto& package : fs::directory_iterator(xlings_home / "data" / "xpkgs", error)) {
        if (!package.is_directory(error) || !os::fs::utf8(package.path().filename()).ends_with("-x-omui-runtime")) continue;
        for (const auto& version : fs::directory_iterator(package.path(), error)) {
            if (!version.is_directory(error)) continue;
            // runtime.json in the version's directory, or in the one directory an archive unpacked into.
            std::vector<fs::path> places{version.path()};
            for (const auto& inner : fs::directory_iterator(version.path(), error))
                if (inner.is_directory(error)) places.push_back(inner.path());
            for (const fs::path& place : places)
                if (const auto text = os::fs::read(place / "runtime.json")) {
                    if (auto runtime = read_runtime(*text, place)) out.push_back(std::move(*runtime));
                    break;
                }
        }
    }
    std::ranges::sort(out, [](const Runtime& left, const Runtime& right) { return compare_versions(left.version, right.version) > 0; });
    return out;
}

Choice choose(const Declared& wanted, std::span<const Runtime> runtimes) {
    Choice out;
    std::string reasons;
    for (const Runtime& runtime : runtimes) {  // the newest first
        std::vector<std::string> missing;
        for (const std::string& need : wanted.needs)
            if (std::ranges::find(runtime.capabilities, need) == runtime.capabilities.end()) missing.push_back(need);
        std::string why;
        if (!wanted.runtime.empty() && runtime.version != wanted.runtime.substr(1)) why = std::format("not {}", wanted.runtime.substr(1));
        else if (major_minor(runtime.openmui) != major_minor(wanted.openmui)) why = std::format("openmui {}, not {}", runtime.openmui, wanted.openmui);
        else if (!missing.empty()) why = std::format("without {}", joined(missing));
        if (why.empty()) {
            out.chosen = runtime;
            return out;
        }
        reasons += std::format("\n  {} (openmui {}; {}): {}", runtime.version, runtime.openmui, runtime.capabilities.empty() ? std::string("no capabilities") : joined(runtime.capabilities), why);
    }
    const std::string wanted_text = std::format("openmui {}{}{}", wanted.openmui, wanted.needs.empty() ? "" : std::format(" with {}", joined(wanted.needs)),
                                                wanted.runtime.empty() ? "" : std::format(", runtime {}", wanted.runtime.substr(1)));
    out.why = runtimes.empty() ? std::format("no omui runtime is installed; the application wants {}", wanted_text)
                               : std::format("no installed omui runtime gives {}:{}", wanted_text, reasons);
    // The runtime line that implements the specification it wants: the one this launcher was released with, else
    // the newest (whose description says whether it does).
    const bool released_line = major_minor(wanted.openmui) == released_openmui &&
                               std::ranges::all_of(wanted.needs, [](const std::string& need) { return std::ranges::contains(released_capabilities, need); });
    out.install = !wanted.runtime.empty() ? std::format("xlings install omui-runtime@{}", wanted.runtime.substr(1))
                  : released_line         ? std::format("xlings install omui-runtime@{}", released_runtime)
                                          : std::string("xlings install omui-runtime");
    return out;
}

int launch(const Launch& what) {
    fs::path application = what.application.empty() ? fs::current_path() : what.application;
    if (application.filename() == "mcpp.toml") application = application.parent_path();
    const auto manifest = os::fs::read(application / "mcpp.toml");
    if (!manifest) {
        refused(std::format("omui-launch: no mcpp.toml in {} (an application's directory)", os::fs::utf8(application)));
        return 2;
    }
    auto wanted = declared(*manifest);
    if (!wanted) {
        refused(std::format("omui-launch: {}: {}", os::fs::utf8(application / "mcpp.toml"), wanted.error()));
        return 2;
    }
    // xlings' home: $XLINGS_HOME, else .xlings in the person's home (each system's own, mcpplibs.os).
    const auto home_variable = os::env::get("XLINGS_HOME");
    const fs::path home = home_variable && !home_variable->empty() ? os::fs::from_utf8(*home_variable)
                                                                   : os::dirs::home().value_or(fs::current_path()) / ".xlings";
    const std::vector<Runtime> runtimes = installed(home);
    const Choice choice = choose(*wanted, runtimes);
    if (!choice.chosen) {
        refused(std::format("omui-launch: {}\n  install one: {}", choice.why, choice.install));
        return 2;
    }
    if (what.which) {  // runtime@1, as the player's --runtime writes it, with where it is
        using mlibs::json::quote;
        std::string capabilities;
        for (const std::string& capability : choice.chosen->capabilities) capabilities += (capabilities.empty() ? "" : ",") + quote(capability);
        answered(std::format(R"({{"$format":"runtime@1","runtime":{{"version":{},"openmui":{},"capabilities":[{}],"directory":{}}}}})", quote(choice.chosen->version),
                             quote(choice.chosen->openmui), capabilities, quote(os::fs::utf8(choice.chosen->directory))));
        return 0;
    }
    // Its player (the package's launcher where it carries one: a self-contained runtime), given the application.
    const fs::path player = fs::exists(choice.chosen->directory / "omui-player") ? choice.chosen->directory / "omui-player"
                                                                                 : choice.chosen->directory / "bin" / "omui-player";
    os::process::spawn_options options;
    options.program = player;
    options.args = {"--manifest", os::fs::utf8(application)};
    options.args.insert(options.args.end(), what.passed.begin(), what.passed.end());
    options.out = options.err = os::process::stdio::inherit;
    // What this launcher's own bundle set for itself (a self-contained package's script: its directory, its C
    // library's locale and conversions) is not the runtime's: dropped, the runtime's script sets its own.
    if (const auto bundle = os::env::get("MCPP_BUNDLE_DIR"); bundle && !bundle->empty()) {
        options.env.emplace_back("MCPP_BUNDLE_DIR", std::nullopt);
        for (const char* variable : {"LOCPATH", "GCONV_PATH"})
            if (os::env::get(variable).value_or("").starts_with(*bundle)) options.env.emplace_back(variable, std::nullopt);
    }
    if (auto replaced = os::process::exec(std::move(options)); !replaced) {
        refused(std::format("omui-launch: {} did not start: {}", os::fs::utf8(player), replaced.error().message()));
        return 1;
    }
    return 0;
}

int main(int argc, char** argv) {
    using mcpplibs::cmdline::App, mcpplibs::cmdline::Arg, mcpplibs::cmdline::Option;
    auto parsed = App("omui-launch")
                      .version(std::string(launcher_version))
                      .description("Starts the application in APP (its directory or mcpp.toml; default: here) with the newest installed omui runtime that "
                                   "satisfies its [package.metadata.openmui.runtime]; what follows `--` goes to it. Exit 2 when none does: why each "
                                   "installed one does not, and the command that installs one")
                      .option(Option("which").help("say the runtime it would start (runtime@1, with its directory); start nothing"))
                      .arg(Arg("app").help("the application's directory or mcpp.toml (default: here; `.` before `--` ARGS)"))
                      .parse(argc, argv);
    if (!parsed) {
        if (!parsed.error().is_error()) return 0;  // --help, --version: said
        refused(std::format("omui-launch: {}", parsed.error().message));
        return 2;
    }
    Launch what{.which = parsed->is_flag_set("which")};
    if (parsed->positional_count() > 0) what.application = os::fs::from_utf8(parsed->positional(0));
    for (std::size_t index = 1; index < parsed->positional_count(); ++index) what.passed.push_back(parsed->positional(index));
    return launch(what);
}

}  // namespace omui::launcher
