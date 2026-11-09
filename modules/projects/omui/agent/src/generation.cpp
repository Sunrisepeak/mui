// Runtime generation's stages and verbs (omui.agent:generation).
module omui.agent;

import std;
import omui.core;
import omui.view;
import omui.semantic;
import omui.viewdoc;
import mlibs.base64;
import mlibs.control;

namespace omui::agent {

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
    };
    struct Version {
        std::size_t   number = 0;
        std::string   document, actor, draft;
        double        at = 0;  // the runtime's time
        viewdoc::View view;
    };
    struct Shown {
        std::vector<Version> versions;
        std::size_t          current = 0;  // 0: nothing
    };
    std::map<std::string, Draft, std::less<>> drafts;
    std::map<std::string, Shown, std::less<>> surfaces;
    std::size_t                               next_draft = 1;
    std::vector<std::string>                  added;  // the verbs, removed with it

    Impl(Runtime& rt, GenerationOptions given) : runtime(rt), options(std::move(given)) {
        for (const Surface& surface : options.surfaces) surfaces.try_emplace(surface.name);
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

    VerbResult draft(const Value& args) {
        if (options.openness == Openness::off) return std::unexpected(std::string("generation is off in this application"));
        const std::string surface = text_arg(args, "surface");
        if (auto known = surface_of(surface); !known) return std::unexpected(known.error());
        std::string document;
        const std::string code = text_arg(args, "code");
        if (!code.empty()) {
            if (options.openness == Openness::data) return std::unexpected(std::string("generation here takes data only: a view document, not code"));
            if (!options.evaluate) return std::unexpected(std::string("this host runs no generated code: send a view document"));
            const std::string language = text_arg(args, "language").empty() ? std::string("ts") : text_arg(args, "language");
            auto made = options.evaluate(language, code);
            if (!made) return std::unexpected(std::format("the code did not make a document: {}", made.error()));
            document = std::move(*made);
        } else if (const Value* given = args.get("document"); given && given->map()) {
            document = given->json();
        } else if (given && given->type() == Value::Type::text) {
            document = std::string(given->as_string());
        } else {
            return std::unexpected(std::string("a draft is a `document` (a view document) or, where code is allowed, `code`"));
        }
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
        drafts.insert_or_assign(id, Draft{id, surface, document, actor_now(runtime), Stage::prepared, std::move(*view)});
        return Value{}.set("draft", id).set("surface", surface).set("stage", "prepared").set("bytes", static_cast<std::int64_t>(document.size()));
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
                               .set("versions", static_cast<std::int64_t>(shown.versions.size())));
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

    Value drafts_value() const {
        Value::List list;
        for (const auto& [id, draft] : drafts)
            list.push_back(Value{}.set("draft", id).set("surface", draft.surface).set("stage", std::string(stage_name(draft.stage))).set("actor", draft.actor));
        return Value{}.set("drafts", Value(std::move(list)));
    }
};

Generation::Generation(Runtime& runtime, GenerationOptions options) : impl_(std::make_shared<Impl>(runtime, std::move(options))) {
    const auto add = [&](std::string_view name, VerbFn fn) {
        const auto entry = std::ranges::find(generate::catalog, name, [](const auto& op) { return op.operation.name; });
        if (entry == generate::catalog.end()) return;
        if (runtime.verbs().add(verb_of(*entry), std::move(fn))) impl_->added.emplace_back(name);
    };
    add("generate.surfaces", [this](const Value&) { return surfaces(); });
    add("generate.draft", [this](const Value& args) { return draft(args); });
    add("generate.preview", [this](const Value& args) { return preview(args); });
    add("generate.commit", [this](const Value& args) { return commit(args); });
    add("generate.restore", [this](const Value& args) { return restore(args); });
    add("generate.versions", [this](const Value& args) { return versions(args); });
    add("generate.drafts", [this](const Value&) { return drafts(); });
    add("generate.discard", [this](const Value& args) { return discard(args); });
}

Generation::~Generation() {
    for (const std::string& name : impl_->added) (void)impl_->runtime.verbs().remove(name);
}

VerbResult Generation::surfaces() const { return impl_->surfaces_value(); }
VerbResult Generation::draft(const Value& args) { return impl_->draft(args); }
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

}  // namespace omui::agent
