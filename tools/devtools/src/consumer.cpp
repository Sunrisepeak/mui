module omui.devtools.consumer;

import std;
import mcpplibs.os;
import mlibs.hash;
import nlohmann.json;

namespace omui::devtools::consumer {

namespace {

namespace fs = std::filesystem;
namespace process = mcpplibs::os::process;
using json = nlohmann::json;
using Outcome = std::expected<void, std::string>;

constexpr std::array<std::string_view, 5> probe_names{"hanim-core", "hanim-api", "hanim-play", "omui-hanim", "hanim-features"};

bool write_text(const fs::path& path, std::string_view text) {
    std::error_code error;
    if (path.has_parent_path()) fs::create_directories(path.parent_path(), error);
    std::ofstream out(path, std::ios::binary);
    out << text;
    return static_cast<bool>(out);
}

std::string text_of(const fs::path& path) { return mcpplibs::os::fs::utf8(path); }

// A file's SHA-256, or null where there is no file.
json digest(const fs::path& path) {
    std::error_code error;
    if (!fs::is_regular_file(path, error)) return nullptr;
    return mlibs::hash::sha256_hex(mcpplibs::os::fs::read(path).value_or(""));
}

// What a path is, resolved as the system resolves it (symbolic links followed where they exist).
fs::path real(const fs::path& path) {
    std::error_code error;
    const auto resolved = fs::weakly_canonical(path, error);
    return error ? path.lexically_normal() : resolved;
}

bool inside(const fs::path& path, const fs::path& parent) {
    const auto relative = path.lexically_relative(parent);
    return !relative.empty() && *relative.begin() != "..";
}

// A JSON value per line that begins with `{` (after blanks where `trimmed`).
std::vector<json> json_lines(std::string_view text, bool trimmed = false) {
    std::vector<json> out;
    for (auto part : std::views::split(text, '\n')) {
        std::string_view line(part.begin(), part.end());
        if (trimmed) line.remove_prefix(std::min(line.find_first_not_of(" \t"), line.size()));
        if (!line.starts_with('{')) continue;
        auto value = json::parse(line, nullptr, false);
        if (!value.is_discarded()) out.push_back(std::move(value));
    }
    return out;
}

// JSON objects in text however they are laid out (pretty-printed across lines too): each one that opens with `{` at
// the start of a line and closes with `}` at the start of a later one, parsed whole.
std::vector<json> json_objects(std::string_view text) {
    std::vector<std::size_t> opens, closes;
    for (std::size_t at = 0; at < text.size(); at = text.find('\n', at) == std::string_view::npos ? text.size() : text.find('\n', at) + 1) {
        if (text[at] == '{') opens.push_back(at);
        if (text[at] == '}') closes.push_back(at);
    }
    std::vector<json> out;
    std::size_t after = 0;
    for (const auto open : opens) {
        if (open < after) continue;
        const std::size_t line_end = text.find('\n', open);
        auto single = json::parse(text.substr(open, line_end == std::string_view::npos ? std::string_view::npos : line_end - open), nullptr, false);
        if (!single.is_discarded()) {
            out.push_back(std::move(single));
            after = line_end == std::string_view::npos ? text.size() : line_end;
            continue;
        }
        for (const auto close : closes) {
            if (close <= open) continue;
            auto whole = json::parse(text.substr(open, close + 1 - open), nullptr, false);
            if (whole.is_discarded()) continue;
            out.push_back(std::move(whole));
            after = close + 1;
            break;
        }
    }
    return out;
}

std::string identity(const json& package) {
    const std::string space = package.value("namespace", ""), name = package.value("name", "");
    return space.empty() ? name : space + "." + name;
}

// A compile command's source, absolute.
fs::path source_of(const json& entry, const fs::path& project) {
    fs::path file = entry.value("file", ""), directory = entry.value("directory", "");
    if (!directory.is_absolute()) directory = project / directory;
    return real(file.is_absolute() ? file : directory / file);
}

std::string git_head(const fs::path& repo) {
    process::spawn_options options;
    options.program = "git";
    options.args = {"-C", text_of(repo), "rev-parse", "HEAD"};
    const auto done = process::run(std::move(options));
    if (!done || done->status.shell_code() != 0) return "unknown";
    std::string head = done->out;
    while (!head.empty() && (head.back() == '\n' || head.back() == '\r')) head.pop_back();
    return head;
}

fs::path fresh_directory(std::string_view prefix) {
    const fs::path made = fs::temp_directory_path() / std::format("{}{:08x}", prefix, std::random_device{}());
    fs::create_directories(made);
    return made;
}

bool same_json(const json& a, const json& b) { return a == b; }

// The newest resolution.json under a project's target/ (schema 2), its compile database beside it.
struct Metadata {
    fs::path resolution, cdb;
    json data, entries;
};

std::expected<Metadata, std::string> newest_metadata(const fs::path& project) {
    std::optional<fs::path> newest;
    fs::file_time_type when{};
    std::error_code error;
    for (const auto& platform : fs::directory_iterator(project / "target", error))
        for (const auto& build : fs::directory_iterator(platform.path(), error)) {
            const fs::path candidate = build.path() / "resolution.json";
            if (!fs::is_regular_file(candidate, error)) continue;
            const auto written = fs::last_write_time(candidate, error);
            if (!newest || written > when) newest = candidate, when = written;
        }
    if (!newest) return std::unexpected("mcpp did not emit resolution.json");
    Metadata out{*newest, newest->parent_path() / "compile_commands.json", json::parse(mcpplibs::os::fs::read(*newest).value_or(""), nullptr, false),
                 json::parse(mcpplibs::os::fs::read(newest->parent_path() / "compile_commands.json").value_or("[]"), nullptr, false)};
    if (out.data.is_discarded() || out.data.value("schema_version", 0) != 2) return std::unexpected("unrecognized mcpp resolution schema; review the graph audit");
    if (out.entries.is_discarded()) return std::unexpected("the compile database is not JSON");
    return out;
}

void copy_file_to(const fs::path& from, const fs::path& to) {
    std::error_code error;
    fs::create_directories(to.parent_path(), error);
    fs::copy_file(from, to, fs::copy_options::overwrite_existing, error);
}

// A fresh project, its mcpp commands logged, the report written after each; the repository's lock held.
class Probe {
public:
    Probe(const fs::path& root, fs::path output, std::string_view prefix, fs::path artifacts = {})
        : repo(real(root)), output(real(output)), project(fresh_directory(prefix)), mcpp(mcpplibs::os::env::get("MCPP").value_or("mcpp")) {
        this->artifacts = artifacts.empty() ? this->output.parent_path() / (text_of(this->output.stem()) + "-artifacts") : artifacts;
        fs::create_directories(this->artifacts);
        lock_before = digest(repo / "mcpp.lock");
    }

    struct Run {
        std::vector<std::string> argv;  // after mcpp
        bool bootstrap = false;         // the project's own resolution: MCPP_LOCKED off
        int expected = 0;
        fs::path cwd;                   // empty: the project
        std::vector<std::pair<std::string, std::string>> variables;
        bool headless = false;          // no display variables
        bool one_log = false;           // standard output and error in one log
    };

    std::expected<std::string, std::string> run(std::string_view label, const Run& asked) {
        process::spawn_options options;
        options.program = mcpp;
        options.args = asked.argv;
        options.cwd = asked.cwd.empty() ? project : asked.cwd;
        if (!mcpplibs::os::env::get("MCPP_JOBS")) options.env.emplace_back("MCPP_JOBS", "2");
        if (asked.bootstrap) options.env.emplace_back("MCPP_LOCKED", std::nullopt);
        else options.env.emplace_back("MCPP_LOCKED", "1");
        if (asked.headless) {
            options.env.emplace_back("DISPLAY", std::nullopt);
            options.env.emplace_back("WAYLAND_DISPLAY", std::nullopt);
        }
        for (const auto& [name, value] : asked.variables) options.env.emplace_back(name, value);
        const auto started = std::chrono::steady_clock::now();
        auto done = process::run(std::move(options));
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        const std::string out = done ? done->out : std::string{}, err = done ? done->err : done.error().message();
        const int status = done ? done->status.shell_code() : 127;
        const std::string prefix = asked.cwd.empty() ? std::string{} : text_of(asked.cwd.filename()) + "-";
        json record{{"argv", asked.argv}, {"cwd", text_of(options.cwd)}, {"exit_code", status}, {"expected", asked.expected}, {"seconds", seconds}};
        if (asked.one_log) {
            const fs::path log = artifacts / (prefix + std::string(label) + ".log");
            write_text(log, out + "\nSTDERR\n" + err);
            record["log"] = text_of(log);
        } else {
            const fs::path stdout_log = artifacts / (prefix + std::string(label) + ".stdout.log"), stderr_log = artifacts / (prefix + std::string(label) + ".stderr.log");
            write_text(stdout_log, out);
            write_text(stderr_log, err);
            record["stdout"] = text_of(stdout_log);
            record["stderr"] = text_of(stderr_log);
        }
        report["commands"].push_back(record);
        save();
        if (status != asked.expected) return std::unexpected(std::format("{}: exit {}, expected {}; see {}", label, status, asked.expected, artifacts_of(label, asked)));
        return out;
    }

    void save() { write_text(output, report.dump(2) + "\n"); }

    // The report finished: the repository's lock held, the project removed unless kept; the summary line printed.
    int finish(Outcome outcome, bool keep, std::string_view lock_after_key, json summary) {
        report["ok"] = outcome.has_value();
        if (!outcome) report["error"] = outcome.error();
        report[std::string(lock_after_key)] = digest(repo / "mcpp.lock");
        if (report[std::string(lock_after_key)] != lock_before) {
            report["ok"] = false;
            report["error"] = "the repository's lock changed during the out-of-tree consumption";
        }
        report["project_retained"] = keep;
        save();
        if (!keep) {
            std::error_code error;
            fs::remove_all(project, error);
        }
        summary["ok"] = report["ok"];
        summary["report"] = text_of(output);
        if (report.contains("error")) summary["error"] = report["error"];
        std::cout << summary.dump() << '\n';
        return report["ok"].get<bool>() ? 0 : 1;
    }

