module omui.player;

import std;
import omui.core;
import omui.view;
import omui.abi;
import omui.theme;
import omui.semantic;
import omui.agent;
import mlibs.trace;

namespace omui {  // names this file uses unqualified, from their owner
using mlibs::log::Log;
}

namespace omui::player {

namespace {

constexpr Log log{"player"};

abi::OmuiString str(std::string_view s) { return {s.data(), s.size()}; }

void collect(void* user, const char* data, std::size_t size) { static_cast<std::string*>(user)->append(data, size); }

std::optional<std::string> read_file(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;
    return std::string(std::istreambuf_iterator<char>(in), {});
}

std::filesystem::file_time_type stamp_of(const std::filesystem::path& file) {
    std::error_code ec;
    const auto t = std::filesystem::last_write_time(file, ec);
    return ec ? std::filesystem::file_time_type{} : t;
}

// What a check of a document said, as lines of text.
std::string diagnostics_text(std::string_view ndjson, std::string_view file) {
    std::string out;
    for (auto line : ndjson | std::views::split('\n')) {
        auto v = parse_json(std::string_view(line.begin(), line.end()));
        if (!v) continue;
        const Value* l = v->get("line");
        const Value* c = v->get("column");
        out += std::format("{}{}: {}: {}{}\n", file, l && c ? std::format(":{}:{}", l->as_int(), c->as_int()) : std::string(),
                           v->get("level") ? v->get("level")->as_string() : "error", v->get("message") ? v->get("message")->as_string() : "",
                           v->get("hint") ? std::format(" ({})", v->get("hint")->as_string()) : std::string());
    }
    return out;
}

}  // namespace

struct Player::Impl {
    struct File {
        enum class Kind : std::uint8_t { component, view, script, theme };
        Kind                              kind = Kind::view;
        std::filesystem::path             path;
        std::filesystem::file_time_type   stamp{};
        std::string                       slot;    // view
        const Language*                   lang = nullptr;  // script
        std::unique_ptr<Engine>           engine;  // script
    };

    Runtime&                          rt;
    Manifest                          manifest;
    std::vector<Language>             languages;
    PlayerOptions                     options;
    // Live Design's history (D-151): states of the design on branches, beside the application.
    DesignHistory                     history;
    std::string                       intent;  // what the design session is doing (design.begin): each step's commit says it
    // Live Design: the text an edited script (or a module a script imports)
    // runs now, by its name, until it is committed to its file or discarded.
    std::map<std::string, std::string> edited;
    // The modules scripts import by path, by name: when each was last seen
    // changed on disk (a saved module runs its script again).
    std::map<std::string, std::filesystem::file_time_type> module_stamps;
    // What the person expects of the design, in their words and as a query
    // that holds or does not (design.expect); design.check reads them against
    // the tree as it is — facts, not an account of them.
    struct Expectation {
        std::string     say;
        semantic::Query query;
        bool            absent = false;
    };
    std::vector<Expectation> expectations;
    // Where the person took the floor (design.handover): what they do after
    // it, done, is a scenario's steps (design.save_scenario).
    std::uint64_t handed_over_at = 0;

    // The expectations read against the tree as it is.
    Value check_expectations(std::span<const Expectation> list) {
        semantic::Tree tree(rt);
        Value::List results;
        std::size_t held = 0;
        for (const auto& x : list) {
            const auto found = tree.find(x.query);
            const bool holds = x.absent ? found.empty() : !found.empty();
            held += holds;
            Value::List nodes;
            for (const auto& n : found) {
                const Value face = spec::to_value(n);
                nodes.push_back(Value{}.set("id", n.id).set("name", n.name).set("value", n.value).set("states", face.get("state") ? *face.get("state") : Value(Value::List{})));
            }
            Value one;
            one.set("say", x.say).set("holds", holds).set("found", Value(std::move(nodes)));
            // Not as expected: what the last press on the node came to (the recorder), or that it is not there.
            if (!holds && !x.query.id.empty()) {
                if (const NodeId node = rt.find(x.query.id); rt.valid(node)) {
                    if (const auto why = rt.explain(rt.screen_bounds(node).center())) one.set("last_press", why->summary);
                } else {
                    one.set("why", std::format("no node {}", x.query.id));
                }
            }
            results.push_back(std::move(one));
        }
        return Value{}.set("held", static_cast<double>(held)).set("of", static_cast<double>(list.size())).set("results", Value(std::move(results)));
    }
    abi::OmuiRuntime                  handle = nullptr;
    std::vector<File>                 files;
    std::vector<std::pair<std::string, std::future<Status>>> pending;  // scripts running, by path
    std::vector<std::string>          problems;
    std::uint64_t                     timer = 0;
    seconds                           period{0.1};

    Impl(Runtime& r, Manifest m, std::vector<Language> l, PlayerOptions o)
        : rt(r), manifest(std::move(m)), languages(std::move(l)), options(o), history(manifest.base / ".omui" / "design") {
        handle = abi::attach(rt, manifest.id);
    }
    ~Impl() {
        if (timer) rt.cancel(timer);
        files.clear();  // engines first: they unsubscribe through the handle
        pending.clear();
        abi::detach(handle);
    }

    std::filesystem::path full(const std::filesystem::path& p) const { return p.is_absolute() ? p : manifest.base / p; }
    std::string name(const std::filesystem::path& p) const { return generic_utf8(p); }

    void problem(std::string text) {
        while (!text.empty() && text.back() == '\n') text.pop_back();
        log.warn("{}", text);
        problems.push_back(std::move(text));
    }

    const Language* language(const Script& s) const {
        for (const Language& l : languages)
            if (l.name == s.lang) return &l;
        const std::string ext = generic_utf8(s.entry.extension());
        for (const Language& l : languages)
            if (std::ranges::find(l.extensions, ext) != l.extensions.end()) return &l;
        return nullptr;
    }

    bool define(File& f) {
        auto text = read_file(full(f.path));
        if (!text) {
            problem(std::format("{}: cannot be read", name(f.path)));
            return false;
        }
        std::string out;
        if (abi::omuiDefineComponent(handle, str(*text), collect, &out) != abi::OMUI_SUCCESS) {
            problem(diagnostics_text(out, name(f.path)));
            return false;
        }
        return true;
    }

