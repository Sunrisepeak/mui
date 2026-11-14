// Runtime generation's stages and verbs (omui.generate:generation).
module omui.generate;

import std;
import omui.core;
import omui.view;
import omui.semantic;
import omui.viewdoc;
import omui.agent;
import mlibs.base64;
import mlibs.control;
import mcpplibs.os;

namespace omui::generate {

using namespace omui::agent;  // the protocol's and the catalogs' names


namespace {

constexpr std::array<std::pair<std::string_view, Openness>, 4> opennesses{{
    {"off", Openness::off}, {"data", Openness::data}, {"limited", Openness::limited}, {"all", Openness::all}}};

std::string text_arg(const Value& args, std::string_view key) {
    const Value* given = args.get(key);
    return given && given->type() == Value::Type::text ? std::string(given->as_string()) : std::string();
}

std::string actor_now(Runtime& runtime) {
    const auto call = runtime.current_invocation();
    return call ? call->actor.text() : std::string("person:host");
}

// What a document holds that its openness or its limits may refuse.
struct Census {
    std::size_t                     nodes = 0;
    std::set<std::string, std::less<>> components, verbs;
};

void count(const viewdoc::Node& node, Census& out) {
    ++out.nodes;
    if (node.kind == viewdoc::Node::Kind::element) out.components.insert(node.type);
    for (const viewdoc::Reaction& reaction : node.on)
        for (const viewdoc::Op& op : reaction.ops)
            if (op.kind == viewdoc::Op::Kind::verb) out.verbs.insert(op.target);
    for (const viewdoc::Node& child : node.children) count(child, out);
    for (const viewdoc::Node& child : node.otherwise) count(child, out);
}

// What a document reads and writes beyond itself (PPet §7.2): the host's signals it binds to,
// the verbs it invokes, the surface it fills.
Value read_write_set(const viewdoc::Document& document, std::string_view surface) {
    Census census;
    count(document.root, census);
    Value::List provided, verbs;
    for (const viewdoc::SignalDecl& signal : document.signals)
        if (signal.provided) provided.push_back(Value(signal.name));
    for (const std::string& verb : census.verbs) verbs.push_back(Value(verb));
    return Value{}
        .set("reads", Value{}.set("signals", Value(std::move(provided))).set("verbs", Value(std::move(verbs))))
        .set("writes", Value{}.set("surface", std::string(surface)));
}

}  // namespace

std::string_view openness_name(Openness openness) noexcept {
    for (const auto& [name, value] : opennesses)
        if (value == openness) return name;
    return "off";
}

std::optional<Openness> parse_openness(std::string_view text) noexcept {
    for (const auto& [name, value] : opennesses)
        if (name == text) return value;
    return std::nullopt;
}

struct Generation::Impl {
    Runtime&          runtime;
    GenerationOptions options;

    enum class Stage : std::uint8_t { validated, prepared, previewed };
    struct Draft {
        std::string   id, surface, document, actor;
        Stage         stage = Stage::validated;
        viewdoc::View view;
        std::size_t   base = 0;  // the surface's version it was made over (its read set: what a commit checks)
    };
    // A piece kept for reuse (generate.keep): its document, checked again whenever it is drafted.
    struct Piece {
        std::string name, document, doc, actor, from;
    };
    std::map<std::string, Piece, std::less<>> pieces;
    struct Version {
        std::size_t   number = 0;
        std::string   document, actor, draft;
        double        at = 0;  // the runtime's time
        viewdoc::View view;
    };
    struct Shown {
        std::vector<Version> versions;
        std::size_t          current = 0;  // 0: nothing
        std::string          problem;      // why what was kept is not shown (started again)
    };
    std::map<std::string, Draft, std::less<>> drafts;
    std::map<std::string, Shown, std::less<>> surfaces;
    std::size_t                               next_draft = 1;
    std::vector<std::string>                  added;  // the verbs, removed with it

    std::uint64_t waiting = 0;  // the timer that shows what was kept once its slots are on screen
    // What the person did in generated interface (the events its views emit), the last ones kept.
    static constexpr std::size_t actions_kept = 256;
    std::deque<Value>            actions;
    std::int64_t                 next_action = 1;
    A2uiReader                   a2ui;  // an A2UI stream's surfaces, between calls