    fs::path repo, output, artifacts, project;
    std::string mcpp;
    json report = json::object();
    json lock_before;

private:
    std::string artifacts_of(std::string_view label, const Run& asked) const {
        const std::string prefix = asked.cwd.empty() ? std::string{} : text_of(asked.cwd.filename()) + "-";
        return text_of(artifacts / (prefix + std::string(label) + (asked.one_log ? ".log" : ".stderr.log")));
    }
};

// ---- hanim-core ------------------------------------------------------------------------------------------------

Outcome hanim_core(Probe& probe) {
    const fs::path fixture = probe.repo / "tools/ci/hanim-core-consumer";
    fs::create_directories(probe.project / "src");
    std::string manifest = mcpplibs::os::fs::read(fixture / "mcpp.toml.in").value_or("");
    for (const auto& [key, path] : {std::pair{"core_path", probe.repo / "modules/projects/hanim/core"}, std::pair{"utils_path", probe.repo / "modules/base/utils"}}) {
        const std::string mark = std::format("{{{{{}}}}}", key), value = json(mcpplibs::os::fs::generic_utf8(path)).dump();
        for (std::size_t at = manifest.find(mark); at != std::string::npos; at = manifest.find(mark, at + value.size())) manifest.replace(at, mark.size(), value);
    }
    write_text(probe.project / "mcpp.toml", manifest);
    copy_file_to(fixture / "src/main.cpp", probe.project / "src/main.cpp");
    copy_file_to(probe.project / "mcpp.toml", probe.artifacts / "consumer.mcpp.toml");
    copy_file_to(probe.project / "src/main.cpp", probe.artifacts / "consumer.cpp");
    // No inherited repository lock or workspace: this project's own path-only graph resolved once, then held.
    if (auto done = probe.run("bootstrap", {.argv = {"build", "--profile", "dev"}, .bootstrap = true}); !done) return std::unexpected(done.error());
    const json temporary_lock = digest(probe.project / "mcpp.lock");
    probe.report["temporary_lock_sha256"] = temporary_lock;
    probe.report["temporary_lock_required"] = !temporary_lock.is_null();  // a path-only graph has nothing to pin
    const std::map<std::string, fs::path> expected{{"hanim.validation.core_consumer", probe.project},
                                                   {"hanim.core", probe.repo / "modules/projects/hanim/core"},
                                                   {"mlibs.math", probe.repo / "modules/base/math"},
                                                   {"mlibs.utils", probe.repo / "modules/base/utils"},
                                                   {"mlibs.memory", probe.repo / "modules/base/memory"},
                                                   {"mlibs.geom", probe.repo / "modules/base/geom"}};
    for (const std::string profile : {"dev", "release"}) {
        auto ran = probe.run(profile, {.argv = {"run", "--locked", "--profile", profile}});
        if (!ran) return std::unexpected(ran.error());
        const auto rows = json_lines(*ran, true);
        std::vector<json> results, logs;
        for (const auto& row : rows) {
            if (row.value("kind", "") == "hanim.core-consumer") results.push_back(row);
            if (row.contains("level") && row.contains("category")) logs.push_back(row);
        }
        if (results.size() != 1 || !results.front().value("ok", false)) return std::unexpected(profile + ": missing successful real consumer result");
        const json& result = results.front();
        for (const auto& [field, value] : std::initializer_list<std::pair<std::string_view, json>>{
                 {"source_lines", 3}, {"target_cubics", 4}, {"contours", 1}, {"seek_count", 6}, {"timeline_channels", 2}, {"log_records", 8},
                 {"repeated_seek_equal", true}, {"endpoints_differ", true}})
            if (!result.contains(field) || result[std::string(field)] != value)
                return std::unexpected(std::format("{}: unexpected {}: {}", profile, field, result.contains(field) ? result[std::string(field)].dump() : "None"));
        const int prepared = result.value("prepared_cubics", 0);
        if (prepared < 4 || prepared > 64) return std::unexpected(profile + ": preparation segment budget not respected");
        if (logs.size() != 8 || std::ranges::any_of(logs, [](const json& row) { return row.value("category", "") != "hanim.consumer" || row.value("level", "") != "info"; }))
            return std::unexpected(profile + ": real sink/history records differ");
        if (std::ranges::any_of(logs, [](const json& row) {
                return !row.contains("line") || row["line"] == 0 || row["line"].is_null() || !row.contains("time") || row["time"].is_null() ||
                       !row.value("file", std::string{}).contains("main.cpp");
            }))
            return std::unexpected(profile + ": missing caller/history metadata");
        // The configured build graph mcpp emits, audited after the successful run, never a manifest-only guess.
        if (auto done = probe.run(profile + "-metadata", {.argv = {"build", "--locked", "--profile", profile, "--configure-only"}}); !done) return std::unexpected(done.error());
        auto metadata = newest_metadata(probe.project);
        if (!metadata) return std::unexpected(profile + ": " + metadata.error());
        std::set<std::string> identities;
        for (const auto& package : metadata->data["graph"]["packages"]) {
            const json& data = package["package"];
            const std::string name = identity(data);
            identities.insert(name);
            const auto found = expected.find(name);
            if (found == expected.end() || data.value("source", "") != "path+" + mcpplibs::os::fs::generic_utf8(found->second))
                return std::unexpected(std::format("{}: unexpected actual package/source {}: {}", profile, name, data.value("source", "")));
        }
        std::set<std::string> wanted;
        for (const auto& [name, _] : expected) wanted.insert(name);
        if (identities != wanted) return std::unexpected(profile + ": unexpected actual closure");
        const std::vector<fs::path> allowed{probe.project, probe.repo / "modules/projects/hanim/core", probe.repo / "modules/base/math",
                                            probe.repo / "modules/base/utils", probe.repo / "modules/base/memory", probe.repo / "modules/base/geom"};
        json sources = json::array();
        bool consumer = false;
        for (const auto& entry : metadata->entries) {
            const fs::path source = source_of(entry, probe.project);
            sources.push_back(text_of(source));
            consumer = consumer || source == real(probe.project / "src/main.cpp");
            if (inside(source, probe.repo) && std::ranges::none_of(allowed, [&](const fs::path& root) { return inside(source, root); }))
                return std::unexpected(std::format("{}: unexpected repository source in the actual compile database: {}", profile, text_of(source)));
        }
        if (!consumer) return std::unexpected(profile + ": consumer missing from the actual compile database");
        const fs::path snapshot = probe.artifacts / (profile + ".resolution.json");
        copy_file_to(metadata->resolution, snapshot);
        probe.report["runs"].push_back({{"profile", profile}, {"result", result}, {"log_records", logs}, {"actual_packages", metadata->data["graph"]["packages"]},
                                        {"actual_package_names", identities}, {"metadata_command", probe.report["commands"].back()["argv"]},
                                        {"resolution", text_of(snapshot)}, {"toolchain", metadata->data["toolchain"]}, {"compiled_sources", sources}});
        if (digest(probe.project / "mcpp.lock") != temporary_lock) return std::unexpected("a locked run changed the temporary resolution");
    }
    return {};
}

// ---- hanim-api -------------------------------------------------------------------------------------------------

// Whether a repository source is of the independent layers hanim may bring: mlibs (base, gfx and, with `wider`,
// content and media; mlibs.dynamics, the solver hanim shares with games, is base: D-131, D-133, D-231), and the
// gfx-layer projects hanim and xrender. Nothing of the engine's tier.
bool independent(const fs::path& source, const fs::path& repo, bool wider) {
    if (!inside(source, repo)) return true;
    std::vector<std::string> parts;
    for (const auto& part : source.lexically_relative(repo)) parts.push_back(text_of(part));
    const auto starts = [&](std::initializer_list<std::string_view> prefix) {
        return parts.size() >= prefix.size() && std::ranges::equal(prefix, std::span(parts).first(prefix.size()));
    };
    return starts({"modules", "base"}) || starts({"modules", "gfx"}) || (wider && (starts({"modules", "content"}) || starts({"modules", "media"}))) ||
           starts({"modules", "projects", "hanim"}) || starts({"modules", "projects", "xrender"});
}

Outcome hanim_api(Probe& probe) {
    const fs::path fixture = probe.repo / "tools/ci/hanim-api-probe";
    fs::create_directories(probe.project / "src");
    std::string manifest = mcpplibs::os::fs::read(fixture / "mcpp.toml.in").value_or("");
    const std::string mark = "{{hanim_path}}", value = json(text_of(probe.repo / "modules/projects/hanim")).dump();
    for (std::size_t at = manifest.find(mark); at != std::string::npos; at = manifest.find(mark, at + value.size())) manifest.replace(at, mark.size(), value);
    write_text(probe.project / "mcpp.toml", manifest);
    std::error_code error;
    for (const auto& file : fs::directory_iterator(fixture / "src", error)) copy_file_to(file.path(), probe.project / "src" / file.path().filename());
    copy_file_to(probe.project / "mcpp.toml", probe.artifacts / "consumer.mcpp.toml");
    if (auto done = probe.run("bootstrap", {.argv = {"build", "--profile", "dev"}, .bootstrap = true}); !done) return std::unexpected(done.error());
    const json temporary_lock = digest(probe.project / "mcpp.lock");
    probe.report["temporary_lock_sha256"] = temporary_lock;
    probe.report["temporary_lock_present"] = !temporary_lock.is_null();
    const std::set<std::string> forbidden{"omui.core", "omui.sdk", "openmui.testing", "mlibs.testing", "openlifa.openmui", "openmui.spec"};
    for (const std::string profile : {"dev", "release"}) {
        auto ran = probe.run(profile, {.argv = {"run", "--locked", "--profile", profile}});
        if (!ran) return std::unexpected(ran.error());
        std::optional<json> result;
        for (const auto& row : json_lines(*ran))
            if (!result && row.value("kind", "") == "hanim.api-probe") result = row;
        if (!result || !result->value("ok", false) || !result->contains("checks") || (*result)["checks"].empty() ||
            !std::ranges::all_of((*result)["checks"], [](const json& check) { return check.value("ok", false); }))
            return std::unexpected(profile + ": missing successful check report");
        if (auto done = probe.run(profile + "-metadata", {.argv = {"build", "--locked", "--profile", profile, "--configure-only"}}); !done) return std::unexpected(done.error());
        auto metadata = newest_metadata(probe.project);
        if (!metadata) return std::unexpected(metadata.error());
        std::vector<std::string> names;
        std::optional<json> umbrella;
        for (const auto& entry : metadata->data["graph"]["packages"]) {
            const std::string name = identity(entry["package"]);
            names.push_back(name);
            if (forbidden.contains(name) || std::ranges::any_of(std::initializer_list<std::string_view>{"omui.ui.", "omui.host.", "omui.site", "openmui.rhi", "mlibs.rhi",
                                                                                                         "openmui.render", "xrender"},
                                                                [&](std::string_view prefix) { return name.starts_with(prefix); }))
                return std::unexpected("forbidden UI/renderer dependency: " + name);
            // The product's package is sunrisepeak.hanim (its namespace, 2026-11-20); the module stays hanim.
            if (!umbrella && entry["package"].value("name", "") == "hanim") umbrella = entry["package"];
        }
        if (!umbrella || umbrella->value("source", "") != "path+" + text_of(probe.repo / "modules/projects/hanim"))
            return std::unexpected("the umbrella did not resolve to the public source package");
        if (!std::ranges::contains(names, "sunrisepeak.hanim") || !std::ranges::contains(names, "hanim.core")) return std::unexpected("the real public hanim package is missing");
        std::set<std::string> sources;
        json source_hashes = json::object();
        for (const auto& item : metadata->entries) {
            const fs::path source = source_of(item, probe.project);
            sources.insert(text_of(source));
            if (!independent(source, probe.repo, false)) return std::unexpected("non-independent repository source: " + text_of(source));
            if (inside(source, probe.repo)) source_hashes[mcpplibs::os::fs::generic_utf8(source.lexically_relative(probe.repo))] = digest(source);
            if (inside(source, probe.project / "src") && !std::ranges::contains(item.value("arguments", json::array()), json("-std=c++23")))
                return std::unexpected("the consumer was not compiled as C++23");
        }
        if (!sources.contains(text_of(real(probe.project / "src/main.cpp")))) return std::unexpected("the actual compilation omitted the consumer's source");
        for (const std::string_view role : {"object", "effect", "library", "program", "engine"})
            if (!sources.contains(text_of(probe.repo / "modules/projects/hanim/src" / role / (std::string(role) + ".cpp"))))
                return std::unexpected("the public product omitted an internal implementation");
        copy_file_to(metadata->resolution, probe.artifacts / (profile + ".resolution.json"));
        copy_file_to(metadata->cdb, probe.artifacts / (profile + ".compile_commands.json"));
        if (digest(probe.project / "mcpp.lock") != temporary_lock) return std::unexpected("the consumer's lock changed");
        std::ranges::sort(names);
        probe.report["profiles"].push_back({{"profile", profile}, {"result", *result}, {"packages", names}, {"compiled_sources", sources},
                                            {"source_files_sha256", source_hashes}, {"toolchain", metadata->data["toolchain"]},
                                            {"resolution", text_of(probe.artifacts / (profile + ".resolution.json"))},
                                            {"cdb", text_of(probe.artifacts / (profile + ".compile_commands.json"))}});
    }
    return {};
}

// ---- hanim-play ------------------------------------------------------------------------------------------------

std::expected<json, std::string> play_audit(Probe& probe, const std::string& profile, const fs::path& openas) {
    auto metadata = newest_metadata(probe.project);
    if (!metadata) return std::unexpected(metadata.error());
    const std::set<std::string> forbidden{"omui.core", "omui.sdk", "openmui.testing", "mlibs.testing", "openlifa.openmui", "openmui.spec"};
    const std::set<std::string> required{"sunrisepeak.hanim", "hanim.core", "xrender.vector", "xrender.raster", "mlibs.image", "mlibs.control", "openlifa.openas"};
    std::vector<std::string> names;
    for (const auto& package : metadata->data["graph"]["packages"]) {
        const json& item = package["package"];
        const std::string name = identity(item);
        names.push_back(name);
        if (forbidden.contains(name) || name.starts_with("omui.ui.") || name.starts_with("omui.host.") || name.starts_with("omui.site"))
            return std::unexpected("forbidden UI/spec/testing/host package: " + name);
        const std::string source = item.value("source", "");
        if (name == "openlifa.openas" && !openas.empty() && (!source.starts_with("path+") || real(source.substr(5)) != openas))
            return std::unexpected("OpenAS did not resolve to the requested unpacked artifact");
        if (source.starts_with("path+") && !independent(real(source.substr(5)), probe.repo, true))
            return std::unexpected("non-independent repository source in the closure: " + source.substr(5));
    }
    for (const auto& name : required)
        if (!std::ranges::contains(names, name)) return std::unexpected("missing a real public consumer: " + name);
    std::set<std::string> sources;
    for (const auto& entry : metadata->entries) {
        const fs::path source = source_of(entry, probe.project);
        if (!independent(source, probe.repo, true)) return std::unexpected("non-independent repository source in the closure: " + text_of(source));
        sources.insert(text_of(source));
    }
    if (!sources.contains(text_of(real(probe.project / "src/main.cpp")))) return std::unexpected("the actual compile database omitted the consumer");
    std::vector<fs::path> wanted;
    for (const std::string_view role : {"object", "effect", "library", "program", "engine"})
        wanted.push_back(probe.repo / "modules/projects/hanim/src" / role / (std::string(role) + ".cpp"));
    wanted.push_back(probe.repo / "modules/projects/hanim/src/render/vector/vector.cpp");
    for (const auto& path : wanted)
        if (!sources.contains(text_of(path))) return std::unexpected("the actual compile database omitted a selected product implementation");
    if (!openas.empty()) {
        bool conformance = false, implementation = false;
        for (const auto& source : sources) {
            const fs::path path = source;
            if (!inside(path, openas)) continue;
            conformance = conformance || path.filename() == "conformance.cppm";
            implementation = implementation || path.extension() == ".cpp";
        }
        if (!conformance || implementation) return std::unexpected("the OpenAS artifact did not supply conformance without implementation sources");
    }
    copy_file_to(metadata->resolution, probe.artifacts / (profile + ".resolution.json"));
    copy_file_to(metadata->cdb, probe.artifacts / (profile + ".compile_commands.json"));
    json hashes = json::object();
    for (const auto& source : sources)
        if (inside(fs::path(source), probe.repo)) hashes[mcpplibs::os::fs::generic_utf8(fs::path(source).lexically_relative(probe.repo))] = digest(source);
    std::ranges::sort(names);
    return json{{"packages", names},
                {"compiled_sources", sources},
                {"source_files_sha256", hashes},
                {"resolution", text_of(probe.artifacts / (profile + ".resolution.json"))},
                {"cdb", text_of(probe.artifacts / (profile + ".compile_commands.json"))},
                {"toolchain", metadata->data["toolchain"]}};
}

std::optional<json> kind_row(std::string_view text, std::string_view kind) {
    for (const auto& row : json_lines(text, true))
        if (row.value("kind", "") == kind) return row;
    return std::nullopt;
}

Outcome hanim_play(Probe& probe, const fs::path& openas) {
    const fs::path fixture = probe.repo / "tools/ci/hanim-play-consumer";
    fs::create_directories(probe.project / "src");
    std::string manifest = mcpplibs::os::fs::read(fixture / "mcpp.toml.in").value_or("");
    for (const auto& [key, relative] : std::initializer_list<std::pair<std::string_view, std::string_view>>{
             {"engine", "projects/hanim"}, {"raster", "projects/xrender/raster"}, {"image", "base/image"}, {"utils", "base/utils"}, {"control", "base/control"}}) {
        const std::string mark = std::format("{{{{{}_path}}}}", key), value = json(text_of(probe.repo / "modules" / relative)).dump();
        for (std::size_t at = manifest.find(mark); at != std::string::npos; at = manifest.find(mark, at + value.size())) manifest.replace(at, mark.size(), value);
    }
    if (!openas.empty()) {
        const std::string published = "openlifa.openas = \"0.2.1\"";
        if (const auto at = manifest.find(published); at != std::string::npos)
            manifest.replace(at, published.size(), "openlifa.openas = { path = " + json(text_of(openas)).dump() + " }");
    }
    write_text(probe.project / "mcpp.toml", manifest);
    copy_file_to(fixture / "src/main.cpp", probe.project / "src/main.cpp");
    copy_file_to(probe.project / "mcpp.toml", probe.artifacts / "consumer.mcpp.toml");
    copy_file_to(probe.project / "src/main.cpp", probe.artifacts / "consumer.cpp");
    const auto run = [&](std::string label, std::vector<std::string> argv, int expected = 0, bool bootstrap = false, fs::path cwd = {}) -> std::expected<std::string, std::string> {
        auto done = probe.run(label, {.argv = std::move(argv), .bootstrap = bootstrap, .expected = expected, .cwd = std::move(cwd), .headless = true});
        if (done && !openas.empty()) {
            const auto stderr_log = mcpplibs::os::fs::read(probe.artifacts / (label + ".stderr.log")).value_or("");
            if (stderr_log.contains("not one of that package's public modules"))
                return std::unexpected(label + ": a private module import remains in the artifact's configuration");
        }
        return done;
    };
    if (auto done = run("bootstrap", {"build", "--profile", "dev"}, 0, true); !done) return std::unexpected(done.error());
    const json temporary_lock = digest(probe.project / "mcpp.lock");
    probe.report["temporary_lock_sha256"] = temporary_lock;
    for (const std::string profile : {"dev", "release"}) {
        auto discovery_text = run(profile + "-discovery", {"run", "--locked", "--profile", profile, "--", "--describe-control"});
        if (!discovery_text) return std::unexpected(discovery_text.error());
        std::optional<json> discovery;
        for (const auto& row : json_lines(*discovery_text))
            if (!discovery && row.dump().contains("hanim.control-discovery")) discovery = row;
        if (!discovery) return std::unexpected("no native operation discovery");
        std::map<std::string, json> operations;
        for (const auto& item : (*discovery)["operations"]) operations[item.value("name", "")] = item;
        if (operations.size() != 13 || operations["animation.release"].value("effect", "") != "irreversible" || operations["animation.inspect"].value("effect", "") != "read")
            return std::unexpected("the native operation discovery contract differs");
        struct Shot {
            std::string label;
            double time;
            std::string region;
            bool retarget;
            double bend;
        };
        const std::vector<Shot> shots{{"start", 0, "0,0,320,240", false, 0},         {"middle", .5, "0,0,320,240", false, 0},
                                      {"end", 1, "0,0,320,240", false, 0},           {"crop", .5, "80,40,160,160", false, 0},
                                      {"retarget", .5, "0,0,320,240", true, 0},      {"path-start", 0, "0,0,320,240", false, .35},
                                      {"path-middle", .5, "0,0,320,240", false, .35}, {"path-end", 1, "0,0,320,240", false, .35},
                                      {"path-retarget", .5, "0,0,320,240", true, .35}};
        const auto number = [](double value) { return value == std::floor(value) ? std::format("{}", static_cast<int>(value)) : std::format("{}", value); };
        json results = json::array();
        for (const auto& shot : shots) {
            const fs::path png = probe.artifacts / std::format("{}-{}.png", profile, shot.label);
            std::vector<std::string> argv{"run", "--locked", "--profile", profile, "--", "--time", number(shot.time), "--region", shot.region, "--bend",
                                          number(shot.bend), "--out", text_of(png)};
            if (shot.retarget) argv.push_back("--retarget");
            auto text = run(profile + "-" + shot.label, argv);
            if (!text) return std::unexpected(text.error());
            std::vector<json> rows;
            for (const auto& row : json_lines(*text, true))
                if (row.value("kind", "") == "hanim.play-consumer") rows.push_back(row);
            if (rows.size() != 1 || !rows.front().value("ok", false)) return std::unexpected(std::format("{}-{}: no successful provenance", profile, shot.label));
            const json& result = rows.front();
            if (result["vertex_path"] != json{{"stage", "after-correspondence"}, {"space", "endpoint-chord"}, {"bend", shot.bend}})
                return std::unexpected("the process path's provenance disagrees");
            if (result["sample_time_seconds"] != shot.time || result["parts"] != 1 || result["contours"] != 1 || result["visible_pixels"].get<double>() <= 100)
                return std::unexpected("the consumer's time, geometry or visible pixels disagree");
            if (result["backend"] != "cpu-reference" || result["controls"]["retarget"] != shot.retarget || !result["controls"]["pause_holds"].get<bool>() ||
                !result["controls"]["reverse_moves_backwards"].get<bool>())
                return std::unexpected("the consumer's controls or backend disagree");
            const auto size = png_size(mcpplibs::os::fs::read(png).value_or(""));
            if (!size || size->first != result["region"]["width"].get<std::uint32_t>() || size->second != result["region"]["height"].get<std::uint32_t>())
                return std::unexpected("the actual PNG's header disagrees with the region's provenance");
            fs::path sidecar = png;
            sidecar += ".provenance.xdef.json";
            if (!same_json(json::parse(mcpplibs::os::fs::read(sidecar).value_or(""), nullptr, false), result)) return std::unexpected("the sidecar provenance differs from standard output");
            results.push_back(result);
        }
        std::set<std::string> ends{results[0]["pixel_fnv1a"].dump(), results[1]["pixel_fnv1a"].dump(), results[2]["pixel_fnv1a"].dump()};
        if (ends.size() != 3) return std::unexpected("the actual endpoint and midpoint pixels must differ");
        if (results[0]["pixel_fnv1a"] != results[5]["pixel_fnv1a"] || results[2]["pixel_fnv1a"] != results[7]["pixel_fnv1a"] ||
            results[1]["pixel_fnv1a"] == results[6]["pixel_fnv1a"])
            return std::unexpected("the process path must keep the endpoints' pixels and change the actual middle's");
        for (const json& result : {results[4], results[8]})
            if (result["target"] == result["initial_target"] || result["controls"]["retarget_pixel_max_u8"].get<double>() > 18 ||
                result["controls"]["retarget_coverage_off_boundary"].get<double>() != 0 || result["controls"]["retarget_styled_off_boundary"].get<double>() != 0)
                return std::unexpected("the real retarget's target or continuity failed");
        json reviews = json::array();
        for (const auto& [label, bend, mode] : std::initializer_list<std::tuple<std::string_view, double, std::string_view>>{
                 {"agent-linear", 0, ""}, {"agent-curved", .35, ""}, {"agent-properties", .35, "properties"}, {"agent-parts", 0, "parts"}, {"agent-channels", 0, "channels"}}) {
            const fs::path png = probe.artifacts / std::format("{}-{}.png", profile, label);
            std::vector<std::string> argv{"run", "--locked", "--profile", profile, "--", "--out", text_of(png), "--bend", number(bend), "--agent-review"};
            if (!mode.empty()) argv.push_back("--" + std::string(mode));
            auto text = run(profile + "-" + std::string(label), argv);
            if (!text) return std::unexpected(text.error());
            const auto result = kind_row(*text, "hanim.play-consumer");
            if (!result || !result->value("ok", false)) return std::unexpected("the independent Agent review did not finish");
            if (mode == "properties" && (*result)["fixture"] != "triangle-properties") return std::unexpected("the property review did not exercise the public property recipe");
            if (mode == "parts" && ((*result)["fixture"] != "selected-parts" || (*result)["parts"] != 4 || (*result)["contours"] != 3))
                return std::unexpected("the selected-part review did not use the public multi-part recipe");
            if (mode == "channels" && ((*result)["fixture"] != "independent-channels" || (*result)["channels"].size() != 4 || !(*result)["sample_progress"].is_null()))
                return std::unexpected("the channel review did not use independent compiled property tracks");
            json review = (*result)["agent_review"];
            if (review["discovered_operations"] != 13 || review["typed_receipts"] != true) return std::unexpected("public control discovery and receipts were not exercised");
            if (review["authorization"] != "mlibs.control/openas" || review["reverse_replayed"] != 61) return std::unexpected("shared authority or the process replay is missing");
            for (const std::string_view key : {"read_only_preserved_state", "dry_run_preserved_state", "foreign_session_refused", "revoked_continuation_refused",
                                               "claimed_provenance_replaced", "nested_operation_filter_preserved"})
                if (review[std::string(key)] != true) return std::unexpected(std::format("the Agent control contract failed: {}", key));
            json& frames = review["frames"];
            std::set<std::string> distinct, kept;
            for (const auto& frame : frames) {
                distinct.insert(frame["pixel_fnv1a"].dump());
                if (frame.contains("kept_region_fnv1a")) kept.insert(frame["kept_region_fnv1a"].dump());
            }
            if (frames.size() != 61 || distinct.size() != 61) return std::unexpected("expected 61 distinct actual process frames");
            if ((mode == "parts" || mode == "channels") &&
                (std::ranges::any_of(frames, [](const json& frame) { return !frame.contains("kept_region_fnv1a"); }) || kept.size() != 1))
                return std::unexpected("the unselected part's actual pixels did not stay fixed throughout the process");
            for (std::size_t index = 0; index < frames.size(); ++index) {
                json& frame = frames[index];
                if (frame["index"] != index || frame["time_numerator"] != index || frame["time_denominator"] != 60)
                    return std::unexpected("the process frame's time provenance disagrees");
                if (frame["sample_time_seconds"].get<double>() != static_cast<double>(index) / 60.0) return std::unexpected("the rendered sample time differs from the requested rational time");
                const fs::path image = frame.value("png", "");
                const auto size = png_size(mcpplibs::os::fs::read(image).value_or(""));
                if (!size || *size != std::pair<std::uint32_t, std::uint32_t>{320, 240}) return std::unexpected("a process frame is not the actual viewport PNG");
                frame["png_sha256"] = digest(image);
            }
            if (digest(fs::path(frames[30].value("png", ""))) != digest(png)) return std::unexpected("the Agent's midpoint differs from the same native renderer's output");
            json kept_result = *result;
            kept_result["agent_review"] = review;
            reviews.push_back(kept_result);
        }
        const fs::path denied = probe.artifacts / (profile + "-agent-denied.png");
        auto denied_text = run(profile + "-agent-denied", {"run", "--locked", "--profile", profile, "--", "--out", text_of(denied), "--agent-review", "--agent-deny-capture"}, 3);
        if (!denied_text) return std::unexpected(denied_text.error());
        if (std::ranges::none_of(json_lines(*denied_text, true), [](const json& row) { return row.value("error", "") == "Agent capture refused" && row["ok"] == false; }))
            return std::unexpected("the capture's denial did not return its structured failure");
        std::error_code error;
        for (const auto& entry : fs::directory_iterator(probe.artifacts, error))
            if (text_of(entry.path().filename()).starts_with(text_of(denied.filename()))) return std::unexpected("a refused visual query published pixels or provenance");
        const fs::path invalid = probe.artifacts / (profile + "-invalid.png");
        if (auto done = run(profile + "-invalid-region", {"run", "--locked", "--profile", profile, "--", "--out", text_of(invalid), "--region", "319,0,2,240"}, 2); !done)
            return std::unexpected(done.error());
        if (fs::exists(invalid)) return std::unexpected("an invalid crop published an image");
        if (auto done = run(profile + "-metadata", {"build", "--locked", "--profile", profile, "--configure-only"}); !done) return std::unexpected(done.error());
        auto audited = play_audit(probe, profile, openas);
        if (!audited) return std::unexpected(audited.error());
        json record{{"profile", profile}, {"runs", results}, {"agent_reviews", reviews}, {"denied_capture_published_no_files", true}, {"display_environment_removed", true}};
        record.update(*audited);
        probe.report["profiles"].push_back(record);
        if (digest(probe.project / "mcpp.lock") != temporary_lock) return std::unexpected("locked runs changed the temporary project's resolution");
    }
    // The tests in a second real consumer, so the program's resolution and compile database hold no test program.
    const fs::path integration = probe.project / "integration";
    fs::create_directories(integration / "src");
    write_text(integration / "mcpp.toml", manifest);
    copy_file_to(fixture / "src/main.cpp", integration / "src/main.cpp");
    std::error_code error;
    fs::copy(fixture / "tests", integration / "tests", fs::copy_options::recursive | fs::copy_options::overwrite_existing, error);
    for (const std::string profile : {"dev", "release"})
        if (auto done = run(profile + "-integration-tests", {"test", "--profile", profile}, 0, profile == "dev", integration); !done) return std::unexpected(done.error());
    return {};
}

// ---- omui-hanim ------------------------------------------------------------------------------------------------

struct Active {
    Metadata metadata;
    json commands;  // the compile commands the build actually runs
    fs::path ninja;
};

// The metadata of the requested profile and target (the IDE database includes inactive units: the build's own DAG
// decides), found by what the consumer's own unit is compiled with, not by modification times.
std::expected<Active, std::string> active_metadata(const fs::path& project, std::string_view profile) {
    const std::string optimization = profile == "dev" ? "-O0" : profile == "release" ? "-O2" : "-O1";
    std::vector<Active> matches;
    std::error_code error;
    for (const auto& platform : fs::directory_iterator(project / "target", error))
        for (const auto& build : fs::directory_iterator(platform.path(), error)) {
            const fs::path resolution = build.path() / "resolution.json";
            if (!fs::is_regular_file(resolution, error)) continue;
            const json data = json::parse(mcpplibs::os::fs::read(resolution).value_or(""), nullptr, false);
            const json entries = json::parse(mcpplibs::os::fs::read(build.path() / "compile_commands.json").value_or("[]"), nullptr, false);
            if (data.is_discarded() || entries.is_discarded()) continue;
            const auto main = std::ranges::find_if(entries, [&](const json& entry) { return real(fs::path(entry.value("file", ""))) == real(project / "src/main.cpp"); });
            if (main == entries.end()) continue;
            const bool web = data["toolchain"].value("triple", "").starts_with("wasm");
            if (std::ranges::contains((*main)["arguments"], json(optimization)) && web == (profile == "wasm-test"))
                matches.push_back({{resolution, build.path() / "compile_commands.json", data, entries}, json::array(), build.path() / "build.ninja"});
        }
    if (matches.size() != 1) return std::unexpected(std::format("ambiguous target/profile metadata: {}", profile));
    Active found = std::move(matches.front());
    const auto outputs = ninja_outputs(mcpplibs::os::fs::read(found.ninja).value_or(""));
    const fs::path directory = found.metadata.resolution.parent_path();
    for (const auto& entry : found.metadata.entries) {
        fs::path output = entry.value("output", "");
        if (output.is_absolute()) {
            if (!inside(output, directory)) continue;
            output = output.lexically_relative(directory);
        }
        if (outputs.contains(mcpplibs::os::fs::generic_utf8(output))) found.commands.push_back(entry);
    }
    if (found.commands.empty()) return std::unexpected("no active compile units found");
    return found;
}

Outcome omui_hanim(Probe& probe, bool web) {
    const fs::path fixture = probe.repo / "tools/ci/omui-hanim-consumer";
    fs::create_directories(probe.project / "src");
    std::string manifest = mcpplibs::os::fs::read(fixture / "mcpp.toml.in").value_or("");
    for (const auto& [key, relative] : std::initializer_list<std::pair<std::string_view, std::string_view>>{
             {"view", "modules/projects/omui/view"}, {"components", "modules/projects/omui/components/base"}, {"raster", "modules/projects/xrender/raster"},
             {"image", "modules/base/image"}, {"utils", "modules/base/utils"}, {"xrender_text", "modules/projects/xrender/text"}, {"hanim", "modules/projects/hanim"}}) {
        const std::string mark = std::format("{{{{{}_path}}}}", key), value = json(text_of(probe.repo / relative)).dump();
        for (std::size_t at = manifest.find(mark); at != std::string::npos; at = manifest.find(mark, at + value.size())) manifest.replace(at, mark.size(), value);
    }
    write_text(probe.project / "mcpp.toml", manifest);
    copy_file_to(fixture / "src/main.cpp", probe.project / "src/main.cpp");
    // Out-of-tree release programs cannot rely on debug source paths to find assets: the real fonts are given.
    const std::string fonts = text_of(probe.repo / "modules/content/text/fonts");
    const auto run = [&](std::string label, std::vector<std::string> argv, int expected = 0, bool bootstrap = false, std::string font_dir = {}) {
        return probe.run(label, {.argv = std::move(argv), .bootstrap = bootstrap, .expected = expected,
                                 .variables = {{"OMUI_FONTS", font_dir.empty() ? fonts : font_dir}}, .headless = true, .one_log = true});
    };
    if (auto done = run("bootstrap", {"build", "--profile", "dev"}, 0, true); !done) return std::unexpected(done.error());
    // The second platform resolved into this consumer's own lock before it is frozen.
    if (web)
        if (auto done = run("bootstrap-web", {"build", "--target", "wasm32-emscripten", "--profile", "wasm-test", "--configure-only"}, 0, true); !done)
            return std::unexpected(done.error());
    const json consumer_lock = digest(probe.project / "mcpp.lock");
    std::vector<std::pair<std::string, std::vector<std::string>>> cases{{"dev", {}}, {"release", {}}};
    if (web) cases.push_back({"wasm-test", {"--target", "wasm32-emscripten"}});
    constexpr std::array frame_kinds{"frames", "driver_frames", "effect_frames", "feedback_frames", "bridge_frames"};
    const auto save_active = [&](const Active& active, const std::string& name) {
        copy_file_to(active.metadata.resolution, probe.artifacts / (name + ".resolution.json"));
        copy_file_to(active.metadata.cdb, probe.artifacts / (name + ".compile_commands.json"));
        write_text(probe.artifacts / (name + ".active-compile_commands.json"), active.commands.dump(2) + "\n");
        copy_file_to(active.ninja, probe.artifacts / (name + ".build.ninja"));
    };
    for (const auto& [profile, target] : cases) {
        const fs::path images = probe.artifacts / profile;
        fs::create_directories(images);
        std::vector<std::string> argv{"run", "--locked", "--profile", profile};
        argv.insert(argv.end(), target.begin(), target.end());
        argv.insert(argv.end(), {"--", text_of(images)});
        auto text = run(profile, argv);
        if (!text) return std::unexpected(text.error());
        std::optional<json> result;
        for (const auto& row : json_lines(*text))
            if (!result && row.value("kind", "") == "omui.hanim-consumer") result = row;
        if (!result || !result->value("ok", false) ||
            std::ranges::any_of(frame_kinds, [&](const char* kind) { return !result->contains(kind) || (*result)[kind].size() != 61; }))
            return std::unexpected("the consumer did not complete the real task");
        for (const char* kind : frame_kinds)
            for (auto& frame : (*result)[kind]) {
                const fs::path png = frame.value("png", "");
                frame["png_sha256"] = digest(png);
                std::error_code error;
                if (!inside(real(png), real(images)) || fs::file_size(png, error) < 100) return std::unexpected("an invalid pixel artifact");
            }
        std::vector<std::string> metadata_argv{"build", "--locked", "--profile", profile};
        metadata_argv.insert(metadata_argv.end(), target.begin(), target.end());
        metadata_argv.push_back("--configure-only");
        if (auto done = run(profile + "-metadata", metadata_argv); !done) return std::unexpected(done.error());
        auto active = active_metadata(probe.project, profile);
        if (!active) return std::unexpected(active.error());
        std::vector<std::string> packages;
        for (const auto& package : active->metadata.data["graph"]["packages"])
            if (package.contains("instances") && std::ranges::contains(package["instances"], json("target"))) {
                const std::string canonical = package["package"].value("canonical", "");
                packages.push_back(canonical.substr(0, canonical.find('@')));
            }
        std::ranges::sort(packages);
        if (!std::ranges::contains(packages, "omui.view") || !std::ranges::contains(packages, "sunrisepeak.hanim") || std::ranges::contains(packages, "omui.sdk") ||
            std::ranges::contains(packages, "omui.site"))
            return std::unexpected("an unexpected application dependency closure");
        std::set<fs::path> sources;
        for (const auto& command : active->commands) sources.insert(source_of(command, probe.project));
        if (!sources.contains(probe.repo / "modules/projects/omui/view/src/hanim/bridge.cpp") || !sources.contains(probe.repo / "modules/projects/hanim/src/engine/engine.cpp"))
            return std::unexpected("the optional public adapter's or player's sources are absent");
        save_active(*active, profile);
        json hashes = json::object();
        for (const auto& source : sources)
            if (inside(source, probe.repo)) hashes[mcpplibs::os::fs::generic_utf8(source.lexically_relative(probe.repo))] = digest(source);
        probe.report["profiles"].push_back({{"profile", profile}, {"result", *result}, {"packages", packages}, {"source_files_sha256", hashes}});
        if (digest(probe.project / "mcpp.lock") != consumer_lock) return std::unexpected("the consumer's lock changed");
    }
    // The same pixels in every profile.
    const auto frames_of = [&](const json& profile) {
        json all = json::array();
        for (const char* kind : frame_kinds)
            for (const auto& frame : profile["result"][kind]) all.push_back(frame);
        return all;
    };
    const json reference = frames_of(probe.report["profiles"][0]);
    for (std::size_t at = 1; at < probe.report["profiles"].size(); ++at) {
        const json frames = frames_of(probe.report["profiles"][at]);
        for (std::size_t index = 0; index < std::min(reference.size(), frames.size()); ++index)
            if (reference[index]["pixel_fnv1a"] != frames[index]["pixel_fnv1a"] || reference[index]["png_sha256"] != frames[index]["png_sha256"])
                return std::unexpected("the profiles' process pixels differ");
    }
    // Missing resources are a failure, never a visual success.
    auto missing_text = run("missing-fonts", {"run", "--locked", "--profile", "release", "--", text_of(probe.artifacts / "release")}, 1, false, text_of(probe.project / "missing-fonts"));
    if (!missing_text) return std::unexpected(missing_text.error());
    const auto missing = json_lines(*missing_text);
    if (missing.empty() || missing.front().value("ok", true) || !missing.front()["frames"].empty() ||
        std::ranges::none_of(missing.front()["checks"], [](const json& check) { return check.value("name", "") == "font_resources_loaded" && !check.value("ok", true); }))
        return std::unexpected("missing resources were reported as a visual success");
    probe.report["missing_fonts_refused"] = true;
    // A second application in the same project: the adapter turned off removes its player's sources from the link.
    std::string without;
    std::string unfeatured = manifest;
    for (const std::string_view feature : {", features = [\"hanim\"]"})
        for (std::size_t at = unfeatured.find(feature); at != std::string::npos; at = unfeatured.find(feature)) unfeatured.erase(at, feature.size());
    for (auto part : std::views::split(std::string_view(unfeatured), '\n')) {
        const std::string_view line(part.begin(), part.end());
        if (!line.starts_with("sunrisepeak.hanim = ")) without += std::string(line) + "\n";
    }
    while (without.ends_with("\n\n")) without.pop_back();
    write_text(probe.project / "mcpp.toml", without);
    write_text(probe.project / "src/main.cpp", R"(import std;
import omui.view;
int main() {
    omui::Runtime runtime;
    runtime.mount("main",omui::box({.width=20,.height=20}));
    const auto frame=runtime.frame({hanim::seconds(0),{}});
    if(!frame.failure.empty())return 1;
    std::println("omui.view-without-player: ok");
}
)");
    const auto restore = [&] {
        write_text(probe.project / "mcpp.toml", manifest);
        copy_file_to(fixture / "src/main.cpp", probe.project / "src/main.cpp");
    };
    auto plain = run("without-player", {"run", "--locked", "--profile", "dev"});
    if (!plain || !plain->contains("omui.view-without-player: ok")) {
        restore();
        return std::unexpected(plain ? "the feature-off application did not run" : plain.error());
    }
    if (auto done = run("without-player-metadata", {"build", "--locked", "--profile", "dev", "--configure-only"}); !done) {
        restore();
        return std::unexpected(done.error());
    }
    auto active = active_metadata(probe.project, "dev");
    if (!active) {
        restore();
        return std::unexpected(active.error());
    }
    // Every view has the host's one Engine (D-126); the player adapter, vector rendering and control stay optional.
    if (std::ranges::any_of(active->commands, [](const json& command) {
            const std::string file = command.value("file", "");
            return file.contains("/view/src/hanim/") || file.contains("/hanim/src/render/vector/") || file.contains("/hanim/src/control/");
        })) {
        restore();
        return std::unexpected("the optional adapter or player leaked into an ordinary view build");
    }
    save_active(*active, "without-player");
    copy_file_to(probe.project / "src/main.cpp", probe.artifacts / "without-player.cpp");
    copy_file_to(probe.project / "mcpp.toml", probe.artifacts / "without-player.mcpp.toml");
    probe.report["feature_off"] = {{"ok", true}, {"compiled_units", active->commands.size()}, {"adapter_and_engine_absent", true},
                                   {"source_sha256", digest(probe.project / "src/main.cpp")}};
    const bool held = digest(probe.project / "mcpp.lock") == consumer_lock;
    restore();
    if (!held) return std::unexpected("the feature-off consumer's lock changed");
    return {};
}

// ---- hanim-features --------------------------------------------------------------------------------------------

constexpr std::string_view core_body = R"(
    Motion<Vec2> move({0,0});
    move.tween_to(seconds(0), {40,20}, seconds(1), ease::linear);
    require(move.value(seconds(.5)) == Vec2{20,10});
    require(move.value(seconds(0)) == Vec2{0,0});
)";
constexpr std::string_view effect_body = R"(
    auto effects = take(PreparedEffects::prepare(fx::chain({fx::blur(0,8)})));
    std::array<float,1> values{};
    take(effects.sample(.5, values)); require(values[0] == 4);
    take(effects.sample(0, values)); require(values[0] == 0);
)";
constexpr std::string_view object_body = R"(
    const std::array path_verbs{mlibs::geom::PathVerb::move,mlibs::geom::PathVerb::line,mlibs::geom::PathVerb::close};
    const std::array path_points{mlibs::geom::Point{0,0},mlibs::geom::Point{1,1}};
    auto imported=take(import_path({path_verbs,path_points}));
    require(imported.contours.size()==1 && imported.contours[0].geometry.segment_count()==1);
    auto objects = take(ObjectRegistry::make());
    auto a = take(objects.create({{{.id="item"}}}));
    auto b = take(objects.create({{{.id="item",.local={.tx=40}}}}));
    auto from = take(objects.capture(a)), to = take(objects.capture(b));
    require(from.parts()[0].local.tx == 0 && to.parts()[0].local.tx == 40);
)";
constexpr std::string_view library_body = R"(
    auto morph = take(PreparedObjectAnimation::prepare(from,to));
    std::vector<FramePart> parts(morph.requirements().parts);
    std::vector<ShapeCubic> cubics(morph.requirements().cubics);
    auto frame = take(morph.sample(.5,{parts,cubics}));
    require(frame.parts[0].local.tx == 20);
)";
constexpr std::string_view program_body = R"(
    auto program = take(PreparedProgram::prepare(anim::morph(from,to).duration(seconds(1))));
    auto current = take(program.sample(0,seconds(.5),{parts,cubics},{}));
    require(current.geometry.parts[0].local.tx == 20);
)";
constexpr std::string_view engine_body = R"(
    auto engine = take(Engine::make());
    auto playback = take(engine.play(anim::morph(from,to).duration(seconds(1))));
    take(engine.seek(playback,seconds(.5)));
    require(take(engine.frame(playback)).geometry.parts[0].local.tx == 20);
    take(engine.release(playback));
)";
constexpr std::string_view vector_body = R"(
    auto renderer = take(vector_render::PreparedVector::bind(morph));
    require(renderer.requirements().world_transforms == morph.requirements().parts);
)";
constexpr std::string_view control_body = R"(
    auto access=std::make_shared<mlibs::control::Access>(mlibs::control::policy_for_profile("dev"));
    mlibs::control::InvocationContext context;
    hanim::control::Controller controller(engine,access,context);
    mlibs::control::Invocation request{.remote=true,.access=access,.session=access->open_session()};
    const auto reply=controller.list(request);
    require(reply && reply->executed && reply->value && reply->value->empty());
    require(hanim::control::operations().size()==13);
    const auto prepared_control=controller.prepare(request,anim::morph(from,to));
    require(prepared_control && prepared_control->value);
    const auto started=controller.play(request,*prepared_control->value);
    require(started && started->executed && started->value);
    const auto moved=controller.seek_progress(request,*started->value,.5);
    const auto observed=controller.inspect(request,*started->value);
    require(moved && moved->executed && observed && observed->value && observed->value->position==seconds(.5));
    auto preview=request;preview.dry_run=true;
    const auto dry=controller.seek_progress(preview,*started->value,.75);
    require(dry && !dry->executed && take(engine.inspect(*started->value)).position==seconds(.5));
    require(access->revoke_session(*request.session));
    const auto denied=controller.inspect(request,*started->value);
    require(!denied && denied.error().code==hanim::control::Error::admission);
    const auto refused_plan=controller.play(request,*prepared_control->value);
    require(!refused_plan && refused_plan.error().code==hanim::control::Error::admission);
)";