    // A theme file into the program's library (again: a saved edit, which
    // replaces what it declared); the runtime's composition is made again.
    bool add_theme(File& f, bool again) {
        auto text = read_file(full(f.path));
        if (!text) {
            problem(std::format("{}: cannot be read", name(f.path)));
            return false;
        }
        if (auto r = theme::library().add(*text, name(f.path)); !r) {
            std::string all;
            for (const auto& x : r.error()) all += x.text() + "\n";
            problem(all);
            return false;
        }
        if (!again) return true;
        std::vector<std::string> layers;
        for (auto part : rt.theme().name | std::views::split('+')) layers.emplace_back(part.begin(), part.end());
        if (auto r = theme::apply(rt, layers); !r) {
            problem(std::format("{}: {}", name(f.path), r.error()));
            return false;
        }
        return true;
    }

    bool mount(File& f) {
        auto text = read_file(full(f.path));
        if (!text) {
            problem(std::format("{}: cannot be read", name(f.path)));
            return false;
        }
        if (abi::omuiMount(handle, str(f.slot), str(*text)) != abi::OMUI_SUCCESS) {
            std::string out;
            (void)abi::omuiCheck(handle, str(*text), collect, &out);
            problem(diagnostics_text(out.empty() ? std::string("{\"message\": \"refused\"}") : out, name(f.path)));
            return false;
        }
        return true;
    }

    bool run(File& f, bool again) {
        auto code = f.lang->load ? f.lang->load(full(f.path)) : [&]() -> std::expected<std::string, std::string> {
            auto t = read_file(full(f.path));
            if (!t) return std::unexpected(std::string("cannot be read"));
            return *t;
        }();
        if (!code) {
            problem(std::format("{}: {}", name(f.path), code.error()));
            return false;
        }
        if (!f.engine) {
            f.engine = f.lang->make(handle);
            if (f.engine && options.design) f.engine->keep_origins(true);
        }
        if (!f.engine) {
            problem(std::format("{}: the {} language could not start", name(f.path), f.lang->name));
            return false;
        }
        // By its full path: the modules it imports by path are found from it.
        const std::string script = generic_utf8(full(f.path));
        pending.emplace_back(name(f.path), again ? f.engine->reload(std::move(*code), script) : f.engine->run(std::move(*code), script));
        return true;
    }

    // ---- Live Design (D-149): the design.* verbs, in development mode ----

    File* script_named(std::string_view path) {
        for (auto& f : files)
            if (f.kind == File::Kind::script && name(f.path) == path) return &f;
        return nullptr;
    }

    // A module a script imports by path, as the design verbs name it (from the
    // manifest's directory), and the script that imports it.
    std::string module_name(const std::filesystem::path& file) const {
        const auto inside = file.lexically_normal().lexically_relative(manifest.base.lexically_normal());
        return generic_utf8(!inside.empty() && *inside.begin() != ".." ? inside : file);
    }
    File* owner_of(std::string_view module) {
        for (auto& f : files)
            if (f.kind == File::Kind::script && f.engine)
                for (const auto& m : f.engine->modules())
                    if (module_name(m) == module) return &f;
        return nullptr;
    }

    // The text a script or a module runs now: an edit's, or its file's.
    std::optional<std::string> source_text(std::string_view path) {
        if (File* f = script_named(path)) return running_text(*f);
        if (const auto edit = edited.find(std::string(path)); edit != edited.end()) return edit->second;
        return read_file(full(path));
    }

    std::expected<std::string, std::string> compiled(const File& f, std::string_view text) const {
        if (f.lang->load_text) return f.lang->load_text(text, full(f.path));
        return std::string(text);
    }

    // The text a script runs now: its edit, or its file's.
    std::optional<std::string> running_text(const File& f) const {
        if (auto it = edited.find(name(f.path)); it != edited.end()) return it->second;
        return read_file(full(f.path));
    }

    // ---- Reloading at once (W2): what changed on disk, or the files named, waited for ----
    Value last_reload;  // what the last reload did, an automatic one too

    std::expected<Value, std::string> reload_now(const Value& request) {
        if (const Value* last = request.get("last"); last && last->as_bool())
            return last_reload.is_null() ? Value{}.set("reloaded", Value(Value::List{})).set("changes", Value{}) : last_reload;
        std::set<std::string> named;
        if (const Value* paths = request.get("paths"); paths && paths->list())
            for (const Value& path : *paths->list()) named.insert(std::string(path.as_string()));
        collect_runs();
        semantic::Tree tree(rt);
        const auto before = tree.snapshot();
        const auto start = std::chrono::steady_clock::now();
        Value::List reloaded, failed;
        std::vector<File*> scripts;  // to run again, each once
        const auto again = [&](File& f) {
            if (std::ranges::find(scripts, &f) == scripts.end()) scripts.push_back(&f);
        };
        const auto fail = [&](std::string path, std::string why) { failed.push_back(Value{}.set("path", std::move(path)).set("problem", std::move(why))); };
        // The modules scripts import by path: one saved runs its script again (it alone evaluated again).
        for (auto& f : files) {
            if (f.kind != File::Kind::script || !f.engine) continue;
            for (const auto& m : f.engine->modules()) {
                const auto module = module_name(m);
                const auto now = stamp_of(m);
                auto [seen, first] = module_stamps.try_emplace(module, now);
                if ((first || seen->second == now) && !named.contains(module)) continue;
                seen->second = now;
                edited.erase(module);  // a file saved meanwhile wins over an edit not committed
                f.engine->module_changed(m, std::nullopt);
                reloaded.emplace_back(module);
                again(f);
            }
        }
        for (auto& f : files) {
            const auto now = stamp_of(full(f.path));
            if (now == f.stamp && !named.contains(name(f.path))) continue;
            f.stamp = now;
            edited.erase(name(f.path));
            reloaded.emplace_back(name(f.path));
            const std::size_t said = problems.size();
            bool ok = true;
            switch (f.kind) {
                case File::Kind::component: ok = define(f); break;
                case File::Kind::view: ok = mount(f); break;
                case File::Kind::theme: ok = add_theme(f, true); break;
                case File::Kind::script: again(f); break;
            }
            if (!ok) fail(name(f.path), problems.size() > said ? problems.back() : std::string("not reloaded"));
        }
        for (File* f : scripts) {
            auto code = f->lang->load ? f->lang->load(full(f->path)) : [&]() -> std::expected<std::string, std::string> {
                auto text = read_file(full(f->path));
                if (!text) return std::unexpected(std::string("cannot be read"));
                return *text;
            }();
            if (!code) {
                fail(name(f->path), code.error());
                continue;
            }
            mlibs::trace::Span span("player.reload", "player");
            if (options.design) rt.present_design();  // shown as a design edit: written in, unwritten, springing
            if (auto ran = f->engine->reload_now(std::move(*code), generic_utf8(full(f->path))); !ran) fail(name(f->path), ran.error());
        }
        const double took = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        Value answer;
        answer.set("reloaded", Value(std::move(reloaded))).set("took_ms", took).set("changes", semantic::changes_between(before, tree.snapshot()));
        if (!failed.empty()) answer.set("problems", Value(std::move(failed)));
        if (!answer.get("reloaded")->list()->empty()) last_reload = answer;  // a look that found nothing keeps the last
        return answer;
    }

