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
// or a JSON record each with --json), with a hint where one is known. With
// --portable, 4: what ties a script to omui (D-157) — an `omui` import, a
// builder of a component that is not the spec's (api.json says which).
//
// omui describe --api (W3, D-145): the same declaration step, its api.json
// printed, or written as llms.txt — the source of the declarations too.
module omui.cli;

import std;
import mcpplibs.os;
import mcpplibs.cmdline;
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

// The application declares what it registers (`mcpp run -- --types DIR`, a
// player-run application): its TypeScript declarations and api.json, in
// .omui/types. The directory, or the diagnostic saying why not.
std::expected<fs::path, Found> declare_types(const fs::path& root) {
    const fs::path dir = root / ".omui" / "types";
    const auto declared = run_quietly("mcpp", {"run", "-q", "--", "--types", utf8(dir)}, root);
    if (declared && declared->status.shell_code() == 0) return dir;
    const std::string why = declared ? declared->err : std::string("mcpp is not on PATH");
    return std::unexpected(Found{"mcpp.toml", diagnostic(vd::Diagnostic::Level::error, "types", 0, 0,
                                                         std::format("the application did not declare its types (mcpp run -- --types): {}",
                                                                     why.substr(why.size() > 2000 ? why.size() - 2000 : 0)))});
}

// Line and column (1-based) of a byte offset.
std::pair<std::uint32_t, std::uint32_t> place_of(std::string_view text, std::size_t offset) {
    const std::string_view before = text.substr(0, offset);
    const auto line = static_cast<std::uint32_t>(std::ranges::count(before, '\n') + 1);
    const auto start = before.rfind('\n');
    return {line, static_cast<std::uint32_t>(offset - (start == std::string_view::npos ? 0 : start + 1) + 1)};
}

