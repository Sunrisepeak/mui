// omui check with no files (W1 step 5): the project here, as the runtime and
// tsc read it, in one list of diagnostics.
//
//   1. every TypeScript file: erasable syntax only (omui.lang.ts:strip, what
//      the runtime runs), its problems where they are and what to write instead;
//   2. every view document, as `omui check FILE…` checks it;
//   3. the types: the application declares what it registers (`mcpp run --
//      --types .omui/types`, a player-run application: its components, its
//      native modules, the define functions), then tsc (TypeScript 7, `xim:
//      typescript`) over its sources — strict, erasable syntax only.
//
// tsc's own diagnostics become omui's (`file:line:column: error: … [TS2322]`,
// or a JSON record each with --json), with a hint where one is known.
module omui.cli;

import std;
import mcpplibs.os;
import omui.core;
import omui.viewdoc;
import omui.lang.ts;

namespace omui::cli {

namespace {

namespace fs = std::filesystem;
namespace vd = omui::viewdoc;

std::string utf8(const fs::path& path) { return mcpplibs::os::fs::utf8(path); }
// A path as diagnostics write it, with forward slashes on every platform.
std::string shown(const fs::path& path) {
    std::string out = utf8(path);
    std::ranges::replace(out, '\\', '/');
    return out;
}

// Directories a project's sources are not in.
bool outside_sources(const fs::path& relative) {
    return std::ranges::any_of(relative, [](const fs::path& part) {
        const std::string name = utf8(part);
        return name == "target" || name == "node_modules" || (name.size() > 1 && name.front() == '.');
    });
}

std::vector<fs::path> sources(const fs::path& root, auto&& wanted) {
    std::vector<fs::path> out;
    std::error_code error;
    for (auto it = fs::recursive_directory_iterator(root, error); !error && it != fs::recursive_directory_iterator(); it.increment(error)) {
        const fs::path relative = it->path().lexically_relative(root);
        if (it->is_directory() && outside_sources(relative)) {
            it.disable_recursion_pending();
            continue;
        }
        if (it->is_regular_file() && wanted(utf8(it->path().filename()))) out.push_back(relative);
    }
    std::ranges::sort(out);
    return out;
}

vd::Diagnostic diagnostic(vd::Diagnostic::Level level, std::string code, std::uint32_t line, std::uint32_t column, std::string message,
                          std::string hint = {}) {
    vd::Diagnostic d;
    d.level = level;
    d.code = std::move(code);
    d.line = line;
    d.column = column;
    d.message = std::move(message);
    d.hint = std::move(hint);
    return d;
}

// What to do about a tsc diagnostic, where it is one an application meets often.
std::string hint_of(std::string_view code) {
    if (code == "TS2305" || code == "TS2724")
        return "the builders in `openmui` are the components this application registers (.omui/types/openmui.d.ts lists them)";
    if (code == "TS2307") return R"(the runtime has "openmui", "openmui:native/<name>", "openmui/define", "omui/define" and files by path)";
    if (code == "TS2353") return "not a property of this component or of every node (NodeProps)";
    return {};
}

// tsc --pretty false: `file(line,col): error TS2322: message`, its continuation lines indented.
std::vector<Found> read_tsc(std::string_view text) {
    static const std::regex located(R"(^(.+)\((\d+),(\d+)\): (error|warning|message) (TS\d+): (.*)$)");
    static const std::regex global(R"(^(error|warning|message) (TS\d+): (.*)$)");
    std::vector<Found> out;
    for (auto part : text | std::views::split('\n')) {
        std::string line(part.begin(), part.end());
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        std::smatch m;
        if (std::regex_match(line, m, located)) {
            if (m[5] == "TS1294") continue;  // not erasable: the first step says so, with what to write instead
            const auto level = m[4] == "warning" || m[4] == "message" ? vd::Diagnostic::Level::warning : vd::Diagnostic::Level::error;
            out.push_back({m[1].str(), diagnostic(level, m[5].str(), static_cast<std::uint32_t>(std::stoul(m[2].str())),
                                                  static_cast<std::uint32_t>(std::stoul(m[3].str())), m[6].str(), hint_of(m[5].str()))});
        } else if (std::regex_match(line, m, global)) {
            const auto level = m[1] == "error" ? vd::Diagnostic::Level::error : vd::Diagnostic::Level::warning;
            out.push_back({".omui/types/tsconfig.json", diagnostic(level, m[2].str(), 0, 0, m[3].str())});
        } else if (!out.empty() && (line.front() == ' ' || line.front() == '\t')) {
            const auto first = line.find_first_not_of(" \t");
            out.back().diagnostic.message += " — " + line.substr(first);
        }
    }
    return out;
}

std::optional<mcpplibs::os::process::output> run_quietly(std::string_view program, std::vector<std::string> args, const fs::path& cwd) {
    const auto found = mcpplibs::os::env::find_executable(program);
    if (!found) return std::nullopt;
    mcpplibs::os::process::spawn_options options;
    options.program = *found;
    options.args = std::move(args);
    options.cwd = cwd;
    auto ran = mcpplibs::os::process::run(std::move(options));
    if (!ran) return std::nullopt;
    return std::move(*ran);
}

}  // namespace

int check_project(bool json, std::ostream& out, std::ostream& err) {
    const fs::path root = fs::current_path();
    auto manifest = mcpplibs::os::fs::read(root / "mcpp.toml");
    if (!manifest) {
        err << "omui check: no mcpp.toml here — give the files to check, or run it in a project\n";
        return 2;
    }
    std::vector<Found> found;

    // 1. Erasable syntax, as the runtime strips it.
    const auto typescript = sources(root, [](std::string_view name) {
        return (name.ends_with(".ts") || name.ends_with(".mts")) && !name.ends_with(".d.ts");
    });
    bool scripts = false;  // a file that is not data: the application runs TypeScript
    for (const fs::path& file : typescript) {
        const std::string name = utf8(file.filename());
        if (!name.ends_with(".omui.ts") && name != "omui.config.ts") scripts = true;
        auto text = mcpplibs::os::fs::read(root / file);
        if (!text) {
            found.push_back({shown(file), diagnostic(vd::Diagnostic::Level::error, "read", 0, 0, text.error().message())});
            continue;
        }
        if (auto stripped = lang::ts::strip_types(*text); !stripped)
            for (const lang::ts::TypeScriptProblem& problem : stripped.error())
                found.push_back({shown(file), diagnostic(vd::Diagnostic::Level::error, "erasable", static_cast<std::uint32_t>(problem.line),
                                                                   static_cast<std::uint32_t>(problem.column), problem.text)});
    }

    // 2. The view documents.
    std::vector<std::string> documents;
    for (const fs::path& file : sources(root, [](std::string_view name) { return name.ends_with(".view.omui.json"); }))
        documents.push_back(shown(file));
    if (auto views = check_documents(documents, err)) std::ranges::move(*views, std::back_inserter(found));
    else return 1;

    // 3. The types, against what the application registers.
    if (scripts && manifest->contains("omui.entry.player")) {
        const fs::path dir = root / ".omui" / "types";
        const auto declared = run_quietly("mcpp", {"run", "-q", "--", "--types", utf8(dir)}, root);
        if (!declared || declared->status.shell_code() != 0) {
            const std::string why = declared ? declared->err : std::string("mcpp is not on PATH");
            found.push_back({"mcpp.toml", diagnostic(vd::Diagnostic::Level::error, "types", 0, 0,
                                                     std::format("the application did not declare its types (mcpp run -- --types): {}",
                                                                 why.substr(why.size() > 2000 ? why.size() - 2000 : 0)))});
        } else if (const auto checked = run_quietly("tsc", {"-p", utf8(dir / "tsconfig.json"), "--pretty", "false"}, root); !checked) {
            found.push_back({"mcpp.toml", diagnostic(vd::Diagnostic::Level::warning, "tsc-missing", 0, 0, "tsc is not installed: the types were not checked",
                                                     "xlings install xim:typescript (TypeScript 7, no node), or name it in mcpp.toml")});
        } else if (auto reported = read_tsc(checked->out); reported.empty() && checked->status.shell_code() != 0) {
            // It failed and said nothing of the sources: what it did say (a launcher's warnings go there too).
            found.push_back({".omui/types/tsconfig.json", diagnostic(vd::Diagnostic::Level::error, "tsc", 0, 0,
                                                                     std::format("tsc failed ({}): {}", checked->status.shell_code(), checked->err))});
        } else {
            std::ranges::move(reported, std::back_inserter(found));
        }
    }
    return report(found, typescript.size() + documents.size(), json, out, err);
}

}  // namespace omui::cli