    // Runs `text` as the script, waited for; the semantic changes it made.
    std::expected<Value, std::string> run_text(File& f, std::string_view text) {
        auto code = compiled(f, text);
        if (!code) return std::unexpected(code.error());  // nothing ran
        if (!f.engine) return std::unexpected(std::format("{} is not running", name(f.path)));
        semantic::Tree tree(rt);
        const auto before = tree.snapshot();
        const auto start = std::chrono::steady_clock::now();
        Status ran;
        {
            mlibs::trace::Span span("player.design", "player");
            rt.present_design();  // what it changes is shown as a design edit (written in, unwritten, springing)
            ran = f.engine->reload_now(std::move(*code), generic_utf8(full(f.path)));
        }
        const double took = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        if (!ran) return std::unexpected(std::format("{}: {}", name(f.path), ran.error()));
        return Value{}.set("path", name(f.path)).set("took_ms", took).set("changes", semantic::changes_between(before, tree.snapshot()));
    }

    // New text for a script or a module, run at once with its state kept
    // (design.edit, and the structured operations' edits): the semantic
    // changes and the time, or its problems with what ran before running again.
    std::expected<Value, std::string> edit_source(const std::string& path, std::string_view text) {
        File* f = script_named(path);
        if (!f) {
            // A module a script imports: it alone is evaluated again, with its script.
            File* owner = owner_of(path);
            if (!owner) return std::unexpected(std::format("no script or module `{}` (design.sources lists them)", path));
            if (auto check = compiled(*owner, text); !check)  // its problems, before anything runs
                return std::unexpected(std::format("{}: {}", path, check.error()));
            const auto file = full(path);
            const auto before = edited.find(path);
            const std::optional<std::string> previous = before != edited.end() ? std::optional(before->second) : std::nullopt;
            owner->engine->module_changed(file, std::string(text));
            auto done = run_text(*owner, running_text(*owner).value_or(std::string()));
            if (!done) {
                owner->engine->module_changed(file, previous);
                (void)run_text(*owner, running_text(*owner).value_or(std::string()));
                return std::unexpected(done.error());
            }
            edited[path] = std::string(text);
            done->set("path", path);
            announce(path, *done);
            return done;
        }
        const std::string previous = running_text(*f).value_or(std::string());
        auto done = run_text(*f, text);
        if (!done) {
            // A run that failed part way: what ran before runs again.
            if (!compiled(*f, text).has_value()) return std::unexpected(done.error());
            (void)run_text(*f, previous);
            return std::unexpected(done.error());
        }
        edited[path] = std::string(text);
        announce(path, *done);
        return done;
    }

    // A design step that changed the design, kept as a commit (D-151: every
    // step is one): its intent, what it did, the semantic changes. Answered
    // with the commit's id; a step that changed nothing makes none.
    VerbResult step(VerbResult done, std::string what) {
        if (!done) return done;
        semantic::Tree tree(rt);
        const auto by = rt.current_invocation();
        const Value* changes = done->get("changes");
        auto commit = history.commit(design_texts(), intent.empty() ? std::move(what) : std::format("{} — {}", intent, what),
                                     by ? by->actor.text() : std::string("person"), agent::tree_hash(tree), std::nullopt,
                                     changes ? changes->json() : std::string());
        if (commit) done->set("commit", commit->id);
        return done;
    }

    // Every source's text as it runs now (edited, or its file's): a design's state.
    std::map<std::string, std::string> design_texts() {
        std::map<std::string, std::string> out;
        for (const auto& f : files) {
            if (f.kind != File::Kind::script) continue;
            if (const auto text = running_text(f)) out.emplace(name(f.path), *text);
            if (!f.engine) continue;
            for (const auto& m : f.engine->modules())
                if (const auto text = source_text(module_name(m))) out.emplace(module_name(m), *text);
        }
        return out;
    }

    // A state of the design put into the running application: each source
    // whose text differs edited as design.edit edits it (and presented so).
    std::expected<Value, std::string> apply_texts(const std::map<std::string, std::string>& texts) {
        Value::List changed;
        for (const auto& [path, text] : texts) {
            const auto now = source_text(path);
            if (now && *now == text) continue;
            auto done = edit_source(path, text);
            if (!done) return std::unexpected(std::format("{}: {}", path, done.error()));
            changed.push_back(done->set("path", path));
        }
        return Value{}.set("edits", Value(std::move(changed)));
    }

    static Value commit_value(const DesignCommit& commit) {
        Value::List parents;
        for (const auto& parent : commit.parents) parents.push_back(Value(parent));
        Value::List paths;
        for (const auto& [path, object] : commit.files) paths.push_back(Value(path));
        return Value{}
            .set("id", commit.id)
            .set("message", commit.message)
            .set("actor", commit.actor)
            .set("branch", commit.branch)
            .set("time", commit.time)
            .set("tree", commit.tree)
            .set("parents", Value(std::move(parents)))
            .set("files", Value(std::move(paths)));
    }