    Impl(Runtime& rt, GenerationOptions given) : runtime(rt), options(std::move(given)) {
        for (const Surface& surface : options.surfaces) surfaces.try_emplace(surface.name);
    }

    ~Impl() {
        if (waiting) runtime.cancel(waiting);
    }

    // ── kept on disk ──
    std::filesystem::path document_file(std::string_view surface, std::size_t version) const {
        return options.directory / std::format("{}-{}.view.omui.json", surface, version);
    }

    void keep() const {
        if (options.directory.empty()) return;
        std::error_code error;
        std::filesystem::create_directories(options.directory, error);
        Value index(Value::Map{});
        for (const auto& [name, shown] : surfaces) {
            Value::List versions;
            for (const Version& version : shown.versions) {
                const auto file = document_file(name, version.number);
                if (!std::filesystem::exists(file)) (void)mcpplibs::os::fs::write_atomic(file, version.document);
                versions.push_back(Value{}
                                       .set("version", static_cast<std::int64_t>(version.number))
                                       .set("actor", version.actor)
                                       .set("draft", version.draft)
                                       .set("at", version.at));
            }
            index.set(name, Value{}.set("shown", static_cast<std::int64_t>(shown.current)).set("versions", Value(std::move(versions))));
        }
        if (auto written = mcpplibs::os::fs::write_atomic(options.directory / "index.json", Value{}.set("surfaces", std::move(index)).json()); !written)
            mlibs::log::warn("generation: cannot keep the versions in {}: {}", generic_utf8(options.directory), written.error().message());
    }

    // What was kept, read again: every version checked as a draft is now; the one shown, shown
    // once its slot is on screen.
    void restart(const std::shared_ptr<Impl>& self) {
        if (options.directory.empty()) return;
        auto text = mcpplibs::os::fs::read(options.directory / "index.json");
        if (!text) return;
        const auto index = parse_json(*text);
        const Value* kept = index ? index->get("surfaces") : nullptr;
        if (!kept || !kept->map()) return;
        for (auto& [name, shown] : surfaces) {
            const Value* surface = kept->get(name);
            if (!surface || !surface->get("versions") || !surface->get("versions")->list()) continue;
            for (const Value& entry : *surface->get("versions")->list()) {
                const std::size_t number = static_cast<std::size_t>(entry.get("version") ? entry.get("version")->as_int() : 0);
                auto document = mcpplibs::os::fs::read(document_file(name, number));
                Version version{number, document ? *document : std::string(), text_arg(entry, "actor"), text_arg(entry, "draft"),
                                entry.get("at") ? entry.get("at")->as_double() : 0, {}};
                std::string problem = document ? std::string() : std::format("version {}'s document is gone", number);
                if (problem.empty())
                    if (auto valid = validate(version.document, std::format("{}-{}", name, number)); !valid) problem = valid.error();
                if (problem.empty()) {
                    auto view = viewdoc::View::load(runtime, version.document, {.file = generic_utf8(document_file(name, number)), .provide = options.provide});
                    if (view) version.view = std::move(*view);
                    else problem = std::format("version {} could not be prepared again", number);
                }
                if (!problem.empty()) version.document.clear();  // kept in the list, never shown
                if (!problem.empty() && shown.problem.empty()) shown.problem = problem;
                shown.versions.push_back(std::move(version));
            }
            const std::size_t current = static_cast<std::size_t>(surface->get("shown") ? surface->get("shown")->as_int() : 0);
            if (current > 0 && current <= shown.versions.size() && !shown.versions[current - 1].document.empty()) shown.current = current;
            else if (current > 0 && shown.problem.empty()) shown.problem = std::format("version {} is not kept", current);
        }
        show_when_claimed(self, 0);
    }

    void show_when_claimed(const std::shared_ptr<Impl>& self, int tries) {
        bool all = true;
        for (auto& [name, shown] : surfaces)
            if (shown.current > 0 && !runtime.claimed(name)) all = false;
        if (all || tries > 200) {  // ten seconds: a slot not shown by then is shown by the next commit
            waiting = 0;
            for (auto& [name, shown] : surfaces)
                if (shown.current > 0 && runtime.claimed(name)) show(name, shown.current);
            return;
        }
        const std::weak_ptr<Impl> alive = self;
        waiting = runtime.after(seconds(0.05), [alive, tries] {
            if (auto impl = alive.lock()) impl->show_when_claimed(impl, tries + 1);
        });
    }

