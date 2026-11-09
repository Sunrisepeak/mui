module omui.player;

import std;
import omui.core;
import omui.view;
import omui.abi;
import omui.theme;
import omui.semantic;
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

namespace {

// The designer the program links, as it said when it started.
DesignerFactory& designer_factory() {
    static DesignerFactory factory = nullptr;
    return factory;
}

}  // namespace

void provide_designer(DesignerFactory factory) noexcept { designer_factory() = factory; }

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
    // Live Design: the text an edited script (or a module a script imports)
    // runs now, by its name, until it is committed to its file or discarded.
    std::map<std::string, std::string> edited;
    // The modules scripts import by path, by name: when each was last seen
    // changed on disk (a saved module runs its script again).
    std::map<std::string, std::filesystem::file_time_type> module_stamps;
    abi::OmuiRuntime                  handle = nullptr;
    std::vector<File>                 files;
    std::vector<std::pair<std::string, std::future<Status>>> pending;  // scripts running, by path
    std::vector<std::string>          problems;
    std::uint64_t                     timer = 0;
    seconds                           period{0.1};

    Impl(Runtime& r, Manifest m, std::vector<Language> l, PlayerOptions o)
        : rt(r), manifest(std::move(m)), languages(std::move(l)), options(o) {
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
            if (f.engine && options.design) f.engine->keep_built_at(true);
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
            if (options.design) rt.present_design();  // what it changes is shown as a design edit (written in, unwritten, springing); a stand-in only shows the end
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

    // Every edit dropped: each edited script runs its file's text again (a
    // module's script, once); the texts dropped.
    Value discard_edits() {
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
    }

    // Every edit written to its file; each file's text before.
    std::expected<Value, std::string> commit_edits() {
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
    }

    // Live Design's designer, when the program links one (provide_designer).
    std::unique_ptr<Designer> designer;

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
    if (d.options.design && !d.designer) {
        if (DesignerFactory make = designer_factory()) d.designer = make(*this);
        else d.problem("development mode, but this program has no designer (omui.designer): `omui dev` builds it in, a release leaves it out");
    }
    if (d.problems.empty()) return {};
    std::string all;
    for (const auto& p : d.problems) all += p + "\n";
    return std::unexpected(std::move(all));
}

Status Player::show(const std::map<std::string, std::string>& texts) {
    for (const auto& [path, text] : texts) {
        const auto now = impl_->source_text(path);
        if (now && *now == text) continue;
        if (auto done = impl_->edit_source(path, text); !done) return std::unexpected(std::format("{}: {}", path, done.error()));
    }
    return {};
}

std::vector<Player::Source> Player::sources() const {
    const Impl& d = *impl_;
    std::vector<Source> out;
    for (const auto& f : d.files)
        if (f.kind == Impl::File::Kind::script) out.push_back({d.name(f.path), f.lang->name, {}, d.edited.contains(d.name(f.path))});
    for (const auto& f : d.files) {
        if (f.kind != Impl::File::Kind::script || !f.engine) continue;
        for (const auto& m : f.engine->modules()) {
            const auto module = d.module_name(m);
            out.push_back({module, f.lang->name, d.name(f.path), d.edited.contains(module)});
        }
    }
    return out;
}

std::optional<std::string> Player::text(std::string_view path) const { return impl_->source_text(path); }

std::expected<Value, std::string> Player::edit(std::string_view path, std::string_view text) { return impl_->edit_source(std::string(path), text); }

std::expected<Value, std::string> Player::commit_edits() { return impl_->commit_edits(); }

Value Player::discard_edits() { return impl_->discard_edits(); }

const Language* Player::language_of(std::string_view path) const {
    Impl& d = *impl_;
    if (const auto* f = d.script_named(path)) return f->lang;
    if (const auto* owner = d.owner_of(path)) return owner->lang;
    return nullptr;
}

std::string Player::source_name(const std::filesystem::path& file) const { return impl_->module_name(file); }
std::filesystem::path Player::source_file(std::string_view path) const { return impl_->full(path); }
Runtime& Player::runtime() const noexcept { return impl_->rt; }
const PlayerOptions& Player::options() const noexcept { return impl_->options; }
const std::vector<Language>& Player::languages() const noexcept { return impl_->languages; }

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
