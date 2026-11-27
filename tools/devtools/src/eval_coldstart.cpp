// eval cold-start (D-186, D-192; the foundation design's cold-start trials made repeatable): a fresh agent session
// that knows nothing of the project — no skill, no files to read — is given one program it may run and one task in
// words. The program has to teach itself: `subjects`, `describe`, the tasks and examples, the ways out of every
// error. The outcome is checked afterwards through the same program, as requests on the state the agent left
// (`lab.history`'s actor, a material's row, the scene), never on the agent's word; the agent's last message is
// held only to what the task asks it to say. Each trial is a line of coldstart.eval.xdef.jsonl: success, the
// commands it ran, its turns, its tokens (fresh apart from cache reads), how long.
//
// A set (tools/eval/coldstart/<set>.coldstart.xdef.json):
//   package, program      what is built (release) and run
//   setup                 the program's arguments before the session, each run once ({dir}: the trial's directory;
//                         {root}: the repository's)
//   wrapper {name, args}  what the agent may run: ./<name>, the program with these arguments ({dir})
//   wrapper {name, args, words: true}  or: ./<name> WORDS… is the program's own command line — these arguments, then
//                         the agent's words as they are (a tool's `subjects`, `describe …`, `<operation> --name value`)
//   wrapper {name, host, base?}  or: ./<name> PATH… prints what a static host of the directory `host` ({dir}) sends
//                         for each path — a file's text, a directory's index.html, `404 not found` — as an agent that
//                         only fetches meets a site at its address; `base` is the address the site says it is served
//                         at (its llms.txt and sitemap write whole addresses): a path given whole from there is read
//                         as its path. The program is not run by the agent then (only by `setup`).
//   intro                 the one paragraph the agent is told about the program
//   check_args            the program's arguments that answer a check's request ({dir})
//   tasks [{id, goal, answer?, checks [{request, at, parse?, then?, equals|contains|above|below|exists}]}]
module omui.devtools.eval;

import std;
import mcpplibs.os;
import nlohmann.json;
import omui.devtools.run;

namespace omui::devtools::eval {

namespace {

namespace fs = std::filesystem;
namespace os = mcpplibs::os;
using nlohmann::json;
using nlohmann::ordered_json;

// A record keeps the start or the end of a long text, whole characters of it: a cut inside a character is not
// UTF-8, and the record is JSON.
bool continues(char byte) { return (static_cast<unsigned char>(byte) & 0xC0) == 0x80; }
std::string first_of(std::string_view text, std::size_t bytes) {
    if (text.size() <= bytes) return std::string(text);
    while (bytes > 0 && continues(text[bytes])) --bytes;
    return std::string(text.substr(0, bytes));
}
std::string last_of(std::string_view text, std::size_t bytes) {
    if (text.size() <= bytes) return std::string(text);
    std::size_t from = text.size() - bytes;
    while (from < text.size() && continues(text[from])) ++from;
    return std::string(text.substr(from));
}

std::string placed(std::string text, const fs::path& directory, const fs::path& root) {
    for (std::size_t at = 0; (at = text.find("{dir}", at)) != std::string::npos;) text.replace(at, 5, os::fs::utf8(directory));
    // The repository's root whole: a trial runs in a directory of its own.
    const std::string top = root.empty() ? std::string() : os::fs::utf8(fs::absolute(root).lexically_normal());
    for (std::size_t at = 0; (at = text.find("{root}", at)) != std::string::npos;) text.replace(at, 6, top);
    return text;
}

std::vector<std::string> arguments_of(const json& listed, const fs::path& directory, const fs::path& root = {}) {
    std::vector<std::string> out;
    for (const auto& arg : listed) out.push_back(placed(arg.get<std::string>(), directory, root));
    return out;
}

// ./<name> PATH…: what a static host of `directory` sends for each path. A whole address from `base` is its path;
// a query and a fragment are not the host's; a directory is its index.html; a file that is no text is said with
// its size (a program, a face, a picture: nothing an agent reads).
std::string host_script(const fs::path& directory, std::string_view base) {
    std::string script = "#!/bin/sh\n";
    script += std::format("site='{}'\nbase='{}'\n", os::fs::utf8(directory), base);
    script += R"sh(for path in "$@"; do
  if [ -n "$base" ]; then case "$path" in "$base"*) path="${path#"$base"}" ;; esac; fi
  path="${path%%\?*}"
  path="${path%%#*}"
  case "$path" in /*) ;; *) path="/$path" ;; esac
  case "$path" in *..*) echo "400 bad request: $path"; continue ;; esac
  case "$path" in */) path="${path}index.html" ;; esac
  if [ -d "$site$path" ]; then echo "301 moved permanently: $path/"; continue; fi
  if [ ! -f "$site$path" ]; then echo "404 not found: $path"; continue; fi
  case "$path" in
    *.html|*.md|*.txt|*.json|*.xml|*.svg|*.css|*.js) cat "$site$path" ;;
    *) echo "200 ($(wc -c < "$site$path") bytes, not text): $path" ;;
  esac