// What ties a script to omui (D-157): an `omui` import, or a builder of a
// component that is not the spec's (api.json says which are).
std::vector<Found> unportable(const fs::path& root, const std::vector<fs::path>& scripts, const Value& api) {
    std::map<std::string, std::string> omui_builders;  // builder → component
    if (const Value* components = api.get("components"); components && components->map())
        for (const auto& [name, component] : *components->map())
            if (const Value* in_spec = component.get("spec"); !in_spec || !in_spec->as_bool()) omui_builders.emplace(lang::ts::camel_case(name), name);
    static const std::regex named(R"re(import\s+(?:type\s+)?\{([^}]*)\}\s*from\s*"openmui")re");
    static const std::regex implementation(R"re(from\s*"(omui(?:/[^"]*)?)")re");
    std::vector<Found> out;
    for (const fs::path& file : scripts) {
        auto text = mcpplibs::os::fs::read(root / file);
        if (!text) continue;
        for (auto it = std::sregex_iterator(text->begin(), text->end(), implementation); it != std::sregex_iterator(); ++it) {
            const auto [line, column] = place_of(*text, static_cast<std::size_t>(it->position(1)));
            out.push_back({shown(file), diagnostic(vd::Diagnostic::Level::error, "portable", line, column,
                                                   std::format("`{}` is omui's, not the spec's: a script importing it runs on omui only", (*it)[1].str()),
                                                   "what the spec gives is imported from \"openmui\"")});
        }
        for (auto it = std::sregex_iterator(text->begin(), text->end(), named); it != std::sregex_iterator(); ++it) {
            const std::string list = (*it)[1].str();
            for (auto part : list | std::views::split(',')) {
                std::string imported(part.begin(), part.end());
                const auto first = imported.find_first_not_of(" \t\r\n");
                if (first == std::string::npos) continue;
                imported = imported.substr(first);
                if (imported.starts_with("type ")) continue;
                imported = imported.substr(0, imported.find_first_of(" \t\r\n"));
                if (auto found = omui_builders.find(imported); found != omui_builders.end()) {
                    const auto offset = static_cast<std::size_t>(it->position(1)) + list.find(imported);
                    const auto [line, column] = place_of(*text, offset);
                    out.push_back({shown(file), diagnostic(vd::Diagnostic::Level::error, "portable", line, column,
                                                           std::format("`{}` builds `{}`, a component of omui's, not the spec's registry", imported, found->second),
                                                           "on another openmui runtime, a component of the spec's, or this one's package with it")});
                }
            }
        }
    }
    return out;
}

std::string args_text(const Value* map) {
    std::string out;
    if (map && map->map())
        for (const auto& [name, type] : *map->map()) out += std::format("{}{}: {}", out.empty() ? "" : ", ", lang::ts::camel_case(name), type.as_string());
    return out;
}

// The API as llms.txt: a model's context, a line an item.
std::string llms_text(const Value& api, std::string_view project) {
    std::string out = std::format("# {}\n\n> What writing for it takes, from what it registers (omui describe --api). Scripts are TypeScript: "
                                  "`import {{ … }} from \"openmui\"`; the full declarations are .omui/types/openmui.d.ts.\n\n## Components\n\n",
                                  project);
    if (const Value* components = api.get("components"); components && components->map())
        for (const auto& [name, component] : *components->map()) {
            const Value* doc = component.get("doc");
            const Value* in_spec = component.get("spec");
            out += std::format("- `{}(props, children?)` — {}{}", lang::ts::camel_case(name), doc ? std::string(doc->as_string()) : std::string("(no doc)"),
                               in_spec && in_spec->as_bool() ? " (the spec's)" : " (omui's)");
            std::string props;
            if (const Value* list = component.get("props"); list && list->map())
                for (const auto& [prop, shape] : *list->map()) {
                    std::string type = shape.get("type") ? std::string(shape.get("type")->as_string()) : std::string("any");
                    if (const Value* values = shape.get("values"); values && values->list()) {
                        type.clear();
                        for (const Value& v : *values->list()) type += (type.empty() ? "" : " | ") + std::format("\"{}\"", v.as_string());
                    }
                    const Value* bindable = shape.get("bindable");
                    props += std::format("{}{}: {}{}", props.empty() ? "" : "; ", lang::ts::camel_case(prop), type, bindable && bindable->as_bool() ? " (two-way)" : "");
                }
            if (!props.empty()) out += std::format(". Properties: {}", props);
            std::string events;
            if (const Value* list = component.get("events"); list && list->map())
                for (const auto& [event, shape] : *list->map())
                    events += std::format("{}on{}({})", events.empty() ? "" : ", ", lang::ts::pascal_case(event), args_text(shape.get("args")));
            if (!events.empty()) out += std::format(". Events: {}", events);
            out += ".\n";
        }
    // What an agent may do with a running application: the protocol's operations, then Live Design's.
    if (const Value* operations = api.get("operations"); operations && operations->map()) {
        out += "\n## Operations\n\n";
        for (const auto& [kind, list] : *operations->map()) {
            if (!list.list()) continue;
            for (const Value& operation : *list.list())
                out += std::format("- `{}` ({}) — {}\n", operation.get("name")->as_string(), operation.get("effect")->as_string(), operation.get("summary")->as_string());
        }
    }
    out += "\n## Native modules\n\n";
    if (const Value* natives = api.get("natives"); natives && natives->map())
        for (const auto& [name, module] : *natives->map()) {
            out += std::format("- `openmui:native/{}` — {}\n", name, module.get("doc") ? std::string(module.get("doc")->as_string()) : std::string());
            if (const Value* functions = module.get("functions"); functions && functions->map())
                for (const auto& [function, shape] : *functions->map()) {
                    std::string params;
                    if (const Value* list = shape.get("params"); list && list->list())
                        for (const Value& param : *list->list())
                            params += std::format("{}{}: {}", params.empty() ? "" : ", ", lang::ts::camel_case(param.get("name")->as_string()), param.get("type")->as_string());
                    const Value* returns = shape.get("returns");
                    const Value* async = shape.get("async");
                    out += std::format("  - `{}({})` → {}{} — {}\n", lang::ts::camel_case(function), params, returns ? std::string(returns->as_string()) : std::string("nothing"),
                                       async && async->as_bool() ? " (a promise; also …Sync)" : "", shape.get("doc") ? std::string(shape.get("doc")->as_string()) : std::string());
                }
        }
    out += "\n## Theme tokens\n\n";
    if (const Value* tokens = api.get("tokens")) {
        for (const std::string_view kind : {"colors", "spacing", "radius"})
            if (const Value* table = tokens->get(kind); table && table->map()) {
                std::string names;
                for (const auto& entry : *table->map()) names += (names.empty() ? "" : ", ") + entry.first;
                out += std::format("- {}: {}\n", kind, names);
            }
        for (const std::string_view kind : {"text", "shadows", "motions"})
            if (const Value* list = tokens->get(kind); list && list->list()) {
                std::string names;
                for (const Value& entry : *list->list()) names += std::format("{}{}", names.empty() ? "" : ", ", entry.as_string());
                out += std::format("- {}: {}\n", kind == "text" ? std::string_view("text styles") : kind, names);
            }
    }
    out += "\n## Verbs\n\n";
    if (const Value* verbs = api.get("verbs"); verbs && verbs->list())
        for (const Value& verb : *verbs->list())
            out += std::format("- `{}` ({}) — {}{}\n", verb.get("name")->as_string(), verb.get("effect")->as_string(), verb.get("summary")->as_string(),
                               args_text(verb.get("args")).empty() ? std::string() : std::format("; takes {}", args_text(verb.get("args"))));
    return out;
}

}  // namespace

int check_project(bool json, bool portable, std::ostream& out, std::ostream& err) {
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
    std::vector<fs::path> scripts;  // the files that are not data: the application runs them
    for (const fs::path& file : typescript) {
        const std::string name = utf8(file.filename());
        if (!name.ends_with(".omui.ts") && name != "omui.config.ts") scripts.push_back(file);
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

    // 3. The types, against what the application registers; 4. what is not portable.
    if (!scripts.empty() && manifest->contains("omui.entry.player")) {
        const auto dir = declare_types(root);
        if (!dir) {
            found.push_back(dir.error());
        } else if (const auto checked = run_quietly("tsc", {"-p", utf8(*dir / "tsconfig.json"), "--pretty", "false"}, root); !checked) {
            found.push_back({"mcpp.toml", diagnostic(vd::Diagnostic::Level::warning, "tsc-missing", 0, 0, "tsc is not installed: the types were not checked",
                                                     "xlings install xim:typescript (TypeScript 7, no node), or name it in mcpp.toml")});
        } else if (auto reported = read_tsc(checked->out); reported.empty() && checked->status.shell_code() != 0) {
            // It failed and said nothing of the sources: what it did say (a launcher's warnings go there too).
            found.push_back({".omui/types/tsconfig.json", diagnostic(vd::Diagnostic::Level::error, "tsc", 0, 0,
                                                                     std::format("tsc failed ({}): {}", checked->status.shell_code(), checked->err))});
        } else {
            std::ranges::move(reported, std::back_inserter(found));
        }
        if (dir && portable) {
            std::optional<Value> api;
            if (auto text = mcpplibs::os::fs::read(*dir / "api.json"))
                if (auto read = parse_json(*text)) api = std::move(*read);
            if (!api) found.push_back({".omui/types/api.json", diagnostic(vd::Diagnostic::Level::error, "portable", 0, 0, "the application wrote no API to check against")});
            else std::ranges::move(unportable(root, scripts, *api), std::back_inserter(found));
        }
    }
    return report(found, typescript.size() + documents.size(), json, out, err);
}

int describe(const mcpplibs::cmdline::ParsedArgs& p, std::ostream& out, std::ostream& err) {
    const fs::path root = fs::current_path();
    auto manifest = mcpplibs::os::fs::read(root / "mcpp.toml");
    if (!manifest || !manifest->contains("omui.entry.player")) {
        err << "omui describe: run it in an application the player runs (its mcpp.toml names omui.entry.player)\n";
        return 2;
    }
    const auto dir = declare_types(root);
    if (!dir) {
        err << dir.error().diagnostic.text(dir.error().file) << '\n';
        return 1;
    }
    auto text = mcpplibs::os::fs::read(*dir / "api.json");
    std::optional<Value> api;
    if (text)
        if (auto read = parse_json(*text)) api = std::move(*read);
    if (!api) {
        err << "omui describe: the application wrote no api.json\n";
        return 1;
    }
    const std::string llms = llms_text(*api, utf8(root.filename()));
    if (auto file = p.value("llms")) {
        if (auto written = mcpplibs::os::fs::write_atomic(mcpplibs::os::fs::from_utf8(*file), llms); !written) {
            err << std::format("omui describe: cannot write {}: {}\n", *file, written.error().message());
            return 1;
        }
    }
    if (p.is_flag_set("json")) out << *text << '\n';
    else if (!p.value("llms")) out << llms;
    return 0;
}

}  // namespace omui::cli
