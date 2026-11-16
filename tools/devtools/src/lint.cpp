// The rules over the sources' text (the first group moved from tools/lint/lint.py):
// macros, headers, platform, thirdparty, legacy-import, testing, cmdline, naming,
// exceptions, paths, determinism and owner.
module omui.devtools.lint;

import std;
import nlohmann.json;
import omui.devtools.lead;
import :tree;

namespace omui::devtools::lint {

namespace fs = std::filesystem;
namespace lead = omui::devtools::lead;
using json = nlohmann::json;

namespace {

constexpr std::array source_suffixes{".cppm", ".cpp", ".ixx", ".cc", ".cxx", ".h", ".hpp", ".hh", ".hxx", ".inl"};
constexpr std::array header_suffixes{".h", ".hpp", ".hh", ".hxx", ".inl"};
constexpr std::array skip_dirs{"target", ".git", ".cache", "build", "node_modules", "fixtures", ".venv", "__pycache__"};
constexpr std::string_view the_header = "spec/include/openmui.h";
// Linux's process directory, spelled so this file does not name it in a literal (the rule reads literals too).
constexpr char proc_chars[]{'/', 'p', 'r', 'o', 'c', '/'};
constexpr std::string_view proc_dir{proc_chars, sizeof proc_chars};

// Members whose results are part of A-level conformance (identical bits): no libm transcendental.
constexpr std::string_view deterministic[]{
    "modules/base/math",         "modules/base/utils",           "modules/projects/omui/native",   "modules/projects/xengine/kernel/audio",
    "modules/projects/xengine/kernel/video", "modules/projects/xengine/kernel/dynamics", "modules/projects/xengine/kernel/particles",
    "modules/projects/xengine/kernel/field", "showcase/widget-breakout/headless", "showcase/glade/headless", "modules/projects/hanim/core",
    "modules/projects/hanim",    "modules/projects/xrender/raster", "modules/content/layout",       "modules/base/geom",
    "modules/projects/xrender/graph", "modules/projects/xrender/vector", "modules/projects/xrender/text", "modules/content/text",
    "modules/content/i18n",      "modules/projects/omui/view",    "showcase/promo-video"};

// The forwarders M1 removed (D-106): code names the owner.
constexpr std::array legacy_modules{
    "openmui.raster", "openmui.simulation.cellular.rhi.cts", "openmui.rhi.cts", "openmui.memory", "openmui.geom", "openmui.unicode",
    "openmui.rhi.webgl", "openmui.image", "openmui.testing", "openmui.render.vector", "openmui.pipeline", "openmui.rhi",
    "openmui.simulation.cellular", "openmui.trace", "openmui.shader", "openmui.render", "openmui.shader.rules", "openmui.rhi.sdl",
    "openmui.rhi.webgpu", "openmui.log", "openmui.markdown", "openmui.i18n.cldr", "openmui.layout", "openmui.session", "openmui.text",
    "openmui.math", "openmui.hash", "openmui.render.effect", "openmui.i18n", "openmui.simulation.cellular.rhi", "openmui.simulation"};

constexpr Rule all_rules[]{
    {"macros", "no #define in our sources (spec/include/openmui.h is the one exception)"},
    {"headers", "no header files but openmui.h; #include only in native/ units"},
    {"platform", "no operating-system header, environment or process access: through mcpplibs.os"},
    {"thirdparty", "a wrapped C library (mcpplibs.mwrap.*) only in a native/ unit (D-83)"},
    {"legacy-import", "no removed forwarder, Lua or Python binding, or omui:: alias of another owner (D-106, D-138)"},
    {"testing", "mlibs.testing only in test programs (tests/)"},
    {"cmdline", "a program's command line declared with mcpplibs.cmdline, never scanned by hand (D-56)"},
    {"naming", "no bare `mui`: the owner's name (openmui, omui, a product, mlibs, xengine)"},
    {"exceptions", "no throw / try in runtime and module sources; a native/ unit may catch a library's"},
    {"paths", "no path::string() / generic_string() outside tests: utf8() / generic_utf8()"},
    {"determinism", "deterministic members call no libm transcendental"},
    {"owner", "each capability has one owner (tools/lint/capabilities.owners.omui.json)"},
    {"toml", "every manifest is TOML (a child dependency's key beside its parent is quoted)"},
    {"members", "every listed member has an mcpp.toml, and every mcpp.toml is listed"},
    {"manifest", "namespaced dependencies are dotted keys of [dependencies], never tables of their own"},
    {"layering", "a member depends only on the layers below it (D-82)"},
    {"modules", "an independent component (mlibs, the kernels) depends on nothing above it (D-55)"},
    {"project-dag", "no project depends on one not below it, its tests included (D-161)"},
    {"layout", "a library's directory is its name; a directory with packages lists them in its README (D-80)"},
    {"licenses", "every index package is in tools/lint/licenses.toml; no GPL or AGPL; LGPL only dynamic (D-83)"},
    {"platforms", "a package on the Web depends only on packages that say they are (D-84)"},
    {"pins", "a root's graph-wide pin is declared, the same, by every member and template that reaches it"},
    {"closure", "the dependency closures the lint names stay small (D-55)"},
    {"imports", "a source imports only modules of packages its manifest reaches (tests: dev-dependencies too)"},
    {"module-closure", "a library's interface re-exports another package only where its manifest lists it (reexports)"},
    {"documents", "every view document, scenario and trace is valid against its committed schema (spec/schema)"},
    {"media", "a picture is under 1 MB; every showcase's README shows its own picture from media/"},
    {"ops", "a package that declares operations has their catalogs (<product>.ops.omui.ts) and a skill.omui.md"},
    {"skills", "every operation a skill names is in a catalog; a skill points at its own package's catalog (D-145)"},
    {"agent-surface", "every package of a project says how an agent reaches what it holds (D-174)"},
    {"typescript", "no *.omui.toml data file: data are TypeScript modules or their JSON snapshots (D-139)"},
    {"matter", "no law of a matter definition and no kernel source names a material (D-165)"},
    {"shader-inputs", "a vertex struct's TEXCOORD places run from 0 in order"},
    {"status", "STATUS: at most 200 lines of at most 600 characters; a task done names its evidence (D-202)"},
    {"maturity", "every package's label; a library depends by default only at its label or above (D-189, D-199)"},
    {"lead", "the signature features against the lead ledger; the lead pages are the ledger's (D-135)"},
    {"tooling", "a library or engine imports no toolkit (a member of kind \"tool\"); tools sit on top (plan §4.5)"},
    {"splittable", "a project reaches nothing of the repository's tools/ by a path: it can leave as a repository (plan §4.5)"},
    {"scripts", "no script but the allowlist's kinds, each with its reason (tools/lint/scripts.allow.omui.json, D-206)"},
    {"claims", "a public document's ratio against another project is a claim of docs/claims.omui.json, with evidence (D-193)"},
    {"platform-table", "every cell of docs/platforms.omui.json is real, software, unverified or unsupported, with its evidence or need (D-201)"},
};

bool starts_with_any(std::string_view text, std::span<const char* const> prefixes) {
    return std::ranges::any_of(prefixes, [&](const char* prefix) { return text.starts_with(prefix); });
}

// `re.match`: the pattern at the start of the text.
bool match_start(std::string_view text, const std::regex& pattern) {
    return std::regex_search(text.begin(), text.end(), pattern, std::regex_constants::match_continuous);
}

bool search(std::string_view text, const std::regex& pattern) { return std::regex_search(text.begin(), text.end(), pattern); }

std::vector<std::string_view> lines_of(std::string_view text) {
    std::vector<std::string_view> out;
    std::size_t at = 0;
    while (at <= text.size()) {
        const auto end = text.find('\n', at);
        if (end == std::string_view::npos) {
            if (at < text.size()) out.push_back(text.substr(at));
            break;
        }
        out.push_back(text.substr(at, end - at));
        at = end + 1;
    }
    return out;
}

std::string_view trimmed(std::string_view line) {
    const auto first = line.find_first_not_of(" \t\r");
    if (first == std::string_view::npos) return {};
    const auto last = line.find_last_not_of(" \t\r");
    return line.substr(first, last - first + 1);
}

// The sources under the root, as paths from it ("/" separated), in order.
std::vector<std::string> source_files(const fs::path& root) {
    std::vector<std::string> out;
    std::error_code error;
    for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, error);
         it != fs::recursive_directory_iterator(); it.increment(error)) {
        if (error) break;
        const auto name = it->path().filename().generic_u8string();
        if (it->is_directory(error)) {
            if (std::ranges::any_of(skip_dirs, [&](std::string_view skip) { return std::u8string_view(name) == std::u8string_view(
                                                                                         reinterpret_cast<const char8_t*>(skip.data()), skip.size()); }))
                it.disable_recursion_pending();
            continue;
        }
        const auto extension = it->path().extension().generic_u8string();
        const std::string_view suffix(reinterpret_cast<const char*>(extension.data()), extension.size());
        if (!std::ranges::contains(source_suffixes, suffix)) continue;
        const auto relative = fs::relative(it->path(), root, error).generic_u8string();
        out.emplace_back(reinterpret_cast<const char*>(relative.data()), relative.size());
    }
    std::ranges::sort(out);
    return out;
}