    // Where a script built a node: its path as design.sources names it, line and column.
    struct Place {
        std::string path;
        std::uint32_t line = 0, column = 0;
    };
    std::expected<Place, std::string> place_of(std::string_view node) const {
        const NodeId id = rt.reveal(node);
        if (!rt.valid(id)) return std::unexpected(std::format("no node `{}`", node));
        // file:line:column, the file as the script ran it
        const std::string_view origin = rt.script_origin(id);
        const auto column_at = origin.rfind(':'), line_at = column_at == std::string_view::npos ? column_at : origin.rfind(':', column_at - 1);
        if (line_at == std::string_view::npos) return std::unexpected(std::format("`{}` was not built by a script (or not in development mode)", node));
        const std::string_view file = origin.substr(0, line_at);
        Place place{module_name(std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(file.data()), file.size())))};
        (void)std::from_chars(origin.data() + line_at + 1, origin.data() + column_at, place.line);
        (void)std::from_chars(origin.data() + column_at + 1, origin.data() + origin.size(), place.column);
        return place;
    }

    // A structured operation on the builder call that built `node`: the new
    // text its language's editor makes, run as design.edit runs it, and
    // answered with the lines it changed.
    VerbResult structured(std::string_view node, const std::function<std::expected<std::string, std::string>(const Language&, std::string_view, const Place&)>& change) {
        auto place = place_of(node);
        if (!place) return std::unexpected(place.error());
        const File* owner = script_named(place->path);
        if (!owner) owner = owner_of(place->path);
        if (!owner) return std::unexpected(std::format("`{}` was built by {}, which is not one of the application's sources", node, place->path));
        const auto text = source_text(place->path);
        if (!text) return std::unexpected(std::format("{} cannot be read", place->path));
        auto changed = change(*owner->lang, *text, *place);
        if (!changed) return std::unexpected(std::format("{}:{}: {}", place->path, place->line, changed.error()));
        auto done = edit_source(place->path, *changed);
        if (!done) return std::unexpected(done.error());
        return done->set("path", place->path).set("source", changed_lines(*text, *changed));
    }

    // Where two texts differ, by whole lines: the first line and the lines before and after.
    static Value changed_lines(std::string_view before, std::string_view after) {
        const auto lines = [](std::string_view text) {
            std::vector<std::string_view> out;
            for (const auto line : std::views::split(text, '\n')) out.emplace_back(line.begin(), line.end());
            return out;
        };
        const auto old_lines = lines(before), new_lines = lines(after);
        std::size_t head = 0, tail = 0;
        while (head < old_lines.size() && head < new_lines.size() && old_lines[head] == new_lines[head]) ++head;
        while (tail + head < old_lines.size() && tail + head < new_lines.size() &&
               old_lines[old_lines.size() - 1 - tail] == new_lines[new_lines.size() - 1 - tail])
            ++tail;
        const auto joined = [](std::span<const std::string_view> part) {
            std::string out;
            for (const auto line : part) out += std::format("{}{}", out.empty() ? "" : "\n", line);
            return out;
        };
        return Value{}
            .set("line", static_cast<double>(head + 1))
            .set("before", joined(std::span(old_lines).subspan(head, old_lines.size() - head - tail)))
            .set("after", joined(std::span(new_lines).subspan(head, new_lines.size() - head - tail)));
    }

    // What an agent's edit did, said beside the first node it changed or
    // added (its presence shows it): the file, how many changes, the first.
    void announce(std::string_view path, const Value& answer) {
        const auto by = rt.current_invocation();
        if (!by || by->actor.kind != "agent") return;
        const Value* changes = answer.get("changes");
        std::string first, node;
        std::size_t count = 0;
        for (const std::string_view kind : {"changed", "added", "removed"}) {
            const Value* list = changes ? changes->get(kind) : nullptr;
            if (!list || !list->list()) continue;
            count += list->list()->size();
            if (first.empty() && !list->list()->empty()) {
                const std::string id(list->list()->front().get("id")->as_string());
                first = std::format("{} {}", kind, id);
                if (kind != "removed") node = id;  // a removed one is not there to stand beside
            }
        }
        rt.note({.actor = by->actor.text(),
                 .text = count ? std::format("{}: {} change{} — {}", path, count, count == 1 ? "" : "s", first) : std::format("{}: no visible change", path),
                 .node = node});
    }

    void design_verbs() {
        // Each verb as Live Design's catalog declares it (design.ops.omui.ts).
        auto add = [&](std::string_view name, VerbFn fn) {
            const auto entry = std::ranges::find(agent::design::catalog, name, [](const auto& op) { return op.operation.name; });
            if (entry == agent::design::catalog.end()) return problem(std::format("design: {} is not in the catalog", name));
            if (auto r = rt.verbs().add(agent::verb_of(*entry), std::move(fn)); !r) problem(std::format("design: {}", r.error()));
        };
        add("design.sources",
            [this](const Value&) -> VerbResult {
                Value::List scripts;
                for (const auto& f : files) {
                    if (f.kind != File::Kind::script) continue;
                    const auto text = running_text(f);
                    scripts.push_back(Value{}
                                          .set("path", name(f.path))
                                          .set("language", f.lang->name)
                                          .set("text", text ? *text : std::string())
                                          .set("edited", edited.contains(name(f.path))));
                }
                Value::List modules;
                for (const auto& f : files) {
                    if (f.kind != File::Kind::script || !f.engine) continue;
                    for (const auto& m : f.engine->modules()) {
                        const auto module = module_name(m);
                        const auto edit = edited.find(module);
                        const auto text = edit != edited.end() ? std::optional(edit->second) : read_file(m);
                        modules.push_back(Value{}
                                              .set("path", module)
                                              .set("imported_by", name(f.path))
                                              .set("text", text ? *text : std::string())
                                              .set("edited", edit != edited.end()));
                    }
                }
                return Value{}.set("scripts", Value(std::move(scripts))).set("modules", Value(std::move(modules)));
            });
        add("design.api",
            [this](const Value&) -> VerbResult {
                std::string catalog;
                (void)abi::omuiCatalog(handle, collect, &catalog);
                Value languages(Value::Map{});
                for (const auto& f : files)
                    if (f.kind == File::Kind::script && f.lang->describe && !languages.get(f.lang->name))
                        languages.set(f.lang->name, f.lang->describe(catalog));
                if (languages.map()->empty()) return std::unexpected(std::string("no script language here describes its API"));
                return Value{}.set("languages", std::move(languages));
            });
        add("design.locate", [this](const Value& args) -> VerbResult {
            if (const Value* node = args.get("node")) {
                auto place = place_of(node->as_string());
                if (!place) return std::unexpected(place.error());
                Value answer = Value{}
                                   .set("node", rt.path(rt.reveal(node->as_string())))
                                   .set("path", place->path)
                                   .set("line", static_cast<double>(place->line))
                                   .set("column", static_cast<double>(place->column));
                if (const auto text = source_text(place->path)) {
                    std::string_view rest = *text;
                    for (std::uint32_t skipped = 1; skipped < place->line && rest.contains('\n'); ++skipped) rest.remove_prefix(rest.find('\n') + 1);
                    answer.set("text", std::string(rest.substr(0, rest.find('\n'))));
                }
                return answer;
            }
            const std::string path(args.get("path") ? args.get("path")->as_string() : "");
            const Value* line = args.get("line");
            if (path.empty() || !line || !line->is_number() || line->as_double() < 1)
                return std::unexpected(std::string("design.locate takes a `node`, or a `path` and a `line` (from 1)"));
            if (!script_named(path) && !owner_of(path)) return std::unexpected(std::format("no script or module `{}` (design.sources lists them)", path));
            const auto wanted = static_cast<std::uint32_t>(line->as_double());
            std::vector<NodeId> built = rt.built_at(generic_utf8(full(path)), wanted);
            if (built.empty()) built = rt.built_at(generic_utf8(full(path).lexically_normal()), wanted);
            Value::List nodes;
            for (const NodeId id : built) nodes.push_back(Value{}.set("node", rt.path(id)).set("origin", std::string(rt.script_origin(id))));
            return Value{}.set("path", path).set("line", static_cast<double>(wanted)).set("nodes", Value(std::move(nodes)));
        });
        add("design.edit", [this](const Value& args) -> VerbResult {
            const Value* text = args.get("text");
            if (!text || text->type() != Value::Type::text) return std::unexpected(std::string("the new text is `text`, a string"));
            const std::string path(args.get("path") ? args.get("path")->as_string() : "");
            return step(edit_source(path, text->as_string()), std::format("edit {}", path));
        });
        add("design.set", [this](const Value& args) -> VerbResult {
            const Value* properties = args.get("props");
            if (!properties || !properties->map() || properties->map()->empty())
                return std::unexpected(std::string("design.set takes `node` and `props`: {name: value} (null takes one out)"));
            const std::string node(args.get("node") ? args.get("node")->as_string() : "");
            return step(structured(node,
                              [&](const Language& language, std::string_view text, const Place& place) -> std::expected<std::string, std::string> {
                                  if (!language.edit.set) return std::unexpected(std::format("{} has no structured edits: use design.edit", language.name));
                                  std::string now(text);
                                  for (const auto& [name, value] : *properties->map()) {
                                      const std::string code = value.json();
                                      auto next = language.edit.set(now, place.line, place.column, name,
                                                                    value.type() == Value::Type::null ? std::nullopt : std::optional<std::string_view>(code));
                                      if (!next) return next;
                                      now = std::move(*next);
                                  }
                                  return now;
                              }),
                        std::format("set {} of {}", properties->json(), node));
        });
        add("design.insert", [this](const Value& args) -> VerbResult {
            const Value* code = args.get("code");
            if (!code || code->type() != Value::Type::text || code->as_string().empty())
                return std::unexpected(std::string("design.insert takes `parent` and `code`: an element as the language writes it"));
            std::optional<std::size_t> index;
            if (const Value* at = args.get("index"); at && at->is_number() && at->as_double() >= 0) index = static_cast<std::size_t>(at->as_double());
            const std::string parent(args.get("parent") ? args.get("parent")->as_string() : "");
            return step(structured(parent,
                              [&](const Language& language, std::string_view text, const Place& place) -> std::expected<std::string, std::string> {
                                  if (!language.edit.insert) return std::unexpected(std::format("{} has no structured edits: use design.edit", language.name));
                                  return language.edit.insert(text, place.line, place.column, code->as_string(), index);
                              }),
                        std::format("insert into {}", parent));
        });
        add("design.remove", [this](const Value& args) -> VerbResult {
            const std::string node(args.get("node") ? args.get("node")->as_string() : "");
            return step(structured(node,
                              [&](const Language& language, std::string_view text, const Place& place) -> std::expected<std::string, std::string> {
                                  if (!language.edit.remove) return std::unexpected(std::format("{} has no structured edits: use design.edit", language.name));
                                  return language.edit.remove(text, place.line, place.column);
                              }),
                        std::format("remove {}", node));
        });
        add("design.begin", [this](const Value& args) -> VerbResult {
            intent = std::string(args.get("intent") ? args.get("intent")->as_string() : "");
            if (intent.empty()) return std::unexpected(std::string("design.begin says what the session is for: `intent`"));
            // Kept before the first step: the state the session began from (nothing, when it is the last commit already).
            semantic::Tree tree(rt);
            const auto by = rt.current_invocation();
            auto start = history.commit(design_texts(), std::format("{} — begun", intent), by ? by->actor.text() : std::string("person"), agent::tree_hash(tree));
            if (by) rt.note({.actor = by->actor.text(), .text = intent});
            return Value{}.set("intent", intent).set("branch", history.head_branch()).set("commit", start ? Value(start->id) : Value{});
        });
        // Live Design's history (D-151): states of the design as commits on branches, under .omui/design/.
        add("design.history", [this](const Value& args) -> VerbResult {
            const std::string branch(args.get("branch") ? args.get("branch")->as_string() : history.head_branch());
            const std::size_t limit = args.get("limit") && args.get("limit")->is_number() ? static_cast<std::size_t>(args.get("limit")->as_double()) : 50;
            Value::List log;
            for (const DesignCommit& commit : history.log(branch, limit)) log.push_back(commit_value(commit));
            Value branches(Value::Map{}), tags(Value::Map{});
            for (const auto& [name, id] : history.branches()) branches.set(name, id);
            for (const auto& [name, id] : history.tags()) tags.set(name, id);
            const auto head = history.texts_of("HEAD");
            return Value{}
                .set("head", history.head_branch())
                .set("branches", std::move(branches))
                .set("tags", std::move(tags))
                .set("log", Value(std::move(log)))
                .set("uncommitted", !head || *head != design_texts());
        });
        add("design.history.commit", [this](const Value& args) -> VerbResult {
            const std::string message(args.get("message") ? args.get("message")->as_string() : "");
            if (message.empty()) return std::unexpected(std::string("a commit says what it is: `message`"));
            semantic::Tree tree(rt);
            const auto by = rt.current_invocation();
            auto commit = history.commit(design_texts(), message, by ? by->actor.text() : std::string("person"), agent::tree_hash(tree));
            if (!commit) return std::unexpected(commit.error());
            return commit_value(*commit);
        });
        add("design.history.branch", [this](const Value& args) -> VerbResult {
            const std::string name(args.get("name") ? args.get("name")->as_string() : "");
            const Value* at = args.get("at");
            auto made = history.branch(name, at ? std::optional<std::string_view>(at->as_string()) : std::nullopt);
            if (!made) return std::unexpected(made.error());
            return Value{}.set("branch", name).set("at", *made);
        });
        add("design.history.switch", [this](const Value& args) -> VerbResult {
            const std::string to(args.get("to") ? args.get("to")->as_string() : "");
            const auto head = history.texts_of("HEAD");
            if (head && *head != design_texts() && !(args.get("force") && args.get("force")->as_bool()))
                return std::unexpected(std::string("the design has changes not committed: design.history.commit them, or switch with force (they are dropped)"));
            const auto branches = history.branches();
            auto at = history.resolve(to);
            if (!at) return std::unexpected(std::format("{} (design.history lists the branches and tags)", at.error()));
            auto texts = history.texts_of(*at);
            if (!texts) return std::unexpected(texts.error());
            auto applied = apply_texts(*texts);
            if (!applied) return std::unexpected(applied.error());
            if (!branches.contains(to))  // a commit, seen: the branch stays; a commit keeps it there, design.history.branch makes it one
                return applied->set("branch", history.head_branch()).set("at", *at).set("detached", true);
            (void)history.switch_to(to);
            return applied->set("branch", to).set("at", branches.at(to));
        });
        add("design.history.revert", [this](const Value& args) -> VerbResult {
            const std::string to(args.get("to") ? args.get("to")->as_string() : "");
            auto id = history.resolve(to);
            if (!id) return std::unexpected(id.error());
            auto texts = history.texts_of(*id);
            if (!texts) return std::unexpected(texts.error());
            auto applied = apply_texts(*texts);
            if (!applied) return std::unexpected(applied.error());
            semantic::Tree tree(rt);
            const auto by = rt.current_invocation();
            auto commit = history.commit(design_texts(), std::format("back to {}", id->substr(0, 8)), by ? by->actor.text() : std::string("person"),
                                         agent::tree_hash(tree));
            if (commit) applied->set("commit", commit_value(*commit));
            return applied;
        });
        add("design.history.diff", [this](const Value& args) -> VerbResult {
            const std::string from(args.get("from") ? args.get("from")->as_string() : "HEAD");
            auto before = history.texts_of(from);
            if (!before) return std::unexpected(before.error());
            std::map<std::string, std::string> after;
            if (const Value* to = args.get("to")) {
                auto texts = history.texts_of(to->as_string());
                if (!texts) return std::unexpected(texts.error());
                after = std::move(*texts);
            } else {
                after = design_texts();  // what runs now
            }
            Value::List files;
            std::set<std::string> paths;
            for (const auto& [path, text] : *before) paths.insert(path);
            for (const auto& [path, text] : after) paths.insert(path);
            for (const auto& path : paths) {
                const std::string old_text = before->contains(path) ? before->at(path) : std::string();
                const std::string new_text = after.contains(path) ? after.at(path) : std::string();
                if (old_text != new_text) files.push_back(changed_lines(old_text, new_text).set("path", path));
            }
            return Value{}.set("from", from).set("to", args.get("to") ? *args.get("to") : Value("running")).set("files", Value(std::move(files)));
        });
        add("design.history.merge", [this](const Value& args) -> VerbResult {
            const std::string from(args.get("from") ? args.get("from")->as_string() : "");
            auto theirs_id = history.resolve(from);
            auto ours_id = history.resolve("HEAD");
            if (!theirs_id) return std::unexpected(theirs_id.error());
            if (!ours_id) return std::unexpected(std::string("nothing committed on this branch to merge into"));
            const auto base_id = history.common_ancestor("HEAD", *theirs_id);
            auto ours = history.texts_of(*ours_id), theirs = history.texts_of(*theirs_id);
            if (!ours || !theirs) return std::unexpected(!ours ? ours.error() : theirs.error());
            std::map<std::string, std::string> base;
            if (base_id)
                if (auto texts = history.texts_of(*base_id)) base = std::move(*texts);
            std::map<std::string, std::string> merged;
            Value::List conflicts;
            std::set<std::string> paths;
            for (const auto& [path, text] : *ours) paths.insert(path);
            for (const auto& [path, text] : *theirs) paths.insert(path);
            for (const auto& path : paths) {
                const std::string base_text = base.contains(path) ? base.at(path) : std::string();
                const std::string our_text = ours->contains(path) ? ours->at(path) : base_text;
                const std::string their_text = theirs->contains(path) ? theirs->at(path) : base_text;
                const TextMerge result = merge_text(base_text, our_text, their_text);
                if (result.conflict) conflicts.push_back(Value(path));
                merged.emplace(path, result.text);
            }
            if (!conflicts.empty())
                return std::unexpected(std::format("both branches changed the same lines of {}: nothing merged — design.edit one side to agree, commit, and merge again",
                                                   Value(conflicts).json()));
            auto applied = apply_texts(merged);
            if (!applied) return std::unexpected(applied.error());
            semantic::Tree tree(rt);
            const auto by = rt.current_invocation();
            auto commit = history.commit(design_texts(), std::format("merge {}", from), by ? by->actor.text() : std::string("person"), agent::tree_hash(tree),
                                         std::vector<std::string>{*ours_id, *theirs_id});
            if (commit) applied->set("commit", commit_value(*commit));
            return applied;
        });
        add("design.history.tag", [this](const Value& args) -> VerbResult {
            const std::string name(args.get("name") ? args.get("name")->as_string() : "");
            const Value* at = args.get("at");
            auto tagged = history.tag(name, at ? std::optional<std::string_view>(at->as_string()) : std::nullopt);
            if (!tagged) return std::unexpected(tagged.error());
            return Value{}.set("tag", name).set("at", *tagged);
        });
        add("design.expect",
            [this](const Value& args) -> VerbResult {
                if (const Value* q = args.get("query")) {
                    auto query = semantic::Query::parse(*q);
                    if (!query) return std::unexpected(std::format("query: {}", query.error()));
                    const std::string say(args.get("say") ? args.get("say")->as_string() : q->json());
                    expectations.push_back({say, std::move(*query), args.get("absent") && args.get("absent")->as_bool()});
                }
                Value::List list;
                for (const auto& x : expectations)
                    list.push_back(Value{}.set("say", x.say).set("query", x.query.to_value()).set("absent", x.absent));
                return Value{}.set("expected", Value(std::move(list)));
            });
        add("design.check",
            [this](const Value&) -> VerbResult { return check_expectations(expectations); });
        add("design.handover",
            [this](const Value&) -> VerbResult {
                const auto journal = rt.action_journal();
                handed_over_at = journal.empty() ? 0 : journal.back().id;
                if (const auto by = rt.current_invocation())
                    rt.note({.actor = by->actor.text(), .text = "Your turn: try it, and tell me when you are done", .hands_back = true});
                return Value{}.set("from", static_cast<double>(handed_over_at));
            });
        add("design.save_scenario",
            [this](const Value& args) -> VerbResult {
                const std::string name(args.get("name") ? args.get("name")->as_string() : "");
                if (name.empty() || name.find_first_of("/\\.") != std::string::npos) return std::unexpected(std::string("`name`: a scenario's name, no path"));
                Value::List steps;
                for (const auto& entry : rt.action_journal())
                    if (entry.id > handed_over_at && entry.actor.starts_with("person:") && entry.exit_code == 0 && entry.action != "input")
                        steps.push_back(Value{}.set("node", entry.path).set("action", entry.action).set("args", entry.args));
                Value::List expect;
                for (const auto& x : expectations) expect.push_back(Value{}.set("say", x.say).set("query", x.query.to_value()).set("absent", x.absent));
                const Value scenario = Value{}.set("omui", "0.1").set("scenario", name).set("steps", Value(steps)).set("expect", Value(std::move(expect)));
                const auto file = manifest.base / "tests" / (name + ".scenario.omui.json");
                std::error_code error;
                std::filesystem::create_directories(file.parent_path(), error);
                std::ofstream(file, std::ios::binary) << scenario.json() << '\n';
                return Value{}.set("file", generic_utf8(file)).set("steps", static_cast<double>(steps.size()));
            });
        add("design.replay",
            [this](const Value& args) -> VerbResult {
                const std::string name(args.get("name") ? args.get("name")->as_string() : "");
                const auto text = read_file(manifest.base / "tests" / (name + ".scenario.omui.json"));
                if (!text) return std::unexpected(std::format("no scenario `{}` (tests/{}.scenario.omui.json)", name, name));
                auto scenario = parse_json(*text);
                if (!scenario) return std::unexpected(std::format("{}: {}", name, scenario.error().text()));
                semantic::Tree tree(rt);
                Invocation as = rt.current_invocation().value_or(Invocation{});
                Value::List done;
                if (const Value* steps = scenario->get("steps"); steps && steps->list())
                    for (const Value& step : *steps->list()) {
                        as.args = step.get("args") ? *step.get("args") : Value{};
                        const Envelope e = tree.act(step.get("node") ? step.get("node")->as_string() : "", step.get("action") ? step.get("action")->as_string() : "", as);
                        FrameInput still;
                        still.time = rt.now();
                        (void)rt.frame(still);
                        done.push_back(Value{}.set("node", step.get("node") ? *step.get("node") : Value{}).set("exit", static_cast<double>(e.exit_code().value_or(1))));
                    }
                std::vector<Expectation> expected;
                if (const Value* list = scenario->get("expect"); list && list->list())
                    for (const Value& x : *list->list())
                        if (auto query = semantic::Query::parse(x.get("query") ? *x.get("query") : Value{}))
                            expected.push_back({std::string(x.get("say") ? x.get("say")->as_string() : ""), std::move(*query), x.get("absent") && x.get("absent")->as_bool()});
                return check_expectations(expected).set("steps", Value(std::move(done)));
            });
        add("design.discard",
            [this](const Value&) -> VerbResult {
                Value::List dropped;
                std::vector<File*> again;  // scripts whose modules were edited: run once more, after
                for (auto it = edited.begin(); it != edited.end();) {
                    if (File* f = script_named(it->first)) {
                        if (const auto file_text = read_file(full(f->path)))
                            if (auto ran = run_text(*f, *file_text); !ran) problem(ran.error());
                    } else if (File* owner = owner_of(it->first)) {
                        owner->engine->module_changed(full(it->first), std::nullopt);
                        if (std::ranges::find(again, owner) == again.end()) again.push_back(owner);
                    }
                    dropped.push_back(Value{}.set("path", it->first).set("text", it->second));
                    it = edited.erase(it);
                }
                for (File* owner : again)
                    if (auto ran = run_text(*owner, running_text(*owner).value_or(std::string())); !ran) problem(ran.error());
                return Value{}.set("discarded", Value(std::move(dropped)));
            });
        add("design.commit",
            [this](const Value&) -> VerbResult {
                Value::List written;
                for (auto it = edited.begin(); it != edited.end();) {
                    File* f = script_named(it->first);
                    File* owner = f ? nullptr : owner_of(it->first);
                    if (!f && !owner) {
                        it = edited.erase(it);
                        continue;
                    }
                    const auto file = full(it->first);
                    const auto before = read_file(file);
                    {
                        std::ofstream out(file, std::ios::binary | std::ios::trunc);
                        out << it->second;
                        if (!out) return std::unexpected(std::format("{}: cannot be written", it->first));
                    }
                    // The watcher need not run it again: the file now says what runs.
                    if (f) f->stamp = stamp_of(file);
                    else module_stamps[it->first] = stamp_of(file);
                    written.push_back(Value{}.set("path", it->first).set("before", before ? *before : std::string()));
                    it = edited.erase(it);
                }
                return Value{}.set("committed", Value(std::move(written)));
            });
    }

    // Scripts that finished: their errors are problems.
    void collect_runs() {
        std::erase_if(pending, [&](auto& p) {
            if (p.second.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return false;
            if (Status s = p.second.get(); !s) problem(std::format("{}: {}", p.first, s.error()));
            return true;
        });
    }
};