    static std::string_view stage_name(Stage stage) {
        switch (stage) {
        case Stage::validated: return "validated";
        case Stage::prepared:  return "prepared";
        case Stage::previewed: return "previewed";
        }
        return "validated";
    }

    std::expected<Shown*, std::string> surface_of(std::string_view name) {
        const auto found = surfaces.find(name);
        if (found == surfaces.end()) {
            std::string names;
            for (const Surface& surface : options.surfaces) names += (names.empty() ? "" : ", ") + surface.name;
            return std::unexpected(std::format("no surface `{}` (the application declares: {})", name, names.empty() ? "none" : names));
        }
        return &found->second;
    }

    std::expected<Draft*, std::string> draft_of(const Value& args) {
        const std::string id = text_arg(args, "draft");
        const auto found = drafts.find(id);
        if (found == drafts.end()) return std::unexpected(std::format("no draft `{}` (generate.drafts lists them)", id));
        return &found->second;
    }

    using Presented = Runtime::DesignPresentation::Kind;

    // What the surface shows: a version (0: nothing) — presented as Live Design presents a step
    // (W5 step 4): what it adds grows in, an undo plays back, a version restored is a switch. A
    // preview shows it still (none).
    void show(std::string_view surface, std::size_t version, std::optional<Presented> presented = std::nullopt) {
        Shown& shown = surfaces.find(surface)->second;
        const Runtime::DesignPresentation before = runtime.design_presentation();
        if (presented) {
            runtime.set_design_presentation({*presented, before.pace});
            runtime.present_design();
        }
        if (version == 0) runtime.unmount(surface);
        else shown.versions[version - 1].view.mount(runtime, surface);
        if (presented) {
            settle_effects();  // the slot's region takes it now, while the presentation is this one
            runtime.set_design_presentation(before);
        }
        shown.current = version;
        if (presented) keep();  // a step (not a preview's showing): what is shown is kept
    }

    // The step undo takes back: the surface from one version to another.
    void into_history(std::string_view surface, std::size_t from, std::size_t to, const std::shared_ptr<Impl>& self) {
        const std::weak_ptr<Impl> alive = self;
        const std::string name(surface);
        runtime.taken_back_by([alive, name, from] { if (auto impl = alive.lock()) impl->show(name, from, Presented::undo); },
                              [alive, name, to] { if (auto impl = alive.lock()) impl->show(name, to, Presented::edit); });
    }

    // A document, checked: its openness, its limits, then against what is registered.
    std::expected<std::string, std::string> validate(const std::string& document, std::string_view file) {
        if (document.size() > options.max_bytes)
            return std::unexpected(std::format("the document is {} bytes, more than the {} a draft may have", document.size(), options.max_bytes));
        viewdoc::ReadResult read = viewdoc::read(document, {.file = std::string(file), .verbs = &runtime.verbs()});
        if (!read.ok()) {
            std::string problems;
            for (const viewdoc::Diagnostic& problem : read.diagnostics)
                if (problem.level == viewdoc::Diagnostic::Level::error) problems += std::format("\n  {} (at {})", problem.text(file), problem.pointer);
            return std::unexpected(std::format("the draft does not validate:{}", problems));
        }
        if (read.document->component) return std::unexpected(std::string("a draft is a view document, not a component document"));
        Census census;
        count(read.document->root, census);
        if (census.nodes > options.max_nodes)
            return std::unexpected(std::format("the document has {} nodes, more than the {} a draft may have", census.nodes, options.max_nodes));
        if (options.openness == Openness::data && !census.verbs.empty())
            return std::unexpected(std::format("generation here takes data only: the draft invokes {} (verbs need limited or all)", *census.verbs.begin()));
        if (options.openness == Openness::limited) {
            for (const std::string& component : census.components)
                if (!std::ranges::contains(options.components, component))
                    return std::unexpected(std::format("`{}` is not among the components a draft may use here (generate.surfaces lists them)", component));
            for (const std::string& verb : census.verbs)
                if (!std::ranges::contains(options.verbs, verb))
                    return std::unexpected(std::format("`{}` is not among the verbs a draft may invoke here (generate.surfaces lists them)", verb));
        }
        return document;
    }