std::vector<std::string_view> parts_of(std::string_view path) {
    std::vector<std::string_view> out;
    for (auto part : std::views::split(path, '/')) out.emplace_back(part.begin(), part.end());
    return out;
}

// One capability's owner and the signature of a re-implementation.
struct Capability {
    std::string name, owner, use, marker;
    std::vector<std::regex> expressions;  // one, or several that must appear together within `window` lines
    std::size_t window = 1;
    std::vector<std::string> mine, within, skip;
    std::string sample_path, sample_text;
};

std::vector<Capability> owners_of(const fs::path& file) {
    std::vector<Capability> out;
    const auto text = read_text(file);
    if (!text) return out;
    const auto data = json::parse(*text, nullptr, false);
    if (data.is_discarded() || !data.contains("capability")) return out;
    for (const auto& row : data["capability"]) {
        Capability capability{.name = row.value("name", ""), .owner = row.value("owner", ""), .use = row.value("use", ""),
                              .marker = row.value("marker", "")};
        const auto flags = row.value("ignore_case", false) ? std::regex::ECMAScript | std::regex::icase : std::regex::ECMAScript;
        if (row.contains("pattern")) capability.expressions.emplace_back(row["pattern"].get<std::string>(), flags);
        else
            for (const auto& one : row["together"]) capability.expressions.emplace_back(one.get<std::string>(), flags);
        capability.window = row.value("window", std::size_t{1});
        for (const auto& path : row.value("paths", json::array())) capability.mine.push_back(path.get<std::string>());
        for (const auto& allow : row.value("allow", json::array())) capability.mine.push_back(allow["path"].get<std::string>());
        for (const auto& path : row.value("within", json::array())) capability.within.push_back(path.get<std::string>());
        for (const auto& part : row.value("skip", json::array())) capability.skip.push_back(part.get<std::string>());
        if (row.contains("sample")) {
            capability.sample_path = row["sample"].value("path", "");
            capability.sample_text = row["sample"].value("text", "");
        }
        out.push_back(std::move(capability));
    }
    return out;
}

