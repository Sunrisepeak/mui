module omui.devtools.closure;

import std;
import nlohmann.json;
import mcpplibs.os;
import omui.devtools.docs;
import omui.devtools.lint;

namespace omui::devtools::closure {

namespace {

using json = nlohmann::json;
namespace fs = std::filesystem;

// The projects whose closure is checked, in the order of the layers (mlibs first); a project's directories.
struct Place {
    std::string_view name;
    std::string_view readme;
    std::array<std::string_view, 3> directories;
};
constexpr std::array<Place, 7> places{{
    {"mlibs", "modules/base/README.md", {"modules/base", "modules/gfx", "modules/content"}},
    {"xrender", "modules/projects/xrender/README.md", {"modules/projects/xrender", "", ""}},
    {"hanim", "modules/projects/hanim/README.md", {"modules/projects/hanim", "", ""}},
    {"omui", "modules/projects/omui/README.md", {"modules/projects/omui", "", ""}},
    {"xengine", "modules/projects/xengine/README.md", {"modules/projects/xengine", "", ""}},
    {"gongfu", "modules/projects/gongfu/README.md", {"modules/projects/gongfu", "", ""}},
    {"xtools", "modules/projects/xtools/README.md", {"modules/projects/xtools", "", ""}},
}};

// A bench scene's first segment → the project that owns what it measures.
constexpr std::array<std::pair<std::string_view, std::string_view>, 18> bench_owners{{
    {"agent", "omui"}, {"audio", "xengine"}, {"canvas", "omui"}, {"canvas-kit", "omui"}, {"dynamics", "xengine"},
    {"game", "xengine"}, {"glass", "xrender"}, {"gongfu", "gongfu"}, {"log", "mlibs"}, {"markdown", "omui"},
    {"motion", "hanim"}, {"public-motion", "hanim"}, {"render", "xrender"}, {"simulation", "xengine"},
    {"styles", "omui"}, {"text", "mlibs"}, {"trace", "mlibs"}, {"ui", "omui"},
}};

// The day RC2 began: a side-by-side comparison counts from it (plan §5.1, item 8).
constexpr std::string_view rc2_began = "2026-11-15";

std::string read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

std::vector<std::string_view> lines_of(std::string_view text) {
    std::vector<std::string_view> out;
    for (auto part : std::views::split(text, '\n')) {
        std::string_view line(part.begin(), part.end());
        if (line.ends_with('\r')) line.remove_suffix(1);
        out.push_back(line);
    }
    return out;
}

std::string_view trimmed(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) text.remove_suffix(1);
    return text;
}

std::string lower(std::string_view text) {
    std::string out(text);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// A section's lines: from the heading named `title` (any level) to the next heading as high or higher.
std::optional<std::vector<std::string_view>> section(std::string_view readme, std::string_view title) {
    const auto lines = lines_of(readme);
    for (std::size_t at = 0; at < lines.size(); ++at) {
        const auto line = lines[at];
        const std::size_t level = line.find_first_not_of('#');
        if (level == 0 || level == std::string_view::npos || line.substr(level, 1) != " ") continue;
        if (!lower(trimmed(line.substr(level))).starts_with(lower(title))) continue;
        std::vector<std::string_view> body;
        for (std::size_t next = at + 1; next < lines.size(); ++next) {
            const std::size_t other = lines[next].find_first_not_of('#');
            if (other > 0 && other != std::string_view::npos && other <= level && lines[next].substr(other, 1) == " ") break;
            body.push_back(lines[next]);
        }
        return body;
    }
    return std::nullopt;
}

// A section's list items, a continued line joined to its item.
std::vector<std::string> items_of(const std::vector<std::string_view>& body) {
    std::vector<std::string> items;
    for (const auto line : body) {
        if (line.starts_with("- ")) items.emplace_back(line.substr(2));
        else if (!items.empty() && line.starts_with("  ") && !trimmed(line).empty()) items.back() += " " + std::string(trimmed(line));
    }
    return items;
}

std::vector<std::string> cells_of(std::string_view row) {
    std::vector<std::string> cells;
    row = trimmed(row);
    if (row.starts_with('|')) row.remove_prefix(1);
    if (row.ends_with('|')) row.remove_suffix(1);
    for (auto part : std::views::split(row, '|')) cells.emplace_back(trimmed(std::string_view(part.begin(), part.end())));
    return cells;
}

bool under(std::string_view path, const Place& place) {
    return std::ranges::any_of(place.directories, [&](std::string_view directory) {
        return !directory.empty() && (path == directory || (path.starts_with(directory) && path.size() > directory.size() && path[directory.size()] == '/'));
    });
}

// The files under a project's directories (no build output, no dot directories).
std::vector<fs::path> files_of(const fs::path& root, const Place& place, auto&& wanted) {
    std::vector<fs::path> out;
    for (const std::string_view directory : place.directories) {
        if (directory.empty() || !fs::exists(root / directory)) continue;
        std::error_code error;
        for (auto it = fs::recursive_directory_iterator(root / directory, fs::directory_options::skip_permission_denied, error); it != fs::recursive_directory_iterator(); it.increment(error)) {
            if (error) break;
            const std::string name = mcpplibs::os::fs::utf8(it->path().filename());
            if (it->is_directory() && (name.starts_with('.') || name == "target" || name == "node_modules")) {
                it.disable_recursion_pending();
                continue;
            }
            if (it->is_regular_file() && wanted(it->path())) out.push_back(it->path());
        }
    }
    std::ranges::sort(out);
    return out;
}

std::string from_root(const fs::path& root, const fs::path& path) { return mcpplibs::os::fs::generic_utf8(fs::relative(path, root)); }

// A library: a package with an exported interface of its own (not a program, not a suite).
bool library(const fs::path& root, std::string_view path) {
    if (path.ends_with("/cts") || path.contains("/cts/")) return false;
    const fs::path sources = root / path / "src";
    if (!fs::exists(sources)) return false;
    std::error_code error;
    for (auto it = fs::recursive_directory_iterator(sources, error); it != fs::recursive_directory_iterator(); it.increment(error)) {
        if (error) break;
        if (!it->is_regular_file() || it->path().extension() != ".cppm") continue;
        std::ifstream in(it->path());
        for (std::string line; std::getline(in, line);)
            if (line.starts_with("export module ")) return true;
    }
    return false;
}

int rank(std::string_view label) { return label == "stable" ? 3 : label == "preview" ? 2 : label == "experimental" ? 1 : 0; }

Cell pass(std::string note) { return {Mark::pass, std::move(note)}; }
Cell owed(std::string note) { return {Mark::owed, std::move(note)}; }

}  // namespace

std::size_t Project::passed() const {
    return static_cast<std::size_t>(std::ranges::count_if(items, [](const Cell& cell) { return cell.mark == Mark::pass || cell.mark == Mark::exempt; }));
}
std::size_t Project::owed() const { return static_cast<std::size_t>(std::ranges::count(items, Mark::owed, &Cell::mark)); }
std::size_t Report::passed() const {
    return std::ranges::fold_left(projects | std::views::transform(&Project::passed), std::size_t{0}, std::plus{});
}
std::size_t Report::owed() const {
    return std::ranges::fold_left(projects | std::views::transform(&Project::owed), std::size_t{0}, std::plus{});
}

std::optional<std::string> definition(std::string_view readme) {
    bool titled = false;
    for (const auto line : lines_of(readme)) {
        if (!titled) {
            titled = line.starts_with("# ");
            continue;
        }
        const auto text = trimmed(line);
        if (text.empty()) continue;
        if (text.starts_with('#') || text.starts_with('|') || text.starts_with("- ") || text.starts_with('>')) return std::nullopt;
        // One sentence: it ends with a full stop, and none ends inside it (abbreviations aside).
        if (!text.ends_with('.')) return std::nullopt;
        std::string inner(text.substr(0, text.size() - 1));
        for (const std::string_view abbreviation : {"e.g. ", "i.e. ", "etc. ", "vs. "})
            for (std::size_t at = inner.find(abbreviation); at != std::string::npos; at = inner.find(abbreviation, at)) inner.replace(at, abbreviation.size(), "_");
        if (inner.contains(". ")) return std::nullopt;
        return std::string(text);
    }
    return std::nullopt;
}

std::optional<std::vector<std::string>> features_owed(std::string_view readme) {
    const auto lines = lines_of(readme);
    for (std::size_t at = 0; at + 1 < lines.size(); ++at) {
        if (!trimmed(lines[at]).starts_with('|') || !trimmed(lines[at + 1]).starts_with("|-") && !trimmed(lines[at + 1]).starts_with("| -")) continue;
        const auto header = cells_of(lines[at]);
        const auto label = std::ranges::find_if(header, [](const std::string& cell) { return lower(cell) == "label"; });
        if (label == header.end()) continue;
        const auto column = static_cast<std::size_t>(label - header.begin());
        std::vector<std::string> missing;
        for (std::size_t row = at + 2; row < lines.size() && trimmed(lines[row]).starts_with('|'); ++row) {
            const auto cells = cells_of(lines[row]);
            const std::string what = cells.empty() ? std::string() : cells.front();
            const std::string mark = column < cells.size() ? lower(cells[column]) : std::string();
            const bool labelled = mark.contains("stable") || mark.contains("preview") || mark.contains("experimental");
            const bool linked = std::ranges::any_of(cells, [](const std::string& cell) { return cell.contains("]("); });
            if (!labelled || !linked) missing.push_back(std::format("{} ({})", what, !labelled ? "no label" : "no evidence link"));
        }
        return missing;
    }
    return std::nullopt;
}

std::optional<std::vector<std::string>> limitations_owed(std::string_view readme) {
    const auto body = section(readme, "Known limitations");
    if (!body) return std::nullopt;
    static const std::regex form(R"re(^\*\*[^*]+\*\* \((foundation|part|specialised|application)\):)re");
    std::vector<std::string> missing;
    for (const std::string& item : items_of(*body))
        if (!std::regex_search(item, form)) missing.push_back(item.substr(0, 60));
    return missing;
}

std::map<std::size_t, std::string> exceptions(std::string_view readme) {
    std::map<std::size_t, std::string> out;
    const auto body = section(readme, "Closure exceptions");
    if (!body) return out;
    static const std::regex form(R"re(^\*\*(\d+)\*\*: (.+)$)re");
    for (const std::string& item : items_of(*body))
        if (std::smatch match; std::regex_search(item, match, form)) out[static_cast<std::size_t>(std::stoul(match[1].str()))] = match[2].str();
    return out;
}

std::expected<Report, std::string> check(const fs::path& root) {
    const auto inventory = docs::read(root);
    if (!inventory) return std::unexpected(inventory.error());
    // What every project's items are read against, read once.
    json budgets, claims;
    try {
        budgets = json::parse(read_file(root / "tests/bench/budgets.bench.xdef.json"));
        claims = json::parse(read_file(root / "docs/release.claims.xdef.json"));
    } catch (const std::exception& error) {
        return std::unexpected(std::format("the budgets or the claims ledger: {}", error.what()));
    }
    const std::array<std::string, 1> surface_rule{"agent-surface"};
    const auto surface = lint::run(root, surface_rule);
    // The consumer probes' fixtures: each manifest's dependencies (by path, or by a published version).
    std::vector<std::pair<std::string, std::string>> probes;  // fixture directory → its manifest
    if (fs::exists(root / "tools/ci"))
        for (const auto& entry : fs::directory_iterator(root / "tools/ci"))
            if (entry.is_directory() && fs::exists(entry.path() / "mcpp.toml.in"))
                probes.emplace_back(mcpplibs::os::fs::utf8(entry.path().filename()), read_file(entry.path() / "mcpp.toml.in"));
    std::ranges::sort(probes);

    Report report;
    for (const Place& place : places) {
        Project project{.name = std::string(place.name), .readme = std::string(place.readme)};
        std::vector<const docs::Package*> packages;
        for (const auto& package : inventory->packages)
            if (under(package.path, place)) {
                packages.push_back(&package);
                if (rank(package.label) > rank(project.label)) project.label = package.label;
            }
        const std::string readme = read_file(root / place.readme);
        const bool experimental = project.label == "experimental";
        auto& items = project.items;

        // 1 ★ The definition, the feature table, and (experimental) the target, gap and graduation.
        {
            const auto said = definition(readme);
            const auto features = features_owed(readme);
            std::vector<std::string> owes;
            if (!said) owes.push_back("the line under the title is not one sentence");
            if (!features) owes.push_back("no feature table with a Label column");
            else if (!features->empty()) owes.push_back(std::format("{} feature row(s) without a label or an evidence link, the first: {}", features->size(), features->front()));
            if (experimental) {
                const auto experiment = docs::section_of(readme);
                const auto has = [&](std::string_view key) {
                    return experiment && std::ranges::any_of(experiment->items, [&](const auto& item) { return item.first.starts_with(key); });
                };
                if (!has("Target") || !has("Gap") || !has("Graduation")) owes.push_back("the Experimental section's Target, Gap and Graduation");
            }
            items[0] = owes.empty() ? pass(std::format("\"{}\"", said->substr(0, 80))) : owed(owes.front() + (owes.size() > 1 ? std::format(" (and {} more)", owes.size() - 1) : ""));
        }
        // 2 Each stable library's surface committed (api.omui.txt; lint keeps aliases and single letters out); programs and
        // conformance suites have none.
        {
            std::vector<std::string> without;
            std::size_t stable = 0;
            for (const auto* package : packages)
                if (package->label == "stable" && library(root, package->path)) {
                    ++stable;
                    if (!fs::exists(root / package->path / "api.omui.txt")) without.push_back(package->name);
                }
            items[1] = without.empty() ? pass(std::format("{} stable librar(ies), each its api.omui.txt", stable))
                                       : owed(std::format("{} stable librar(ies) without api.omui.txt, the first {}", without.size(), without.front()));
        }
        // 3 Every error code's description has a way out (D-186): a description of three fields lacks it.
        {
            static const std::regex wayless(R"re(return\s*\{\s*"([a-z][a-z0-9_.\-]*)"\s*,\s*Kind::\w+\s*(,\s*"[^"]*"\s*)?\})re");
            std::size_t described = 0;
            std::vector<std::string> lacking;
            for (const auto& file : files_of(root, place, [](const fs::path& path) { return path.extension() == ".cppm" || path.extension() == ".cpp"; })) {
                const std::string text = read_file(file);
                if (!text.contains("Kind::")) continue;
                described += static_cast<std::size_t>(std::ranges::count_if(lines_of(text), [](std::string_view line) { return line.contains("return {\"") && line.contains("Kind::"); }));
                for (auto it = std::sregex_iterator(text.begin(), text.end(), wayless); it != std::sregex_iterator(); ++it)
                    lacking.push_back(std::format("{} ({})", (*it)[1].str(), from_root(root, file)));
            }
            items[2] = lacking.empty() ? pass(std::format("{} described code(s), each with its way out", described))
                                       : owed(std::format("{} code(s) without a way out, the first {}", lacking.size(), lacking.front()));
        }
        // 4 ★ lint agent-surface: nothing owed in the project.
        {
            const auto owing = std::ranges::count_if(surface, [&](const lint::Problem& problem) { return under(problem.where.substr(0, problem.where.find(':')), place); });
            items[3] = owing == 0 ? pass("lint agent-surface: none owed") : owed(std::format("lint agent-surface: {} owed", owing));
        }
        // 5 An operation catalog and a skill (its SOP).
        {
            const auto catalogs = files_of(root, place, [](const fs::path& path) { return mcpplibs::os::fs::utf8(path.filename()).ends_with(".ops.xdef.ts"); });
            const auto skills = files_of(root, place, [](const fs::path& path) { return path.filename() == "skill.omui.md"; });
            items[4] = !catalogs.empty() && !skills.empty() ? pass(std::format("{} catalog(s), {} skill(s)", catalogs.size(), skills.size()))
                       : owed(catalogs.empty() ? "no operation catalog (*.ops.xdef.ts)" : "no skill (skill.omui.md)");
        }
        // 6 ★ Tests: each stable or preview package has its own; an experimental project at least one (CI's rows are
        // reconciled by `omui-devtools ci run`).
        {
            std::vector<std::string> without;
            std::size_t tested = 0, counted = 0;
            for (const auto* package : packages) {
                if (package->name.ends_with(".cts")) continue;  // a conformance suite: it is the tests, run by its subjects
                const bool has = fs::exists(root / package->path / "tests");
                tested += has ? 1 : 0;
                if (package->label != "experimental") {
                    ++counted;
                    if (!has) without.push_back(package->name);
                }
            }
            if (experimental) items[5] = tested > 0 ? pass(std::format("{} package(s) with tests", tested)) : owed("no package with tests");
            else items[5] = without.empty() ? pass(std::format("{} stable or preview package(s), each with tests", counted))
                                            : owed(std::format("{} package(s) without tests, the first {}", without.size(), without.front()));
        }
        // 7 A budget for its signature scenes (tests/bench; D-187 on the real GPU is gate 4's).
        {
            std::size_t count = 0;
            for (const auto& budget : budgets.value("budgets", json::array())) {
                const std::string name = budget.value("name", "");
                const auto first = std::string_view(name).substr(0, name.find('/'));
                const auto owner = std::ranges::find(bench_owners, first, &std::pair<std::string_view, std::string_view>::first);
                if (owner != bench_owners.end() && owner->second == place.name) ++count;
            }
            items[6] = count > 0 ? pass(std::format("{} budget(s) in tests/bench", count)) : owed("no budget in tests/bench");
        }
        // 8 A side-by-side comparison dated from RC2 on, in the claims ledger.
        {
            std::string found;
            for (const auto& claim : claims.value("claims", json::array()))
                if (under(claim.value("where", ""), place) && claim.value("date", "") >= rc2_began) {
                    found = claim.value("id", "");
                    break;
                }
            items[7] = !found.empty() ? pass(std::format("claim {}", found)) : owed(std::format("no claim in the ledger dated from {}", rc2_began));
        }
        // 9 An out-of-tree probe built from the published packages, not paths.
        {
            std::vector<std::string> by_path, by_version;
            static const std::regex dependency(R"re(^\s*"?([A-Za-z0-9_.\-]+)"?\s*=\s*(.*)$)re");
            for (const auto& [fixture, manifest] : probes) {
                bool in_dependencies = false, mine = false, pathed = false;
                for (const auto line : lines_of(manifest)) {
                    if (line.starts_with('[')) in_dependencies = line.starts_with("[dependencies]");
                    std::string text(line);
                    if (std::smatch match; in_dependencies && std::regex_search(text, match, dependency)) {
                        const std::string key = match[1].str();
                        if (key.substr(0, key.find('.')) == place.name) {
                            mine = true;
                            pathed = pathed || match[2].str().contains("path");
                        }
                    }
                }
                if (mine) (pathed ? by_path : by_version).push_back(fixture);
            }
            if (!by_version.empty()) items[8] = pass(std::format("probe {} from published packages", by_version.front()));
            else if (!by_path.empty()) items[8] = owed(std::format("probe {} builds from paths; from published packages once RC3.1 publishes them", by_path.front()));
            else items[8] = owed("no out-of-tree probe (tools/ci/*/mcpp.toml.in)");
        }
        // 10 ★ Known limitations, each with the level it is solved at.
        {
            const auto limits = limitations_owed(readme);
            if (!limits) items[9] = owed("no Known limitations section");
            else if (!limits->empty()) items[9] = owed(std::format("{} item(s) without a level, the first: {}", limits->size(), limits->front()));
            else items[9] = pass("each item names its level");
        }
        // What the project's label requires, and what its README exempts.
        for (std::size_t index = 0; index < item_count; ++index) {
            if (experimental && !starred[index]) items[index] = {Mark::not_required, "experimental: the starred items only"};
        }
        for (const auto& [number, why] : exceptions(readme))
            if (number >= 1 && number <= item_count && items[number - 1].mark == Mark::owed) items[number - 1] = {Mark::exempt, why};
        report.projects.push_back(std::move(project));
    }
    return report;
}