    // An event a generated view emitted, kept for the agent (generate.actions): an A2UI button's
    // action as A2UI has it — its context gathered from the `context.` arguments.
    void acted(std::string_view surface, std::string_view event, const Value& args) {
        Value entry = Value{}.set("n", next_action++).set("surface", std::string(surface)).set("event", std::string(event)).set("at", runtime.now().count());
        Value context(Value::Map{});
        if (args.map())
            for (const auto& [key, value] : *args.map()) {
                if (key.starts_with("context.")) context.set(key.substr(8), value);
                else entry.set(key, value);
            }
        if (context.map() && !context.map()->empty()) entry.set("context", std::move(context));
        actions.push_back(std::move(entry));
        if (actions.size() > actions_kept) actions.pop_front();
    }

    VerbResult draft(const Value& args, const std::shared_ptr<Impl>& self) {
        if (options.openness == Openness::off) return std::unexpected(std::string("generation is off in this application"));
        const std::string surface = text_arg(args, "surface");
        if (auto known = surface_of(surface); !known) return std::unexpected(known.error());
        std::string document;
        const std::string code = text_arg(args, "code");
        const bool background = args.get("background") && args.get("background")->as_bool();
        if (background && !options.tasks) return std::unexpected(std::string("this host runs no background tasks: draft without `background`"));
        if (!code.empty()) {
            if (options.openness == Openness::data) return std::unexpected(std::string("generation here takes data only: a view document, not code"));
            if (!options.evaluate) return std::unexpected(std::string("this host runs no generated code: send a view document"));
            const std::string language = text_arg(args, "language").empty() ? std::string("ts") : text_arg(args, "language");
            if (background) return in_background(surface, code, args, self, language);
            auto made = options.evaluate(language, code);
            if (!made) return std::unexpected(std::format("the code did not make a document: {}", made.error()));
            document = std::move(*made);
        } else if (const std::string piece = text_arg(args, "piece"); !piece.empty()) {
            const auto kept = pieces.find(piece);
            if (kept == pieces.end()) return std::unexpected(std::format("no piece `{}` (generate.pieces lists them)", piece));
            document = kept->second.document;
        } else if (const Value* given = args.get("document"); given && given->map()) {
            document = given->json();
        } else if (given && given->type() == Value::Type::text) {
            document = std::string(given->as_string());
        } else {
            return std::unexpected(std::string("a draft is a `document` (a view document) or, where code is allowed, `code`"));
        }
        if (args.get("background") && args.get("background")->as_bool()) return in_background(surface, std::move(document), args, self);
        const std::string id = std::format("d{}", next_draft);
        auto valid = validate(document, id);
        if (!valid) return std::unexpected(valid.error());
        // Prepared: built, its signals made, nothing shown.
        auto view = viewdoc::View::load(runtime, document, {.file = id, .provide = options.provide});
        if (!view) {
            std::string problems;
            for (const viewdoc::Diagnostic& problem : view.error()) problems += std::format("\n  {}", problem.text(id));
            return std::unexpected(std::format("the draft could not be prepared:{}", problems));
        }
        ++next_draft;
        const std::weak_ptr<Impl> alive = self;
        view->on_any([alive, surface](std::string_view event, const Value& emitted) {
            if (auto impl = alive.lock()) impl->acted(surface, event, emitted);
        });
        const std::size_t base = surfaces.find(surface)->second.current;
        Value answer = Value{}
                           .set("draft", id)
                           .set("surface", surface)
                           .set("stage", "prepared")
                           .set("bytes", static_cast<std::int64_t>(document.size()))
                           .set("over", static_cast<std::int64_t>(base))
                           .set("set", read_write_set(view->document(), surface));
        drafts.insert_or_assign(id, Draft{id, surface, document, actor_now(runtime), Stage::prepared, std::move(*view), base});
        return answer;
    }