struct Patterns {
    std::regex define{R"(#\s*define\b)"};
    std::regex include{R"(#\s*include\b)"};
    std::regex environment{R"(\bstd::(getenv|system|_Exit)\s*\(|\bgetenv\s*\()"};
    std::regex os_header{R"(#\s*include\s*[<"](windows\.h|winsock2?\.h|unistd\.h|pthread\.h|dlfcn\.h|fcntl\.h|sys/[^>"]+|mach/[^>"]+|)"
                         R"(CoreFoundation/[^>"]+|Cocoa/[^>"]+|AppKit/[^>"]+|linux/[^>"]+)[>"])"};
    std::regex mwrap{R"((export\s+)?import\s+mcpplibs\.mwrap\.)"};
    std::regex import_of{R"((export\s+)?import\s+([\w.]+)\s*;)"};
    std::regex openmui_module{R"((export\s+)?(?:module|import)\s+(openmui(?:[.:][\w.:]*)?)\s*;)"};
    std::regex removed_binding{R"(^omui\.(lang\.lua|lang\.python|entry\.python)(\.|:|$))"};
    // `(?<![\w])` in the script: std::regex has no look-behind, so the character before is matched.
    std::regex legacy_alias{R"((^|[^\w])omui::(cellular|effect|geom|hash|i18n|imageio|layout|math|md|pipeline|raster|render|rhi|session|shader|testing|trace|typeset|unicode)::)"};
    std::regex testing_import{R"((export\s+)?import\s+(mlibs|openmui)\.testing\b)"};
    std::regex hand_parsed{R"(==\s*"--[A-Za-z]|starts_with\("--[A-Za-z]|\bargv\[[a-z_]\w*\])"};
    std::regex bare_mui{R"(\bmui(::|_[a-z])|\bMUI_|\bnamespace\s+mui\b|\bmodule\s+mui\b|import\s+mui\b)"};
    std::regex thrown{R"(\bthrow\b)"};
    std::regex thrown_or_caught{R"(\bthrow\b|\btry\s*\{|\bcatch\s*\()"};
    std::regex narrowed{R"(\.(generic_)?string\(\))"};
    std::regex transcendental{
        R"(\bstd::(exp|exp2|expm1|log|log2|log10|log1p|pow|sin|cos|tan|asin|acos|atan|atan2|sinh|cosh|tanh|cbrt|hypot|lerp)\s*\()"};
};