Outcome hanim_features(Probe& probe) {
    const fs::path product = probe.repo / "modules/projects/hanim";
    const std::set<std::string> all_roles{"object", "effect", "library", "program", "engine", "render/vector", "control"};
    const auto without = [&](std::initializer_list<std::string> removed) {
        std::set<std::string> out = all_roles;
        for (const auto& role : removed) out.erase(role);
        return out;
    };
    struct Case {
        std::string name;
        std::optional<std::vector<std::string>> features;  // none: the defaults
        std::set<std::string> roles;
        std::string body;
    };
    const auto join = [](std::initializer_list<std::string_view> parts) {
        std::string out;
        for (const auto part : parts) out += part;
        return out;
    };
    const std::vector<Case> cases{
        {"core", std::vector<std::string>{}, {}, join({core_body})},
        {"effect", std::vector<std::string>{"effect"}, {"effect"}, join({core_body, effect_body})},
        {"object", std::vector<std::string>{"object"}, {"object"}, join({core_body, object_body})},
        {"library", std::vector<std::string>{"library"}, {"object", "library"}, join({core_body, object_body, library_body})},
        {"program", std::vector<std::string>{"program"}, {"object", "effect", "library", "program"}, join({core_body, object_body, library_body, effect_body, program_body})},
        {"default", std::nullopt, without({"render/vector", "control"}), join({core_body, object_body, library_body, effect_body, program_body, engine_body})},
        {"render-vector", std::vector<std::string>{"render-vector"}, without({"engine", "control"}), join({core_body, object_body, library_body, program_body, vector_body})},
        {"control", std::vector<std::string>{"control"}, without({"render/vector"}),
         join({core_body, object_body, library_body, effect_body, program_body, engine_body, control_body})},
    };
    const fs::path work = probe.project;
    const auto run = [&](const fs::path& project, std::string label, std::vector<std::string> argv, bool bootstrap = false) {
        return probe.run(label, {.argv = std::move(argv), .bootstrap = bootstrap, .cwd = project, .headless = true, .one_log = true});
    };
    for (const auto& item : cases) {
        const fs::path project = work / item.name;
        fs::create_directories(project / "src");
        std::string selection;
        if (item.features) selection = ", default-features = false, features = " + json(*item.features).dump();
        write_text(project / "mcpp.toml", "[package]\nnamespace=\"hanim.validation\"\nname=\"feature_consumer\"\nversion=\"0.1.0\"\n"
                                          "[targets.consumer]\nkind=\"bin\"\nmain=\"src/main.cpp\"\n[build]\nsources=[\"src/*.cpp\"]\n"
                                          "[dependencies]\nsunrisepeak.hanim = { path = " + json(text_of(product)).dump() + selection + " }\n");
        write_text(project / "src/main.cpp", "import std;\nimport hanim;\nusing namespace hanim;\nvoid require(bool value) { if (!value) std::exit(1); }\n"
                                             "template<class T, class E> T take(std::expected<T,E> value) {\n    require(value.has_value());\n"
                                             "    if constexpr (!std::is_void_v<T>) return std::move(*value);\n}\nint main() {\n" +
                                                 item.body + "\nstd::println(\"hanim feature consumer passed\");\n}\n");
        if (auto done = run(project, "bootstrap", {"run", "--profile", "release"}, true); !done) return std::unexpected(done.error());
        const json own_lock = digest(project / "mcpp.lock");
        auto repeat = run(project, "repeat", {"run", "--profile", "release", "--locked"});
        if (!repeat) return std::unexpected(repeat.error());
        if (!repeat->contains("hanim feature consumer passed")) return std::unexpected("the consumer did not execute");
        if (auto done = run(project, "metadata", {"build", "--profile", "release", "--locked", "--configure-only"}); !done) return std::unexpected(done.error());
        auto metadata = newest_metadata(project);
        if (!metadata) return std::unexpected(metadata.error());
        std::set<std::string> packages;
        for (const auto& package : metadata->data["graph"]["packages"])
            if (package.contains("instances") && std::ranges::contains(package["instances"], json("target"))) {
                const std::string canonical = package["package"].value("canonical", "");
                packages.insert(canonical.substr(0, canonical.find('@')));
            }
        const auto has_prefix = [&](std::initializer_list<std::string_view> prefixes) {
            return std::ranges::any_of(packages, [&](const std::string& name) {
                return std::ranges::any_of(prefixes, [&](std::string_view prefix) { return name.starts_with(prefix); });
            });
        };
        if (has_prefix({"omui.ui.", "omui.host."}) ||
            std::ranges::any_of(std::initializer_list<std::string_view>{"omui.core", "omui.sdk", "mlibs.testing", "openlifa.openmui"},
                                [&](std::string_view name) { return packages.contains(std::string(name)); }))
            return std::unexpected("a UI or test dependency in the public product");
        if (item.name != "render-vector" && has_prefix({"openmui.render", "xrender", "mlibs.rhi", "xrender.raster", "mlibs.shader"}))
            return std::unexpected("the renderer enabled without opting in");
        if ((item.name == "control") != packages.contains("mlibs.control")) return std::unexpected("the control dependency did not follow the selected feature");
        // hanim.core measures its contours with mlibs.geom; the engine's solver is mlibs.dynamics (D-131, D-133, D-231).
        if (item.name == "core" &&
            packages != std::set<std::string>{"hanim.validation.feature_consumer", "sunrisepeak.hanim", "hanim.core", "mlibs.math", "mlibs.utils", "mlibs.memory", "mlibs.geom"})
            return std::unexpected("the core feature's closure is not minimal");
        if (!packages.contains("mlibs.geom")) return std::unexpected("the shared geometry (the measures of hanim.core and the object feature) is missing");
        // The engine's solver and its timeline come with the engine feature alone (feature-deps, kept since mcpp 2026.11.7.1, #897).
        const bool engine = item.roles.contains("engine");
        if (engine != packages.contains("mlibs.dynamics") || engine != packages.contains("mlibs.simulation"))
            return std::unexpected("the engine solver's dependency did not follow the engine feature");
        std::set<fs::path> sources;
        for (const auto& entry : metadata->entries) sources.insert(source_of(entry, project));
        std::set<fs::path> expected{product / "src/hanim.cppm"};
        std::error_code error;
        for (const auto& role : item.roles)
            for (const auto& file : fs::directory_iterator(product / "src" / role, error))
                if (file.path().extension() == ".cpp" || file.path().extension() == ".cppm") expected.insert(file.path());
        std::set<fs::path> actual;
        for (const auto& source : sources)
            if (inside(source, product / "src")) actual.insert(source);
        if (actual != expected) return std::unexpected("the feature's source closure differs: " + item.name);
        const auto selected = std::ranges::find_if(metadata->data["graph"]["packages"], [](const json& package) {
            return package["package"].value("canonical", "").starts_with("sunrisepeak.hanim@");
        });
        if (selected == metadata->data["graph"]["packages"].end()) return std::unexpected("no selected hanim package");
        const fs::path exports = fs::path((*selected).value("outDir", "")) / "selected.cppm";
        std::set<std::string> expected_exports{"export import hanim.core;"};
        for (std::string role : item.roles) {
            std::ranges::replace(role, '/', '.');
            expected_exports.insert("export import hanim." + role + ";");
        }
        std::set<std::string> actual_exports;
        for (auto part : std::views::split(std::string_view(mcpplibs::os::fs::read(exports).value_or("")), '\n')) {
            const std::string_view line(part.begin(), part.end());
            if (line.starts_with("export import ")) actual_exports.emplace(line);
        }
        if (actual_exports != expected_exports) return std::unexpected("the public feature exports differ");
        copy_file_to(metadata->resolution, probe.artifacts / (item.name + ".resolution.json"));
        copy_file_to(metadata->cdb, probe.artifacts / (item.name + ".compile_commands.json"));
        copy_file_to(exports, probe.artifacts / (item.name + ".selected.cppm"));
        copy_file_to(project / "src/main.cpp", probe.artifacts / (item.name + ".cpp"));
        if (digest(project / "mcpp.lock") != own_lock) return std::unexpected("the consumer's lock changed");
        json product_sources = json::array();
        for (const auto& source : actual) product_sources.push_back(mcpplibs::os::fs::generic_utf8(source.lexically_relative(probe.repo)));
        probe.report["cases"].push_back({{"name", item.name}, {"features", item.features ? json(*item.features) : json(nullptr)}, {"packages", packages},
                                         {"product_sources", product_sources}, {"public_exports", expected_exports}});
    }
    // The optional control path also runs without a renderer on the Web, with no file-system or browser shim.
    const fs::path web = work / "control";
    {
        std::ofstream append(web / "mcpp.toml", std::ios::binary | std::ios::app);
        append << "\n[target.'cfg(os = \"emscripten\")'.abi]\nexceptions=true\nthreads=false\n";
    }
    if (auto done = run(web, "web-bootstrap", {"build", "--target", "wasm32-emscripten", "--profile", "release", "--configure-only"}, true); !done)
        return std::unexpected(done.error());
    const json web_lock = digest(web / "mcpp.lock");
    auto web_run = run(web, "web-run", {"run", "--target", "wasm32-emscripten", "--profile", "release", "--locked"});
    if (!web_run) return std::unexpected(web_run.error());
    if (!web_run->contains("hanim feature consumer passed")) return std::unexpected("the Web control consumer did not execute");
    if (digest(web / "mcpp.lock") != web_lock) return std::unexpected("the Web control consumer's lock changed");
    probe.report["web_control"] = {{"ok", true}, {"target", "wasm32-emscripten"}, {"source_sha256", digest(web / "src/main.cpp")}, {"manifest_sha256", digest(web / "mcpp.toml")}};
    // The packed product, linked by a consumer without its implementation sources.
    auto packed = run(probe.repo, "pack", {"pack", "-p", "sunrisepeak.hanim", "--profile", "release", "--locked", "--message-format", "json"});
    if (!packed) return std::unexpected(packed.error());
    std::vector<json> envelopes;
    for (const auto& value : json_objects(*packed))
        if (value.is_object() && value.value("kind", "") == "mcpp.pack") envelopes.push_back(value);
    if (envelopes.size() != 1) return std::unexpected("missing a unique pack envelope");
    const fs::path archive = envelopes.front()["data"]["artifacts"][0].value("path", "");
    const fs::path installation = work / "installation";
    fs::create_directories(installation);
    process::spawn_options untar;
    untar.program = "tar";
    untar.args = {"-xf", text_of(archive), "-C", text_of(installation), "--no-same-owner"};
    if (const auto done = process::run(std::move(untar)); !done || done->status.shell_code() != 0) return std::unexpected("the archive could not be unpacked");
    std::vector<fs::path> manifests;
    bool implementation = false;
    std::error_code error;
    for (auto it = fs::recursive_directory_iterator(installation, error); it != fs::recursive_directory_iterator(); it.increment(error)) {
        if (it->path().filename() == "mcpp.toml") manifests.push_back(it->path());
        if (it->path().extension() == ".cpp") implementation = true;
    }
    if (manifests.size() != 1) return std::unexpected("an unexpected packed manifest set");
    const fs::path installed = manifests.front().parent_path();
    if (implementation) return std::unexpected("the archive contains implementation sources");
    const fs::path project = work / "packed";
    fs::create_directories(project / "src");
    std::string manifest = "[package]\nnamespace=\"hanim.validation\"\nname=\"packed_consumer\"\nversion=\"0.1.0\"\n"
                           "[targets.consumer]\nkind=\"bin\"\nmain=\"src/main.cpp\"\n[build]\nsources=[\"src/*.cpp\"]\n"
                           "[dependencies]\n\"hanim\" = { path = " + json(text_of(installed)).dump() + " }\n";
    for (const auto& [name, relative] : std::initializer_list<std::pair<std::string_view, std::string_view>>{
             {"hanim.core", "projects/hanim/core"}, {"mlibs.geom", "base/geom"}, {"mlibs.memory", "base/memory"}, {"mlibs.utils", "base/utils"},
             {"mlibs.dynamics", "base/dynamics"}, {"mlibs.simulation", "base/simulation"}})
        manifest += json(name).dump() + " = { path = " + json(text_of(probe.repo / "modules" / relative)).dump() + " }\n";
    write_text(project / "mcpp.toml", manifest);
    copy_file_to(probe.repo / "tools/ci/hanim-api-probe/src/main.cpp", project / "src/main.cpp");
    auto text = run(project, "bootstrap", {"run", "--profile", "release"}, true);
    if (!text) return std::unexpected(text.error());
    std::optional<json> result;
    for (const auto& row : json_lines(*text))
        if (!result && row.value("kind", "") == "hanim.api-probe") result = row;
    if (!result || !result->value("ok", false) || (*result)["checks"].empty() ||
        !std::ranges::all_of((*result)["checks"], [](const json& check) { return check.value("ok", false); }))
        return std::unexpected("the packed public authoring checks failed");
    if (auto done = run(project, "repeat", {"run", "--profile", "release", "--locked"}); !done) return std::unexpected(done.error());
    if (auto done = run(project, "metadata", {"build", "--profile", "release", "--locked", "--configure-only"}); !done) return std::unexpected(done.error());
    auto metadata = newest_metadata(project);
    if (!metadata) return std::unexpected(metadata.error());
    for (const auto& entry : metadata->entries) {
        const fs::path source = source_of(entry, project);
        // hanim.core lives inside the product's directory as a package of its own.
        if (inside(source, product) && !inside(source, product / "core")) return std::unexpected("the packed consumer compiled the original product's sources");
    }
    copy_file_to(metadata->resolution, probe.artifacts / "packed.resolution.json");
    copy_file_to(metadata->cdb, probe.artifacts / "packed.compile_commands.json");
    std::vector<std::string> interfaces;
    for (auto it = fs::recursive_directory_iterator(installed, error); it != fs::recursive_directory_iterator(); it.increment(error))
        if (it->path().extension() == ".cppm") interfaces.push_back(mcpplibs::os::fs::generic_utf8(it->path().lexically_relative(installed)));
    std::ranges::sort(interfaces);
    probe.report["packed"] = {{"archive", text_of(archive)}, {"sha256", digest(archive)}, {"bytes", fs::file_size(archive, error)},
                              {"checks", (*result)["checks"].size()}, {"result", *result}, {"installed_interfaces", interfaces}};
    return {};
}

}  // namespace