    // A draft made as a task (W8): answered at once with its handle; the code evaluated on the pool,
    // then — on the UI thread, at a frame's boundary — drafted, and committed when asked, as the
    // verbs are, in the name of who asked (decided again then). A cancelled task commits nothing.
    VerbResult in_background(const std::string& surface, std::string text, const Value& args, const std::shared_ptr<Impl>& self,
                             std::string language = {}) {
        const bool commit_now = args.get("commit") && args.get("commit")->as_bool();
        Invocation asked = runtime.current_invocation().value_or(Invocation{});
        const std::weak_ptr<Impl> alive = self;
        EvaluateFn evaluate = language.empty() ? EvaluateFn{} : options.evaluate;
        const std::string id = options.tasks->submit(
            {.name = std::format("a draft for {}{}", surface, commit_now ? ", committed" : ""),
             .work = [alive, surface, text = std::move(text), language, evaluate, commit_now, asked](std::stop_token stop, Progress& progress) -> TaskWork {
                 std::string document = text;
                 if (evaluate) {
                     progress.set(0.1, "evaluating the code");
                     auto made = evaluate(language, text);
                     if (!made) return std::unexpected(std::format("the code did not make a document: {}", made.error()));
                     document = std::move(*made);
                 }
                 if (stop.stop_requested()) return std::unexpected(std::string("cancelled"));
                 progress.set(0.8, "checking and preparing");
                 return TaskCommit([alive, surface, document = std::move(document), commit_now, asked]() -> VerbResult {
                     const auto impl = alive.lock();
                     if (!impl) return std::unexpected(std::string("the application's generation is gone"));
                     Invocation call = asked;
                     call.args = Value{}.set("surface", surface).set("document", document);
                     const Envelope drafted = impl->runtime.invoke("generate.draft", call);
                     const Value* made = drafted.data_value();
                     if (drafted.exit_code().value_or(1) != 0 || !made) return std::unexpected(std::format("the draft: {}", drafted.ndjson()));
                     Value out = *made;
                     if (!commit_now) return out;
                     call.args = Value{}.set("draft", *made->get("draft"));
                     const Envelope committed = impl->runtime.invoke("generate.commit", call);
                     if (committed.exit_code().value_or(1) != 0 || !committed.data_value())
                         return std::unexpected(std::format("the commit: {}", committed.ndjson()));
                     return out.set("version", *committed.data_value()->get("version"));
                 });
             }});
        return Value{}.set("task", id).set("surface", surface).set("stage", "submitted");
    }

    VerbResult preview(const Value& args) {
        auto found = draft_of(args);
        if (!found) return std::unexpected(found.error());
        Draft& draft = **found;
        if (!runtime.claimed(draft.surface)) return std::unexpected(std::format("the surface `{}` is not on screen now: nothing shows it", draft.surface));
        const Rect window{0, 0, runtime.size().width, runtime.size().height};
        const auto picture = [&]() -> Value {
            auto png = options.capture ? options.capture(window) : runtime.capture_png(window);
            if (!png && !options.capture) return Value{};  // a host that draws no pixels: the changes say it
            if (!png) return Value{}.set("error", png.error());
            return Value{}.set("format", "png").set("base64", mlibs::base64::encode(*png));
        };
        // A frame at this very time, the effects the mount scheduled run first (a verb runs inside a
        // batch: its writes would otherwise show only after it returns).
        const auto settle = [&] {
            settle_effects();
            FrameInput still;
            still.time = runtime.now();
            (void)runtime.frame(still);
        };
        semantic::Tree tree(runtime);
        const semantic::Snapshot before = tree.snapshot({});
        Value seen_before = picture();
        Shown& shown = surfaces.find(draft.surface)->second;
        const std::size_t current = shown.current;
        runtime.begin_preview();
        draft.view.mount(runtime, draft.surface);
        settle();
        Value seen_after = picture();
        const semantic::Snapshot after = tree.snapshot({});
        show(draft.surface, current);
        settle();
        runtime.end_preview();
        draft.stage = Stage::previewed;
        Value out = Value{}.set("draft", draft.id).set("stage", "previewed").set("changes", semantic::changes_between(before, after));
        if (!seen_before.is_null()) out.set("before", std::move(seen_before)).set("after", std::move(seen_after));
        return out;
    }