void check_source(std::string_view rel, std::string_view text, const Patterns& re, std::span<const Capability> owners,
                  std::vector<Problem>& problems) {
    const std::string code = strip_comments(text);
    const auto lines = lines_of(code);
    const auto raw = lines_of(text);
    const auto parts = parts_of(rel);
    const auto has_part = [&](std::string_view part) { return std::ranges::contains(parts, part); };
    const bool is_the_header = rel == the_header;
    const bool in_native = has_part("native");
    const bool in_tests = has_part("tests");
    const auto at = [&](std::size_t no) { return std::format("{}:{}", rel, no); };
    const auto add = [&](std::string_view rule, std::string where, std::string message) {
        problems.push_back({std::string(rule), std::move(where), std::move(message)});
    };

    const auto dot = rel.rfind('.');
    if (dot != std::string_view::npos && std::ranges::contains(header_suffixes, rel.substr(dot)) && !is_the_header)
        add("headers", std::string(rel), "a header file; openmui has one header (openmui.h), the rest are modules");

    for (std::size_t index = 0; index < lines.size(); ++index) {
        const std::size_t no = index + 1;
        const std::string_view line = lines[index];
        const std::string_view stripped = trimmed(line);
        const std::string_view original = index < raw.size() ? raw[index] : std::string_view{};
        if (stripped.starts_with('#')) {
            if (match_start(stripped, re.define) && !is_the_header) add("macros", at(no), "#define; use constexpr, consteval or a module partition");
            if (match_start(stripped, re.os_header)) add("platform", at(no), "an operating-system header; use mcpplibs.os");
            if (match_start(stripped, re.include) && !in_native && !is_the_header)
                add("headers", at(no), "#include outside a native/ unit; import a module instead");
        }
        if ((line.contains("getenv") || line.contains("system") || line.contains("_Exit")) && search(line, re.environment))
            add("platform", at(no), "environment and process access go through mcpplibs.os (env, process)");
        else if (original.contains(proc_dir))
            add("platform", at(no), "environment and process access go through mcpplibs.os (env, process)");
        if (stripped.contains("import") || stripped.contains("module")) {
            if (match_start(stripped, re.mwrap) && !in_native && !in_tests)
                add("thirdparty", at(no), "a wrapped C library (mcpplibs.mwrap.*) is used only in a native/ unit (D-83)");
            std::match_results<std::string_view::const_iterator> found;
            if (std::regex_search(stripped.begin(), stripped.end(), found, re.openmui_module, std::regex_constants::match_continuous)) {
                const std::string name = found[2].str();
                if (!name.starts_with("openmui.spec") && !rel.starts_with("spec/"))
                    add("legacy-import", at(no), std::format("`{}`: omui's modules are omui.* (D-107); openmui is the specification's", name));
            }
            if (std::regex_search(stripped.begin(), stripped.end(), found, re.import_of, std::regex_constants::match_continuous)) {
                const std::string name = found[2].str();
                if (std::ranges::contains(legacy_modules, std::string_view(name)))
                    add("legacy-import", at(no), std::format("`{}` was a forwarder (D-106): import its owner", name));
                if (search(name, re.removed_binding))
                    add("legacy-import", at(no), std::format("`{}`: the Lua and Python bindings are gone (D-138); write it in TypeScript", name));
            }
            if (match_start(stripped, re.testing_import) && !in_tests && !rel.starts_with("modules/base/utils/src/testing/"))
                add("testing", at(no), "mlibs.testing is for test programs (tests/): no artifact carries it");
        }
        if (line.contains("omui::") && search(line, re.legacy_alias))
            add("legacy-import", at(no), "an omui:: alias of another owner's namespace (D-106): name the owner");
        // Test programs (a package's tests/) are mtest's; the benchmark runners under tests/bench are programs.
        if ((line.contains("--") || line.contains("argv[")) && search(line, re.hand_parsed) && (!in_tests || rel.starts_with("tests/bench/")))
            add("cmdline", at(no),
                "a command line scanned by hand: declare its options with mcpplibs.cmdline (a program run by omui::run adds them beside "
                "RunOptions::declare) (D-56)");
        if ((line.contains("mui") || line.contains("MUI_")) && search(line, re.bare_mui))
            add("naming", at(no), "bare `mui`; use the owner: openmui (spec), omui (UI), a named product, mlibs or xengine");
    }

    const std::string_view top = parts.empty() ? std::string_view{} : parts.front();
    // A project's toolkit (modules/projects/<project>/tools/, plan §4.5) is tool code, as the repository's tools/ is.
    const bool in_toolkit = rel.starts_with("modules/projects/") && rel.contains("/tools/");
    if ((top == "modules" || top == "mgame") && !in_tests && !in_toolkit) {
        // A native/ unit adapts a third-party library: it may catch what the library throws, to answer std::expected.
        const std::regex& pattern = in_native ? re.thrown : re.thrown_or_caught;
        for (std::size_t index = 0; index < lines.size(); ++index)
            if ((lines[index].contains("throw") || lines[index].contains("try") || lines[index].contains("catch")) && search(lines[index], pattern))
                add("exceptions", at(index + 1), "exceptions in core code; return std::expected");
    }

    if (!rel.contains("/tests/") && !rel.starts_with("tests/"))
        for (std::size_t index = 0; index < lines.size(); ++index)
            if (lines[index].contains("string()") && search(lines[index], re.narrowed) && !(index < raw.size() && raw[index].contains("NARROW-OK:")))
                add("paths", at(index + 1), "path::string() narrows through the code page on Windows; use utf8() / generic_utf8()");

    for (const auto& capability : owners) {
        const auto under = [&](std::span<const std::string> prefixes) {
            return std::ranges::any_of(prefixes, [&](const std::string& prefix) { return rel.starts_with(prefix); });
        };
        if (under(capability.mine) || (!capability.within.empty() && !under(capability.within))) continue;
        if (std::ranges::any_of(capability.skip, [&](const std::string& part) { return has_part(part); })) continue;
        if (!search(code, capability.expressions.front())) continue;  // one search a file; the lines only where it is
        for (std::size_t index = 0; index < lines.size(); ++index) {
            if (!capability.marker.empty() && index < raw.size() && raw[index].contains(capability.marker)) continue;
            bool hit = search(lines[index], capability.expressions.front());
            if (hit && capability.expressions.size() > 1) {
                const std::size_t end = std::min(lines.size(), index + capability.window);
                for (std::size_t other = 1; other < capability.expressions.size() && hit; ++other) {
                    bool any = false;
                    for (std::size_t near = index; near < end && !any; ++near) any = search(lines[near], capability.expressions[other]);
                    hit = any;
                }
            }
            if (hit) add("owner", at(index + 1), std::format("{}: {} has it; use {}", capability.name, capability.owner, capability.use));
        }
    }

    for (std::string_view member : deterministic_members()) {
        if (!rel.starts_with(std::string(member) + "/src/")) continue;
        for (std::size_t index = 0; index < lines.size(); ++index) {
            std::match_results<std::string_view::const_iterator> found;
            if (lines[index].contains("std::") && std::regex_search(lines[index].begin(), lines[index].end(), found, re.transcendental))
                add("determinism", at(index + 1),
                    std::format("std::{} differs between platforms' libm; use mlibs.math's det (hanim::dm)", found[1].str()));
        }
    }
}