done
)sh";
    return script;
}

// The first data record the program answers to one request, or why there is none.
std::expected<json, std::string> answer_to(const fs::path& program, std::vector<std::string> args, const json& request, const fs::path& cwd) {
    const auto done = run::program(cwd, os::fs::utf8(program), std::move(args), {.cwd = cwd, .input = request.dump() + "\n", .timeout = std::chrono::seconds(120)});
    if (!done) return std::unexpected(done.error());
    for (const auto line : std::views::split(std::string_view(done->out), '\n')) {
        const auto record = json::parse(std::string_view(line.begin(), line.end()), nullptr, false);
        if (record.is_discarded() || !record.is_object()) continue;
        if (record.value("kind", "") == "data") return record;
        if (record.value("kind", "") == "error") return std::unexpected(std::format("{}: {}", record.value("code", ""), record.value("text", "")));
    }
    return std::unexpected(std::string("no data in the answer"));
}

std::optional<json> at_pointer(const json& value, std::string_view pointer) {
    const json* here = &value;
    if (pointer.empty()) return *here;
    for (const auto part : std::views::split(pointer.substr(1), '/')) {
        const std::string token(part.begin(), part.end());
        if (here->is_array()) {
            std::size_t index = 0;
            const auto [end, failed] = std::from_chars(token.data(), token.data() + token.size(), index);
            if (failed != std::errc{} || end != token.data() + token.size() || index >= here->size()) return std::nullopt;
            here = &(*here)[index];
        } else if (here->is_object() && here->contains(token)) {
            here = &(*here)[token];
        } else {
            return std::nullopt;
        }
    }
    return *here;
}

// One check on the state the agent left: its request answered, the value at its place, held to what it wants.
// Nothing: it holds; otherwise what was found.
std::optional<ordered_json> checked(const fs::path& program, const std::vector<std::string>& args, const json& check, const fs::path& cwd) {
    const auto answered = answer_to(program, args, check.value("request", json::object()), cwd);
    const auto failed = [&](const ordered_json& got) { return ordered_json{{"check", ordered_json::parse(check.dump())}, {"got", got}}; };
    if (!answered) return failed(answered.error());
    auto value = at_pointer(*answered, check.value("at", ""));
    if (value && check.value("parse", false) && value->is_string()) {
        auto parsed = json::parse(value->get<std::string>(), nullptr, false);
        value = parsed.is_discarded() ? std::nullopt : std::optional<json>(parsed);
    }
    if (value && check.contains("then")) value = at_pointer(*value, check.value("then", ""));
    if (check.value("exists", false)) return value ? std::nullopt : std::optional(failed(nullptr));
    if (!value) return failed(nullptr);
    const ordered_json got = ordered_json::parse(value->dump());
    if (check.contains("equals") && *value != check["equals"]) return failed(got);
    if (check.contains("contains") && (value->is_string() ? value->get<std::string>() : value->dump()).find(check.value("contains", "")) == std::string::npos) return failed(got);
    if (check.contains("above") && !(value->is_number() && value->get<double>() > check["above"].get<double>())) return failed(got);
    if (check.contains("below") && !(value->is_number() && value->get<double>() < check["below"].get<double>())) return failed(got);
    return std::nullopt;
}

}  // namespace