    VerbResult commit(const Value& args, const std::shared_ptr<Impl>& self) {
        auto found = draft_of(args);
        if (!found) return std::unexpected(found.error());
        Draft& draft = **found;
        if (!runtime.claimed(draft.surface)) return std::unexpected(std::format("the surface `{}` is not on screen now: nothing shows it", draft.surface));
        Shown& shown = surfaces.find(draft.surface)->second;
        if (shown.current != draft.base && !(args.get("over") && args.get("over")->as_bool()))
            return std::unexpected(std::format("conflict: `{}` shows version {} now; the draft was made over version {} — draft it again over what is there, "
                                               "or commit with `over` to replace it",
                                               draft.surface, shown.current, draft.base));
        const std::size_t before = shown.current, number = shown.versions.size() + 1;
        shown.versions.push_back({number, draft.document, actor_now(runtime), draft.id, runtime.now().count(), draft.view});
        const std::string surface = draft.surface, id = draft.id;
        drafts.erase(id);
        show(surface, number, Presented::edit);
        into_history(surface, before, number, self);
        return Value{}.set("surface", surface).set("version", static_cast<std::int64_t>(number)).set("before", static_cast<std::int64_t>(before)).set("draft", id);
    }

    VerbResult restore(const Value& args, const std::shared_ptr<Impl>& self) {
        const std::string surface = text_arg(args, "surface");
        auto known = surface_of(surface);
        if (!known) return std::unexpected(known.error());
        const Value* given = args.get("version");
        const std::int64_t version = given && given->is_number() ? given->as_int() : -1;
        if (version < 0 || static_cast<std::size_t>(version) > (*known)->versions.size())
            return std::unexpected(std::format("`{}` has versions 1 to {} (0: none)", surface, (*known)->versions.size()));
        const std::size_t before = (*known)->current;
        show(surface, static_cast<std::size_t>(version), Presented::switch_to);
        into_history(surface, before, static_cast<std::size_t>(version), self);
        return Value{}.set("surface", surface).set("version", version).set("before", static_cast<std::int64_t>(before));
    }

    VerbResult versions(const Value& args) {
        const std::string surface = text_arg(args, "surface");
        auto known = surface_of(surface);
        if (!known) return std::unexpected(known.error());
        Value::List list;
        for (const Version& version : (*known)->versions)
            list.push_back(Value{}
                               .set("version", static_cast<std::int64_t>(version.number))
                               .set("actor", version.actor)
                               .set("draft", version.draft)
                               .set("at", version.at)
                               .set("document", parse_json(version.document).value_or(Value{})));
        return Value{}.set("surface", surface).set("shown", static_cast<std::int64_t>((*known)->current)).set("versions", Value(std::move(list)));
    }

    Value surfaces_value() const {
        Value::List list;
        for (const Surface& surface : options.surfaces) {
            const Shown& shown = surfaces.find(surface.name)->second;
            list.push_back(Value{}
                               .set("name", surface.name)
                               .set("doc", surface.doc)
                               .set("shown", runtime.claimed(surface.name))
                               .set("version", static_cast<std::int64_t>(shown.current))
                               .set("versions", static_cast<std::int64_t>(shown.versions.size()))
                               .set("problem", shown.problem.empty() ? Value{} : Value(shown.problem)));
        }
        Value out = Value{}
                        .set("openness", std::string(openness_name(options.openness)))
                        .set("code", options.openness >= Openness::limited && static_cast<bool>(options.evaluate))
                        .set("limits", Value{}.set("bytes", static_cast<std::int64_t>(options.max_bytes)).set("nodes", static_cast<std::int64_t>(options.max_nodes)))
                        .set("surfaces", Value(std::move(list)));
        if (options.openness == Openness::limited) {
            Value::List components, verbs;
            for (const std::string& name : options.components) components.push_back(Value(name));
            for (const std::string& name : options.verbs) verbs.push_back(Value(name));
            out.set("components", Value(std::move(components))).set("verbs", Value(std::move(verbs)));
        }
        return out;
    }