std::span<const std::string_view> names() { return probe_names; }

std::optional<std::pair<std::uint32_t, std::uint32_t>> png_size(std::string_view bytes) {
    constexpr std::string_view signature{"\x89PNG\r\n\x1a\n", 8};
    if (bytes.size() < 24 || bytes.substr(0, 8) != signature) return std::nullopt;
    const auto word = [&](std::size_t at) {
        return std::uint32_t{static_cast<unsigned char>(bytes[at])} << 24 | std::uint32_t{static_cast<unsigned char>(bytes[at + 1])} << 16 |
               std::uint32_t{static_cast<unsigned char>(bytes[at + 2])} << 8 | static_cast<unsigned char>(bytes[at + 3]);
    };
    return std::pair{word(16), word(20)};
}

std::set<std::string> ninja_outputs(std::string_view text) {
    std::set<std::string> out;
    for (auto part : std::views::split(text, '\n')) {
        const std::string_view line(part.begin(), part.end());
        if (!line.starts_with("build ")) continue;
        const std::string_view targets = line.substr(6, line.find(':') == std::string_view::npos ? std::string_view::npos : line.find(':') - 6);
        for (auto word : std::views::split(targets, ' '))
            if (!std::string_view(word.begin(), word.end()).empty()) out.emplace(word.begin(), word.end());
    }
    return out;
}