std::vector<Problem> check_sources(const fs::path& root, std::span<const Capability> owners) {
    static const Patterns patterns;
    const auto files = source_files(root);
    // The files are independent: shared out among the machine's threads, each its own list, joined in the files' order.
    const std::size_t workers = std::clamp<std::size_t>(std::thread::hardware_concurrency(), 1, 32);
    std::vector<std::vector<Problem>> found(files.size());
    std::atomic<std::size_t> next{0};
    {
        std::vector<std::jthread> threads;
        for (std::size_t worker = 0; worker < workers; ++worker)
            threads.emplace_back([&] {
                for (std::size_t index = next++; index < files.size(); index = next++) {
                    const auto& rel = files[index];
                    const auto text = read_text(root / path_of(rel));
                    if (text) check_source(rel, *text, patterns, owners, found[index]);
                }
            });
    }
    std::vector<Problem> problems;
    for (auto& one : found) std::ranges::move(one, std::back_inserter(problems));
    return problems;
}

fs::path owners_file(const fs::path& root) { return root / "tools" / "lint" / "capabilities.owners.omui.json"; }

}  // namespace

std::span<const Rule> rules() noexcept { return all_rules; }

std::span<const std::string_view> deterministic_members() noexcept { return deterministic; }

std::string strip_comments(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    const std::size_t size = text.size();
    std::size_t at = 0;
    const auto newlines = [&](std::size_t from, std::size_t to) {
        for (std::size_t index = from; index < to; ++index)
            if (text[index] == '\n') out.push_back('\n');
    };
    while (at < size) {
        const char c = text[at];
        if (text.substr(at).starts_with("//")) {
            const auto end = text.find('\n', at);
            at = end == std::string_view::npos ? size : end;
        } else if (text.substr(at).starts_with("/*")) {
            auto end = text.find("*/", at + 2);
            end = end == std::string_view::npos ? size : end + 2;
            newlines(at, end);
            at = end;
        } else if (c == 'R' && text.substr(at).starts_with("R\"") &&
                   (at == 0 || !(std::isalnum(static_cast<unsigned char>(text[at - 1])) || text[at - 1] == '_'))) {
            // A raw string literal: R"delim( … )delim"
            const auto open = text.find('(', at + 2);
            std::size_t end = size;
            if (open != std::string_view::npos) {
                const std::string closing = std::format("){}\"", text.substr(at + 2, open - at - 2));
                const auto close = text.find(closing, open + 1);
                end = close == std::string_view::npos ? size : close + closing.size();
            }
            out += "\"\"";
            newlines(at, end);
            at = end;
        } else if (c == '"') {
            std::size_t end = at + 1;
            while (end < size && text[end] != '"') end += text[end] == '\\' ? 2 : 1;
            out += "\"\"";
            at = end + 1;
        } else if (c == '\'' && !(at > 0 && std::isalnum(static_cast<unsigned char>(text[at - 1])))) {
            // A character literal ('"', '\''); after a digit, a digit separator (1'000).
            std::size_t end = at + 1;
            while (end < size && text[end] != '\'' && text[end] != '\n') end += text[end] == '\\' ? 2 : 1;
            out += "''";
            at = end + 1;
        } else {
            out.push_back(c);
            ++at;
        }
    }
    return out;
}