Player::Player(Runtime& runtime, Manifest manifest, std::vector<Language> languages, PlayerOptions options)
    : impl_(std::make_unique<Impl>(runtime, std::move(manifest), std::move(languages), options)) {
    // The protocol's `reload` (W2) reloads through the player.
    runtime.set_source_reloader([impl = impl_.get()](const Value& request) { return impl->reload_now(request); });
}

Player::~Player() { impl_->rt.set_source_reloader({}); }

Status Player::start() {
    mlibs::trace::Span span("player.start", "player");
    Impl& d = *impl_;
    d.problems.clear();
    // Themes first: what the views and scripts show is themed from the start.
    for (const auto& t : d.manifest.themes) {
        d.files.push_back({Impl::File::Kind::theme, t, stamp_of(d.full(t)), {}, nullptr, nullptr});
        (void)d.add_theme(d.files.back(), false);
    }
    if (!d.manifest.theme.empty())
        if (auto r = theme::apply(d.rt, d.manifest.theme); !r) d.problem(std::format("theme: {}", r.error()));
    for (const auto& c : d.manifest.components) d.files.push_back({Impl::File::Kind::component, c, stamp_of(d.full(c)), {}, nullptr, nullptr});
    for (const auto& v : d.manifest.views) {
        // ui/main.view.omui.json mounts at "main".
        std::string slot = generic_utf8(v.filename());
        slot = slot.substr(0, slot.find('.'));
        d.files.push_back({Impl::File::Kind::view, v, stamp_of(d.full(v)), slot, nullptr, nullptr});
    }
    for (const Script& s : d.manifest.scripts) {
        const Language* l = d.language(s);
        if (!l) {
            d.problem(std::format("{}: no language `{}` is registered with the player", d.name(s.entry), s.lang));
            continue;
        }
        d.files.push_back({Impl::File::Kind::script, s.entry, stamp_of(d.full(s.entry)), {}, l, nullptr});
    }
    // Components may need each other: define them until no more can be.
    std::vector<Impl::File*> undefined;
    for (auto& f : d.files)
        if (f.kind == Impl::File::Kind::component) undefined.push_back(&f);
    for (bool progress = true; progress && !undefined.empty();) {
        progress = false;
        const std::size_t before = d.problems.size();
        std::erase_if(undefined, [&](Impl::File* f) {
            if (!d.define(*f)) return false;
            progress = true;
            return true;
        });
        if (progress) d.problems.resize(before);  // what failed may succeed on the next round
    }
    for (auto& f : d.files)
        if (f.kind == Impl::File::Kind::view) (void)d.mount(f);
    for (auto& f : d.files)
        if (f.kind == Impl::File::Kind::script) (void)d.run(f, false);
    if (d.options.design) d.design_verbs();
    if (d.problems.empty()) return {};
    std::string all;
    for (const auto& p : d.problems) all += p + "\n";
    return std::unexpected(std::move(all));
}