int probe(const fs::path& root, std::string_view name, const Options& options) {
    const auto output_of = [&](std::string_view fallback) { return options.output.empty() ? root / fallback : options.output; };
    if (name == "hanim-core") {
        Probe probe(root, output_of("target/hanim-core-consumer/report.xdef.json"), "hanim-public-core-consumer-");
        probe.report = {{"format", "hanim.independent-core-consumer@0.1"}, {"ok", false}, {"project", text_of(probe.project)}, {"commands", json::array()},
                        {"runs", json::array()}, {"fixture_sha256", digest(probe.repo / "tools/ci/hanim-core-consumer/src/main.cpp")},
                        {"source_lock_sha256_before", probe.lock_before},
                        {"limits", {"Public core geometry/timeline/log evidence only; not HObject/Engine/A1 or pixel-domain acceptance.",
                                    "No renderer, UI, spec, testing, site, host or GPU is consumed.", "No new performance, allocation or physical-GPU claim."}}};
        return probe.finish(hanim_core(probe), options.keep_project, "source_lock_sha256_after", {{"profiles", 2}});
    }
    if (name == "hanim-api") {
        Probe probe(root, output_of("target/hanim-public-authoring/report.xdef.json"), "hanim-api-probe-");
        const fs::path fixture = probe.repo / "tools/ci/hanim-api-probe";
        json hashes{{"mcpp.toml.in", digest(fixture / "mcpp.toml.in")}};
        std::error_code error;
        for (const auto& file : fs::directory_iterator(fixture / "src", error)) hashes["src/" + text_of(file.path().filename())] = digest(file.path());
        probe.report = {{"format", "hanim.public-authoring@0.2"}, {"ok", false}, {"source_root", text_of(probe.repo)}, {"source_commit", git_head(probe.repo)},
                        {"project", text_of(probe.project)}, {"reused_project", false}, {"source_lock_before", probe.lock_before}, {"commands", json::array()},
                        {"profiles", json::array()}, {"fixture_sha256", hashes},
                        {"scope", {"Public import hanim; no consumer facade or second compiler/player.", "Real strict contour/transform/fill morph and parameter effects only.",
                                   "ObjectRef uses borrowed Engine registry; external hosts use explicit Snapshot inputs.",
                                   "Explicit nested composite scopes, hierarchy and atomic program frames; no renderer required.",
                                   "Public property recipes, independent channel timing and shared-path capture are included; GPU execution, general channel blending and a generic Agent provider remain outside this probe.",
                                   "No hot-path performance/allocation or sanitizer claim."}}};
        Outcome outcome = hanim_api(probe);
        const json profiles = probe.report["profiles"].size();
        return probe.finish(std::move(outcome), options.keep_project, "source_lock_after", {{"profiles", profiles}});
    }
    if (name == "hanim-play") {
        Probe probe(root, output_of("target/hanim-play-consumer/report.xdef.json"), "hanim-public-play-consumer-");
        const fs::path fixture = probe.repo / "tools/ci/hanim-play-consumer";
        const fs::path contract = probe.repo / "modules/projects/hanim/visual/tests/visual/shape_morph_test.cpp";
        probe.report = {
            {"format", "hanim.independent-play-consumer@0.2"},
            {"ok", false},
            {"project", text_of(probe.project)},
            {"source_commit", git_head(probe.repo)},
            {"fixture_sha256", digest(fixture / "src/main.cpp")},
            {"source_lock_before", probe.lock_before},
            {"openas_package", options.openas.empty() ? json(nullptr) : json{{"path", text_of(options.openas)}, {"manifest_sha256", digest(options.openas / "mcpp.toml")}}},
            {"manifest_sha256", digest(fixture / "mcpp.toml.in")},
            {"raster_contract", {{"source", text_of(contract) + ":95"}, {"source_sha256", digest(contract)}, {"max_boundary_coverage_u8", 18}, {"off_boundary_changes", 0},
                                 {"coverage_total_u8_budget", 8192},
                                 {"basis", "Existing 1/32-device-pixel flattening error per representation; independent exact subdivision may differ by 1/16 pixel. "
                                           "Boundary-only coverage tolerance; uniform interior/exterior exact."}}},
            {"commands", json::array()},
            {"profiles", json::array()},
            {"limits", {"Fixed triangle/circle/diamond fixture; not a general asset editor or complete Engine acceptance.",
                        "CPU reference pixels only; no GUI, window, physical GPU performance or realtime aesthetic claim.",
                        "Native hanim consumer uses shared authorization/context; no generic provider registry or new wire protocol.",
                        "No sanitizer claim: this gate runs dev and release."}}};
        if (!options.openas.empty()) {
            bool implementation = false;
            std::error_code error;
            for (auto it = fs::recursive_directory_iterator(options.openas, error); it != fs::recursive_directory_iterator(); it.increment(error))
                implementation = implementation || it->path().extension() == ".cpp";
            if (!fs::is_regular_file(options.openas / "mcpp.toml") || implementation) {
                std::cerr << "omui-devtools consumer hanim-play: --openas-package must be an unpacked binary artifact, not an implementation source checkout\n";
                return 2;
            }
        }
        Outcome outcome = hanim_play(probe, options.openas.empty() ? fs::path{} : real(options.openas));
        const json profiles = probe.report["profiles"].size();
        return probe.finish(std::move(outcome), options.keep_project, "source_lock_after", {{"profiles", profiles}});
    }
    if (name == "omui-hanim") {
        Probe probe(root, output_of("target/omui-hanim-consumer/report.xdef.json"), "omui-hanim-consumer-");
        const fs::path fixture = probe.repo / "tools/ci/omui-hanim-consumer";
        probe.report = {{"format", "omui.hanim-presentation@0.1"},
                        {"ok", false},
                        {"project", text_of(probe.project)},
                        {"reused_project", false},
                        {"base_commit", git_head(probe.repo)},
                        {"source_lock_before", probe.lock_before},
                        {"commands", json::array()},
                        {"profiles", json::array()},
                        {"fixture_sha256", {{"mcpp.toml.in", digest(fixture / "mcpp.toml.in")}, {"src/main.cpp", digest(fixture / "src/main.cpp")}}},
                        {"scope", {"Local presentation offsets and reusable host transport; public Engine and TimeMap remain the execution/time owners.",
                                   "Actual standard button, shaped text, mouse and Agent semantic activation without GUI/display server.",
                                   "Projected target anchors follow in the current frame; complete sampled-layout constraints remain out of scope.",
                                   "Host controls, visibility/interaction/preferences, frame demand and arbitrary clock sampling are exercised without a window.",
                                   "Single-object leaf/nested image scopes affect real UI pixels, with atomic publication, provider reload and exact reverse sampling.",
                                   "No claim of complete UI geometry capture, 3D or arbitrary cross-object image scopes."}}};
        Outcome outcome = omui_hanim(probe, options.web);
        // A failed run keeps its project for a look.
        return probe.finish(std::move(outcome), options.keep_project || !probe.report.value("error", std::string{}).empty(), "source_lock_after", json::object());
    }
    if (name == "hanim-features") {
        const fs::path output = output_of("target/hanim-features/report.xdef.json");
        Probe probe(root, output, "hanim-feature-consumer-", real(output).parent_path() / "artifacts");
        probe.report = {{"ok", false}, {"format", "hanim.features@0.1"}, {"project", text_of(probe.project)}, {"source_lock_before", probe.lock_before},
                        {"cases", json::array()}, {"commands", json::array()},
                        {"limits", {"Native release consumers; no clean-machine timing or GPU performance claim.",
                                    "The default archive is tested with explicit source dependencies: mcpp pack omits local path-dependency edges.",
                                    "The archive freezes selected features; this is not registry publication or binary feature reselection."}}};
        Outcome outcome = hanim_features(probe);
        const json count = probe.report["cases"].size();
        return probe.finish(std::move(outcome), options.keep_project, "source_lock_after", {{"cases", count}});
    }
    std::cerr << std::format("omui-devtools consumer: no probe {} (hanim-core, hanim-api, hanim-play, omui-hanim, hanim-features)\n", name);
    return 2;
}

}  // namespace omui::devtools::consumer