namespace {

std::vector<Problem> run_with(const fs::path& root, std::span<const std::string> only, std::span<const Closure> closures,
                              std::span<const std::pair<std::string, std::string>> owed) {
    const auto owners = owners_of(owners_file(root));
    const auto workspace = Workspace::read(root);
    auto problems = check_manifests(workspace);
    const bool readable = std::ranges::none_of(problems, [](const Problem& problem) { return problem.rule == "toml"; });
    if (readable) {  // the other rules read the manifests
        for (auto&& found : {check_projects(workspace), check_layout(workspace), check_licences(workspace), check_platforms(workspace),
                             check_pins(workspace), check_closures(workspace, closures), check_modules(workspace),
                             check_data(workspace, owed)})
            std::ranges::move(found, std::back_inserter(problems));
    }
    std::ranges::move(check_sources(root, owners), std::back_inserter(problems));
    if (fs::exists(root / ".agents/docs/evaluation/lead.features.omui.json"))
        for (auto& [where, what] : lead::problems(root)) problems.push_back({"lead", std::move(where), std::move(what)});
    if (!only.empty()) std::erase_if(problems, [&](const Problem& problem) { return !std::ranges::contains(only, problem.rule); });
    return problems;
}

}  // namespace

std::vector<Problem> run(const fs::path& root, std::span<const std::string> only) {
    return run_with(root, only, repository_closures(), repository_owed());
}

namespace {

// The small workspace every case plants into (the script's): hanim, its core, a UI core and the specification,
// each with a manifest and the READMEs that list them. A case may add a dependency, rename a package or
// replace a file.
struct Planted {
    std::string rule, path, text, capability;
    std::map<std::string, std::string> dependencies;  // a member's extra manifest tables
    std::map<std::string, std::string> names;         // a member's [package] name lines, instead of its own
    std::vector<std::string> also_unexpected;         // rules a case breaks as well (and may)
    std::string root_prefix;                          // before the root's [workspace]
    std::string licences;                             // more lines for tools/lint/licenses.toml
    std::vector<Closure> closures;                    // the closure rules the case runs with
    std::map<std::string, std::string> files;         // more files the case needs
    std::string label = "stable";                     // the UI core's maturity
    std::string ui_kind;                              // the UI core's kind, when it is planted as one
    bool project = false;                             // a project's package that says nothing of its agent surface
    bool view_schema = false;                         // the repository's view schema, copied in
};

void plant(const fs::path& root, const Planted& planted, const fs::path& owners_table) {
    std::error_code error;
    fs::remove_all(root, error);
    fs::create_directories(root / "tools" / "lint", error);
    if (fs::exists(owners_table)) fs::copy_file(owners_table, root / "tools" / "lint" / "capabilities.owners.omui.json", fs::copy_options::overwrite_existing, error);
    std::vector<std::string> members{"modules/gfx/hanim", "modules/base/hanim/core", "modules/ui/core", "spec"};
    if (planted.project) members.push_back("modules/projects/thing");
    std::map<std::string, std::string> names{{"modules/gfx/hanim", "name = \"hanim\""},
                                             {"modules/base/hanim/core", "namespace = \"hanim\"\nname = \"core\""},
                                             {"modules/ui/core", "namespace = \"openmui\"\nname = \"core\""},
                                             {"spec", "namespace = \"openlifa\"\nname = \"openmui\""},
                                             {"modules/projects/thing", "name = \"thing\""}};
    for (const auto& [member, name] : planted.names) names[member] = name;
    const auto write = [&](const fs::path& file, std::string_view text) {
        fs::create_directories(file.parent_path(), error);
        std::ofstream(file, std::ios::binary) << text;
    };
    std::string listed;
    for (const auto& member : members) listed += std::format("{}\"{}\"", listed.empty() ? "" : ", ", member);
    write(root / "mcpp.toml", std::format("{}[workspace]\nmembers = [{}]\n", planted.root_prefix, listed));
    write(root / "tools" / "lint" / "licenses.toml",
          "[packages]\n\"ns.kernel\" = [\"MIT\"]\n\"ns.os\" = [\"MIT\"]\n\"compat.sdl3\" = [\"Zlib\"]\n" + planted.licences);
    std::map<std::string, std::string> readmes{{"modules", "[base](base) [gfx](gfx) [ui](ui)"},
                                               {"modules/base", "[hanim](hanim)"},
                                               {"modules/base/hanim", "[core](core)"},
                                               {"modules/gfx", "[hanim](hanim)"},
                                               {"modules/ui", "[core](core)"}};
    if (planted.project) {
        readmes["modules"] += " [projects](projects)";
        readmes["modules/projects"] = "[thing](thing)";
    }
    for (const auto& [directory, text] : readmes)
        write(root / path_of(directory) / "README.md", text + "\n");
    for (const auto& member : members) {
        fs::create_directories(root / path_of(member) / "src", error);
        const bool hanim = member == "modules/gfx/hanim" || member == "modules/base/hanim/core";
        const auto extra = planted.dependencies.find(member);
        write(root / path_of(member) / "mcpp.toml",
              std::format("[package]\n{}\nversion = \"0.1.0\"\n{}{}\n[package.metadata.openmui]\nmaturity = \"{}\"\n{}", names[member],
                          hanim ? "[build]\ncxxflags = [\"-ffp-contract=off\"]\n" : "", extra == planted.dependencies.end() ? "" : extra->second,
                          member == "modules/ui/core" ? planted.label : std::string("stable"),
                          member == "modules/ui/core" && !planted.ui_kind.empty() ? std::format("kind = \"{}\"\n", planted.ui_kind) : std::string()));
    }
    for (const auto& [path, text] : planted.files) write(root / path_of(path), text);
    if (planted.view_schema) {
        fs::create_directories(root / "spec" / "schema", error);
        fs::copy_file(owners_table.parent_path().parent_path().parent_path() / "spec" / "schema" / "view.schema.json", root / "spec" / "schema" / "view.schema.json",
                      fs::copy_options::overwrite_existing, error);
    }
    if (!planted.path.empty()) write(root / path_of(planted.path), planted.text);
}

}  // namespace