std::string page(const Report& report, std::string_view language) {
    const bool zh = language == "zh";
    constexpr std::array<std::string_view, item_count> zh_names{"定义与特性", "API 复查", "错误", "Agent 面", "ops 与 skill",
                                                                "测试", "基准", "并排对照", "独立消费", "已知局限"};
    std::string out = zh ? "# 收口清单\n\n每个项目的收口清单（发布方案 §5.1，发布门第 3 条）：稳定与预览级过全部 10 项，实验级过带 ★ 的 4 项。"
                           "每一格从仓库里读出，由 `omui-devtools closure` 生成，不要手改。✓ 通过，○ 欠着，例外 写明的例外，— 不要求。\n\n"
                         : "# Closure\n\nEach project's closure checklist (release plan §5.1, gate 3): stable and preview projects pass all ten items, experimental "
                           "ones the four starred. Every cell is read from the repository by `omui-devtools closure`; do not edit. ✓ passed, ○ owed, "
                           "exempt an exception said, — not required.\n\n";
    out += zh ? "| 项目 | 标注 |" : "| Project | Label |";
    for (std::size_t index = 0; index < item_count; ++index)
        out += std::format(" {}{}{} |", index + 1, starred[index] ? "★ " : " ", zh ? zh_names[index] : item_names[index]);
    out += "\n|---|---|" + [] {
        std::string dashes;
        for (std::size_t index = 0; index < item_count; ++index) dashes += "---|";
        return dashes;
    }() + "\n";
    for (const Project& project : report.projects) {
        out += std::format("| {} | {} |", project.name, project.label);
        for (const Cell& cell : project.items)
            out += cell.mark == Mark::pass ? " ✓ |" : cell.mark == Mark::owed ? " ○ |" : cell.mark == Mark::exempt ? (zh ? " 例外 |" : " exempt |") : " — |";
        out += "\n";
    }
    const std::size_t passed = report.passed(), owing = report.owed();
    out += std::vformat(zh ? "\n通过 {} 项，欠 {} 项。\n\n## 欠着的\n\n" : "\n{} passed, {} owed.\n\n## Owed\n\n", std::make_format_args(passed, owing));
    for (const Project& project : report.projects)
        for (std::size_t index = 0; index < item_count; ++index)
            if (project.items[index].mark == Mark::owed)
                out += std::format("- **{}**, {} {}: {}\n", project.name, index + 1, zh ? zh_names[index] : item_names[index], project.items[index].note);
    return out;
}

}  // namespace omui::devtools::closure