    VerbResult read_a2ui(const Value& args, const std::shared_ptr<Impl>& self) {
        const Value* given = args.get("messages");
        if (!given) return std::unexpected(std::string("generate.a2ui reads `messages`: A2UI messages, one, a list, or JSON lines"));
        const A2uiReader::Read read = a2ui.feed(given->type() == Value::Type::text ? std::string(given->as_string()) : given->json());
        if (!read.problems.empty()) {
            std::string problems;
            for (const std::string& problem : read.problems) problems += "\n  " + problem;
            return std::unexpected(std::format("the A2UI messages were not all read:{}", problems));
        }
        const std::string into = text_arg(args, "surface");
        const bool commit_now = args.get("commit") && args.get("commit")->as_bool();
        Value::List made;
        for (const A2uiReader::Rendered& rendered : read.rendered) {
            const std::string surface = into.empty() ? rendered.surface : into;
            auto drafted = draft(Value{}.set("surface", surface).set("document", rendered.document), self);
            if (!drafted) return std::unexpected(std::format("A2UI surface `{}`: {}", rendered.surface, drafted.error()));
            Value one = Value{}.set("a2ui", rendered.surface).set("surface", surface).set("draft", *drafted->get("draft"));
            if (commit_now) {
                auto committed = commit(Value{}.set("draft", *drafted->get("draft")), self);
                if (!committed) return std::unexpected(std::format("A2UI surface `{}`: {}", rendered.surface, committed.error()));
                one.set("version", *committed->get("version"));
            }
            made.push_back(std::move(one));
        }
        Value::List deleted;
        for (const std::string& gone : read.deleted) {
            const std::string surface = into.empty() ? gone : into;
            deleted.push_back(Value(gone));
            if (commit_now)
                if (auto known = surface_of(surface); known && (*known)->current != 0) {
                    const std::size_t before = (*known)->current;
                    show(surface, 0, Presented::undo);
                    into_history(surface, before, 0, self);
                }
        }
        return Value{}.set("drafts", Value(std::move(made))).set("deleted", Value(std::move(deleted)));
    }

    Value actions_value(const Value& args) const {
        const Value* since = args.get("since");
        const std::int64_t after = since && since->is_number() ? since->as_int() : 0;
        Value::List list;
        for (const Value& entry : actions)
            if (entry.get("n")->as_int() > after) list.push_back(entry);
        return Value{}.set("actions", Value(std::move(list))).set("last", next_action - 1);
    }

    // ── pieces kept for reuse ──
    void keep_pieces() const {
        if (options.directory.empty()) return;
        std::error_code error;
        std::filesystem::create_directories(options.directory, error);
        Value::List list;
        for (const auto& [name, piece] : pieces)
            list.push_back(Value{}.set("name", name).set("doc", piece.doc).set("actor", piece.actor).set("from", piece.from)
                               .set("document", parse_json(piece.document).value_or(Value{})));
        (void)mcpplibs::os::fs::write_atomic(options.directory / "pieces.json", Value{}.set("pieces", Value(std::move(list))).json());
    }

    void read_pieces() {
        if (options.directory.empty()) return;
        auto text = mcpplibs::os::fs::read(options.directory / "pieces.json");
        if (!text) return;
        const auto kept = parse_json(*text);
        const Value* list = kept ? kept->get("pieces") : nullptr;
        if (!list || !list->list()) return;
        for (const Value& entry : *list->list())
            if (const Value* document = entry.get("document"); document && document->map())
                pieces.insert_or_assign(text_arg(entry, "name"),
                                        Piece{text_arg(entry, "name"), document->json(), text_arg(entry, "doc"), text_arg(entry, "actor"), text_arg(entry, "from")});
    }

    VerbResult keep_piece(const Value& args) {
        const std::string surface = text_arg(args, "surface"), name = text_arg(args, "name");
        auto known = surface_of(surface);
        if (!known) return std::unexpected(known.error());
        if (name.empty() || !std::ranges::all_of(name, [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; }))
            return std::unexpected(std::string("a piece's name is lower-case words joined by _"));
        const Value* given = args.get("version");
        const std::size_t version = given && given->is_number() ? static_cast<std::size_t>(given->as_int()) : (*known)->current;
        if (version == 0 || version > (*known)->versions.size() || (*known)->versions[version - 1].document.empty())
            return std::unexpected(std::format("`{}` has no version {} to keep", surface, version));
        const std::string from = std::format("{} version {}", surface, version);
        pieces.insert_or_assign(name, Piece{name, (*known)->versions[version - 1].document, text_arg(args, "doc"), actor_now(runtime), from});
        keep_pieces();
        return Value{}.set("piece", name).set("from", from);
    }