int cold_start(const fs::path& root, std::string_view set_name, const ColdStart& options) {
    const fs::path set_path = root / "tools/eval/coldstart" / std::format("{}.coldstart.xdef.json", set_name);
    const auto set = json::parse(os::fs::read(set_path).value_or(""), nullptr, false);
    if (set.is_discarded() || !set.is_object()) {
        std::cerr << std::format("omui-devtools eval cold-start: no set tools/eval/coldstart/{}.coldstart.xdef.json\n", set_name);
        return 2;
    }
    const auto built = run::build(root, {"-p", set.value("package", ""), "--profile", "release"});
    fs::path program;
    if (built)
        for (const auto& artifact : *built)
            if (artifact.name == set.value("program", "")) program = artifact.path;
    if (program.empty()) {
        std::cerr << std::format("omui-devtools eval cold-start: {} did not build{}\n", set.value("program", ""), built ? std::string() : ": " + built.error());
        return 1;
    }
    const json wrapper = set.value("wrapper", json::object());
    const std::string name = wrapper.value("name", "program");
    std::vector<ordered_json> records;
    // A trial is on the ledger when it ends: a run that stops midway has lost none, and a byte that is not UTF-8
    // (a tool's output inside an answer) is replaced, not the end of the run.
    std::ofstream ledger;
    if (!options.dry_run) ledger.open(root / ".agents/docs/evaluation/coldstart.eval.xdef.jsonl", std::ios::binary | std::ios::app);
    const auto kept = [&](const ordered_json& record) {
        if (ledger.is_open()) ledger << record.dump(-1, ' ', false, ordered_json::error_handler_t::replace) << std::endl;
    };
    for (const auto& task : set.value("tasks", json::array())) {
        if (!options.only.empty() && !std::ranges::contains(options.only, task.value("id", ""))) continue;
        for (int trial = 0; trial < options.trials; ++trial) {
            ordered_json record{{"date", run::today()}, {"set", set_name}, {"task", task.value("id", "")}, {"agent", options.agent}, {"commit", run::commit(root)}};
            // The trial's own directory: the program's state, and beside it an empty one the agent works in.
            const fs::path work = fs::temp_directory_path() / std::format("omui-coldstart-{}", std::random_device{}());
            const fs::path state = work / "state", agent_dir = work / "agent";
            std::error_code error;
            fs::create_directories(agent_dir, error);
            bool ready = true;
            std::string setup_said;
            for (const auto& step : set.value("setup", json::array())) {
                const auto done = run::program(work, os::fs::utf8(program), arguments_of(step, state, root), {.cwd = work, .merged = true});
                if (ready && !(done && done->status == 0)) setup_said = done ? last_of(done->out, 300) : done.error();
                ready = ready && done && done->status == 0;
            }
            // ./<name> REQUEST…: each argument one request, a line on the program's standard input — a plain command,
            // which an agent's permission to run ./<name> admits (a here-document is a shell construct it does not).
            std::string line = "#!/bin/sh\nprintf '%s\\n' \"$@\" | exec";
            line += std::format(" '{}'", os::fs::utf8(program));
            for (const auto& arg : arguments_of(wrapper.value("args", json::array()), state)) line += std::format(" '{}'", arg);
            if (wrapper.value("words", false)) {
                line = std::format("#!/bin/sh\nexec '{}'", os::fs::utf8(program));
                for (const auto& arg : arguments_of(wrapper.value("args", json::array()), state, root)) line += std::format(" '{}'", arg);
                line += " \"$@\"";
            }
            if (wrapper.contains("host")) line = host_script(fs::path(os::fs::from_utf8(placed(wrapper.value("host", ""), state, root))), wrapper.value("base", ""));
            std::ofstream(agent_dir / name, std::ios::binary) << line << "\n";
            fs::permissions(agent_dir / name, fs::perms::owner_all, error);
            if (!ready) {
                record["outcome"] = "unavailable";
                record["why"] = std::format("the setup failed: {}", setup_said);
                std::cout << std::format("{:<20} unavailable — {}\n", task.value("id", ""), record.value("why", ""));
                kept(record);
                records.push_back(record);
                fs::remove_all(work, error);
                continue;
            }
            const std::string prompt = std::format("{}\n\nYour task:\n{}\n\nUse only ./{}; do not read or write files. When you are done, say what you found and how you know it.",
                                                   placed(set.value("intro", ""), state, root), task.value("goal", ""), name);
            const auto started = std::chrono::steady_clock::now();
            auto reply = ask({.agent = options.agent,
                              .directory = agent_dir,
                              .tools = {std::format("Bash(./{}:*)", name)},
                              .extra = {"--disallowedTools", "Read", "Glob", "Grep", "Edit", "Write", "WebFetch", "WebSearch", "Agent", "--max-turns",
                                        std::to_string(set.value("max_turns", 40))},
                              .timeout = std::chrono::seconds(set.value("timeout", 1200))},
                             prompt);
            record["seconds"] = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - started).count();
            if (!reply) {
                record["outcome"] = "unavailable";
                record["why"] = first_of(reply.error(), 300);
                std::cout << std::format("{:<20} unavailable — {}\n", task.value("id", ""), record.value("why", ""));
                kept(record);
                records.push_back(record);
                fs::remove_all(work, error);
                continue;
            }
            ordered_json failed = ordered_json::array();
            const auto check_args = arguments_of(set.value("check_args", json::array()), state);
            for (const auto& check : task.value("checks", json::array()))
                if (auto found = checked(program, check_args, check, work)) failed.push_back(std::move(*found));
            if (task.contains("answer") && reply->text.find(task.value("answer", "")) == std::string::npos) failed.push_back({{"answer", task["answer"]}});
            const Tokens tokens = tokens_of(reply->usage);
            record["outcome"] = failed.empty() ? "success" : "failure";
            record["failed"] = failed;
            record["steps"] = reply->tools.size();
            // What it ran, in order (each cut to a line's length): where a failure went, and which of a site's files
            // an agent that only fetches found.
            ordered_json commands = ordered_json::array();
            for (const std::string& command : reply->commands) commands.push_back(first_of(command, 200));
            record["commands"] = std::move(commands);
            record["turns"] = reply->turns;
            record["tokens"] = tokens.fresh();
            record["usage"] = {{"input", tokens.input}, {"cache_write", tokens.cache_write}, {"cache_read", tokens.cache_read}, {"output", tokens.output}};
            record["last"] = last_of(reply->text, 400);
            std::cout << std::format("{:<20} {} — {} commands, {} turns, {} fresh tokens (+{} cache reads), {} s\n", task.value("id", ""), record.value("outcome", ""),
                                     reply->tools.size(), reply->turns, tokens.fresh(), tokens.cache_read, record.value("seconds", 0));
            if (!failed.empty()) std::cout << "    failed: " << failed.dump() << '\n';
            if (options.keep) std::cout << "    kept: " << os::fs::utf8(work) << '\n';
            else fs::remove_all(work, error);
            kept(record);
            records.push_back(std::move(record));
        }
    }
    std::size_t tried = 0, succeeded = 0;
    for (const auto& record : records) {
        const std::string outcome = record.value("outcome", "");
        tried += outcome == "success" || outcome == "failure";
        succeeded += outcome == "success";
    }
    if (tried) std::cout << std::format("{}: {} of {} ({:.0f}%)\n", set_name, succeeded, tried, 100.0 * succeeded / tried);
    return tried ? 0 : 1;
}

}  // namespace omui::devtools::eval