std::vector<std::string> Player::poll() {
    Impl& d = *impl_;
    if (d.options.design) {
        // Development mode reloads at once, waited for, so what a save did is known (W2: `reload` with `last`).
        std::vector<std::string> reloaded;
        if (auto done = d.reload_now(Value{}); done)
            if (const Value* list = done->get("reloaded"); list && list->list()) {
                for (const Value& path : *list->list()) reloaded.emplace_back(path.as_string());
                if (!reloaded.empty()) log.info("{} reloaded", reloaded.front());
            }
        return reloaded;
    }
    d.collect_runs();
    std::vector<std::string> reloaded;
    // The modules scripts import by path: one saved runs its script again (it alone evaluated again).
    for (auto& f : d.files) {
        if (f.kind != Impl::File::Kind::script || !f.engine) continue;
        bool changed = false;
        for (const auto& m : f.engine->modules()) {
            const auto module = d.module_name(m);
            const auto now = stamp_of(m);
            auto [seen, first] = d.module_stamps.try_emplace(module, now);
            if (first || seen->second == now) continue;
            seen->second = now;
            d.edited.erase(module);  // a file saved meanwhile wins over an edit not committed
            f.engine->module_changed(m, std::nullopt);
            changed = true;
            reloaded.push_back(module);
        }
        if (changed) {
            mlibs::trace::Span span("player.reload", "player");
            if (d.run(f, true)) log.info("{} reloaded (a module it imports changed)", d.name(f.path));
        }
    }
    for (auto& f : d.files) {
        const auto now = stamp_of(d.full(f.path));
        if (now == f.stamp) continue;
        f.stamp = now;
        d.edited.erase(d.name(f.path));  // a file saved meanwhile wins over an edit not committed
        mlibs::trace::Span span("player.reload", "player");
        bool ok = false;
        switch (f.kind) {
            case Impl::File::Kind::component: ok = d.define(f); break;
            case Impl::File::Kind::view: ok = d.mount(f); break;
            case Impl::File::Kind::script: ok = d.run(f, true); break;
            case Impl::File::Kind::theme: ok = d.add_theme(f, true); break;
        }
        if (ok) {
            log.info("{} reloaded", d.name(f.path));
            reloaded.push_back(d.name(f.path));
        }
    }
    return reloaded;
}