int self_test(const fs::path& repository, const fs::path& scratch, std::ostream& out) {
    std::vector<Planted> cases{
        {"macros", "modules/base/hanim/core/src/bad.cpp", "module x;\n#define BAD 1\n"},
        {"headers", "modules/ui/core/src/util.h", "int f();\n"},
        {"naming", "modules/ui/core/src/a.cpp", "namespace mui { }\n"},
        {"exceptions", "modules/base/hanim/core/src/e.cpp", "void f() { throw 1; }\n"},
        {"paths", "modules/ui/core/src/p.cpp", "std::string f(const std::filesystem::path& p) { return p.string(); }\n"},
        {"determinism", "modules/base/math/src/d.cpp", "double f(double x) { return std::sin(x); }\n"},
        {"platform", "modules/ui/core/src/native/io.cpp", "module;\n#include <unistd.h>\nmodule x;\n"},
        {"testing", "modules/ui/core/src/t.cpp", "module x;\nimport mlibs.testing;\n"},
        {"cmdline", "tools/x/src/main.cpp",
         "int main(int argc, char** argv) {\n    for (int i = 1; i < argc; ++i) if (std::string_view(argv[i]) == \"--json\") return 1;\n}\n"},
        {"legacy-import", "modules/ui/core/src/l.cpp", "module x;\nimport openmui.geom;\n"},
        {"thirdparty", "modules/base/hanim/core/src/w.cpp", "module x;\nimport mcpplibs.mwrap.md4c;\n"},
        // the manifests'
        {"toml", "modules/ui/core/mcpp.toml", "[package]\nnamespace = \"omui\"\nname = \"core\"\n[dependencies]\na.b = \"1\"\na.b.c = \"2\"\n"},
        {"members", "modules/stray/mcpp.toml", "[package]\nname = \"stray\"\nversion = \"0.1.0\"\n"},
        {.rule = "manifest", .dependencies = {{"modules/ui/core", "[dependencies.compat]\nsdl3 = \"3.4.18\"\n"}}},
        // a specification that depends on a library breaks the projects' order too
        {.rule = "layering", .dependencies = {{"spec", "[dependencies]\ncore = { path = \"../modules/ui/core\" }\n"}}, .also_unexpected = {"project-dag"}},
        {.rule = "modules", .dependencies = {{"modules/gfx/hanim", "[dependencies]\ncore = { path = \"../../ui/core\" }\n"}}},
        {.rule = "project-dag", .dependencies = {{"modules/base/hanim/core", "[dev-dependencies]\nopenlifa.openmui = { path = \"../../../../spec\" }\n"}}},
        {.rule = "layout", .names = {{"modules/gfx/hanim", "name = \"anim\""}}},
        // what the manifests reach
        {.rule = "licenses", .dependencies = {{"modules/ui/core", "[dependencies]\nacme.copyleft = \"1.0.0\"\n"}},
         .licences = "\"acme.copyleft\" = [\"GPL-3.0-only\"]\n"},
        {.rule = "platforms", .dependencies = {{"modules/ui/core", "[dependencies]\nhanim = { path = \"../../gfx/hanim\" }\n"}},
         .names = {{"modules/ui/core", "namespace = \"openmui\"\nname = \"core\"\nplatforms = [\"linux\", \"emscripten\"]"}}},
        {.rule = "pins", .dependencies = {{"modules/ui/core", "[dependencies]\nns.os = \"0.1.0\"\n"}},  // reaches ns.os, does not pin ns.kernel
         .root_prefix = "[package]\nname = \"root\"\nversion = \"0.1.0\"\n[package.metadata.openmui.pins]\n\"ns.kernel\" = \"ns.os\"\n[dependencies]\nns.kernel = \"1.0.0\"\n"},
        {.rule = "closure", .dependencies = {{"modules/gfx/hanim", "[dependencies]\nhanim.core = { path = \"../../base/hanim/core\" }\n"}},
         .closures = {{"hanim", {}, {"hanim.core"}}}},  // planted: hanim may not reach its core
        // what the sources import: a module of a package the manifest does not reach; a re-export not listed
        {.rule = "imports", .path = "modules/ui/core/tests/i_test.cpp", .text = "import std;\nimport hanim.core;\n",
         .files = {{"modules/base/hanim/core/src/core.cppm", "export module hanim.core;\n"}}},
        {.rule = "module-closure", .path = "modules/gfx/hanim/src/hanim.cppm", .text = "export module hanim;\nexport import hanim.core;\n",
         .dependencies = {{"modules/gfx/hanim", "[dependencies]\nhanim.core = { path = \"../../base/hanim/core\" }\n"}},
         .files = {{"modules/base/hanim/core/src/core.cppm", "export module hanim.core;\n"}}},
        // the data and the documents
        {"media", "docs/huge.png", std::string(1024 * 1024 + 1, 'x')},
        {.rule = "agent-surface", .project = true},
        {"typescript", "modules/ui/core/contract/x.contract.omui.toml", "[contract.x]\nrole = \"button\"\n"},
        {"ops", "modules/ui/core/src/o.cpp", "module x;\nconstexpr auto op = mlibs::control::Operation{\"ui.wipe\", openas::Effect::irreversible};\n"},
        {.rule = "documents", .path = "examples/x/ui/bad.view.omui.json", .text = R"({"omui": "0.1", "view": {"root": {"typ": "text"}}})", .view_schema = true},
        {.rule = "skills", .path = "modules/ui/core/skill.omui.md", .text = "# x\n\nSee [x.ops.omui.ts](x.ops.omui.ts); then `ui.wipe`.\n",
         .files = {{"modules/ui/core/x.ops.omui.ts", "export default defineOps({ operations: [{ name: \"ui.snapshot\" }] });\n"}}},
        {"matter", "modules/ui/core/x.matter.omui.json",
         R"({"matter": {"materials": {"wood": {}, "ash": {}}, "laws": [{"when": "material == wood && temperature >= 500", "become": "burnt"}]}})"},
        {"shader-inputs", "modules/ui/core/x.slang", "struct VIn {\n    float3 position : TEXCOORD0;\n    float2 second : TEXCOORD4;\n};\n"},
        {.rule = "maturity", .label = "experimental"},
        {"scripts", "tools/x/run.sh", "#!/bin/sh\nmcpp build\n"},
        {"claims", "modules/projects/hanim/README.md", "# hanim\n\nIt plays 3.5× faster than GSAP.\n"},
        {.rule = "tooling", .dependencies = {{"modules/gfx/hanim", "[dependencies]\ncore = { path = \"../../ui/core\" }\n"}}, 
         .also_unexpected = {"layering", "modules", "project-dag"}, .ui_kind = "tool"},
        {"splittable", "modules/projects/hanim/README.md", "# hanim\n\nSee [the probe](../../../tools/ci/probe/README.md).\n"},
        {"platform-table", "docs/platforms.omui.json",
         R"({"platforms": [{"id": "a"}], "capability": [{"id": "x", "label": "stable", "cells": {"a": {"status": "passed"}}}]})"},
        {"status", ".agents/docs/STATUS.md", "# STATUS\n\n| Task | State | Evidence |\n|---|---|---|\n| RC0.1 push | done | pushed |\n"},
        {"lead", ".agents/docs/evaluation/lead.features.omui.json",
         R"({"feature": [{"id": "A", "project": "omui", "line": "use-safely", "title": "x", "status": "claimed"}]})"},
    };
    // The owners' samples come from the repository's own table.
    const fs::path table = owners_file(repository);
    for (const auto& capability : owners_of(table)) cases.push_back({"owner", capability.sample_path, capability.sample_text + "\n", capability.name});

    int failures = 0;
    const fs::path root = scratch / "lint-self-test";
    for (const auto& planted : cases) {
        plant(root, planted, table);
        std::vector<Problem> found, other;
        for (auto& problem : run_with(root, {}, planted.closures, {})) {
            const bool mine = problem.rule == planted.rule && (planted.capability.empty() || problem.message.starts_with(planted.capability + ":"));
            if (mine) found.push_back(std::move(problem));
            else if (!std::ranges::contains(planted.also_unexpected, problem.rule)) other.push_back(std::move(problem));
        }
        const bool ok = !found.empty() && other.empty();
        const std::string name = planted.capability.empty() ? planted.rule : planted.rule + ":" + planted.capability;
        out << std::format("  {}  {}: {}\n", ok ? "ok  " : "FAIL", name, found.empty() ? std::string("not caught") : found.front().text());
        for (const auto& problem : other) out << "        unexpected: " << problem.text() << '\n';
        failures += ok ? 0 : 1;
    }
    std::error_code error;
    fs::remove_all(root, error);
    out << std::format("lint self-test: {} of {} rules catch their violation\n", cases.size() - failures, cases.size());
    return failures;
}

}  // namespace omui::devtools::lint