    Value pieces_value() const {
        Value::List list;
        for (const auto& [name, piece] : pieces)
            list.push_back(Value{}.set("name", name).set("doc", piece.doc).set("actor", piece.actor).set("from", piece.from));
        return Value{}.set("pieces", Value(std::move(list)));
    }

    VerbResult forget(const Value& args) {
        const std::string name = text_arg(args, "name");
        if (!pieces.erase(name)) return std::unexpected(std::format("no piece `{}` (generate.pieces lists them)", name));
        keep_pieces();
        return Value{}.set("piece", name).set("forgotten", true);
    }

    Value drafts_value() const {
        Value::List list;
        for (const auto& [id, draft] : drafts)
            list.push_back(Value{}.set("draft", id).set("surface", draft.surface).set("stage", std::string(stage_name(draft.stage))).set("actor", draft.actor));
        return Value{}.set("drafts", Value(std::move(list)));
    }
};

Generation::Generation(Runtime& runtime, GenerationOptions options) : impl_(std::make_shared<Impl>(runtime, std::move(options))) {
    impl_->restart(impl_);
    impl_->read_pieces();
    const auto add = [&](std::string_view name, VerbFn fn) {
        const auto entry = std::ranges::find(agent::generate::catalog, name, [](const auto& op) { return op.operation.name; });
        if (entry == agent::generate::catalog.end()) return;
        if (runtime.verbs().add(agent::verb_of(*entry), std::move(fn))) impl_->added.emplace_back(name);
    };
    add("generate.surfaces", [this](const Value&) { return surfaces(); });
    add("generate.draft", [this](const Value& args) { return draft(args); });
    add("generate.preview", [this](const Value& args) { return preview(args); });
    add("generate.commit", [this](const Value& args) { return commit(args); });
    add("generate.restore", [this](const Value& args) { return restore(args); });
    add("generate.versions", [this](const Value& args) { return versions(args); });
    add("generate.drafts", [this](const Value&) { return drafts(); });
    add("generate.discard", [this](const Value& args) { return discard(args); });
    add("generate.a2ui", [this](const Value& args) { return a2ui(args); });
    add("generate.actions", [this](const Value& args) { return actions(args); });
    add("generate.keep", [this](const Value& args) { return keep(args); });
    add("generate.pieces", [this](const Value&) { return pieces(); });
    add("generate.forget", [this](const Value& args) { return forget(args); });
}

Generation::~Generation() {
    for (const std::string& name : impl_->added) (void)impl_->runtime.verbs().remove(name);
}

VerbResult Generation::surfaces() const { return impl_->surfaces_value(); }
VerbResult Generation::draft(const Value& args) { return impl_->draft(args, impl_); }
VerbResult Generation::a2ui(const Value& args) { return impl_->read_a2ui(args, impl_); }
VerbResult Generation::actions(const Value& args) const { return impl_->actions_value(args); }
VerbResult Generation::keep(const Value& args) { return impl_->keep_piece(args); }
VerbResult Generation::pieces() const { return impl_->pieces_value(); }
VerbResult Generation::forget(const Value& args) { return impl_->forget(args); }
VerbResult Generation::preview(const Value& args) { return impl_->preview(args); }
VerbResult Generation::commit(const Value& args) { return impl_->commit(args, impl_); }
VerbResult Generation::restore(const Value& args) { return impl_->restore(args, impl_); }
VerbResult Generation::versions(const Value& args) const { return impl_->versions(args); }
VerbResult Generation::drafts() const { return impl_->drafts_value(); }

VerbResult Generation::discard(const Value& args) {
    auto found = impl_->draft_of(args);
    if (!found) return std::unexpected(found.error());
    const std::string id = (*found)->id, surface = (*found)->surface;
    impl_->drafts.erase(id);
    return Value{}.set("draft", id).set("surface", surface).set("discarded", true);
}

}  // namespace omui::generate