void Player::watch(seconds period) {
    Impl& d = *impl_;
    d.period = period;
    d.timer = d.rt.after(period, [this] {
        impl_->timer = 0;
        (void)poll();
        watch(impl_->period);
    });
}

bool Player::idle() const {
    Impl& d = *impl_;
    d.collect_runs();
    if (!d.pending.empty()) return false;
    for (const auto& f : d.files)
        if (f.engine && !f.engine->idle()) return false;
    return true;
}

std::span<const std::string> Player::problems() const noexcept { return impl_->problems; }

std::expected<std::vector<std::filesystem::path>, std::string> Player::declare(const std::filesystem::path& dir) const {
    const Impl& d = *impl_;
    std::string catalog, natives;
    (void)abi::omuiCatalog(d.handle, collect, &catalog);
    (void)abi::omuiNativeCatalog(d.handle, collect, &natives);
    std::vector<std::filesystem::path> written;
    for (const Language& l : d.languages) {
        if (!l.declare) continue;
        auto files = l.declare(catalog, natives, d.manifest.base, dir);
        if (!files) return std::unexpected(std::format("{}: {}", l.name, files.error()));
        written.insert(written.end(), files->begin(), files->end());
    }
    if (written.empty()) return std::unexpected(std::string("no language registered here declares its API for a type checker"));
    return written;
}
const Manifest& Player::manifest() const noexcept { return impl_->manifest; }
abi::OmuiRuntime Player::handle() const noexcept { return impl_->handle; }

}  // namespace omui::player
