module omui.ui.base;

import std;
import omui.core;
import omui.view;
import mlibs.agent;

namespace omui {

namespace {

constexpr float row_h = 28, pad_x = 8, time_w = 44, dot = 8;

// "{code}" and "{agent}" in a phrase, filled in.
std::string filled(std::string text, const AgentLink& l) {
    const std::string agent = l.agent.empty() ? std::string("Your agent") : l.agent;  // held: the views below read it
    for (const auto& [key, with] : {std::pair<std::string_view, std::string_view>{"{code}", l.code}, {"{agent}", agent}})
        for (auto at = text.find(key); at != std::string::npos; at = text.find(key, at + with.size())) text.replace(at, key.size(), with);
    return text;
}

// A path as a record says it: its last two segments (the node and what holds it); the window's, app.
std::string short_path(std::string_view path) {
    if (path.empty()) return "app";
    int seen = 0;
    for (std::size_t i = path.size(); i-- > 0;)
        if (path[i] == '/' && ++seen == 2) return std::format("…{}", path.substr(i));
    return std::string(path);
}

// What an action was given, briefly: its value, else its arguments.
std::string given(const Value& args) {
    if (args.is_null()) return {};
    if (const auto* m = args.map(); m && m->size() == 1 && args.get("value")) return args.get("value")->json();
    std::string j = args.json();
    if (j == "{}") return {};
    return j.size() > 40 ? j.substr(0, 39) + "…" : j;
}

// Who, as a record says it: the actor's own name (agent:guide → guide).
// What a card over the page should not cover: the boxes of the page's leaves
// (window coordinates), but the presence's own and those inside the node acted on.
std::vector<Rect> page_leaves(const Services& services, NodeId presence, const Rect& acted_on) {
    NodeId top = presence;
    while (services.parent(top)) top = services.parent(top);
    std::vector<Rect> out;
    std::vector<NodeId> pending{top};
    for (std::size_t visited = 0; !pending.empty() && visited < 4096; ++visited) {
        const NodeId node = pending.back();
        pending.pop_back();
        if (node == presence) continue;
        const auto children = services.children(node);
        if (!children.empty()) {
            pending.insert(pending.end(), children.begin(), children.end());
            continue;
        }
        const Rect box = services.bounds(node);
        const bool inside = box.x >= acted_on.x && box.y >= acted_on.y && box.right() <= acted_on.right() && box.bottom() <= acted_on.bottom();
        if (!box.empty() && !inside) out.push_back(box);
    }
    return out;
}

float overlap(const Rect& a, const Rect& b) {
    const float width = std::min(a.right(), b.right()) - std::max(a.x, b.x), height = std::min(a.bottom(), b.bottom()) - std::max(a.y, b.y);
    return width > 0 && height > 0 ? width * height : 0;
}

std::string_view who(std::string_view actor) {
    const auto colon = actor.find(':');
    return colon == std::string_view::npos ? actor : actor.substr(colon + 1);
}

std::optional<std::uint64_t> entry_of(std::string_view key) {
    std::uint64_t i = 0;
    if (key.empty() || std::from_chars(key.data(), key.data() + key.size(), i).ec != std::errc{}) return std::nullopt;
    return i;
}

}  // namespace

void ActorLog::mounted() {
    status_.set_style("label");
    status_.set_max_lines(1);
    empty_.set_style("label");
    services().follow_actions(node(), true);  // a new record, a link's new state: laid out and stated again
}

void ActorLog::prop_changed(ActorLogProp) {
    if (!attached()) return;
    services().invalidate_layout(node());
    services().invalidate_semantics(node());
}

std::string ActorLog::phrase(std::string_view key) const {
    if (const Value* own = props.phrases.get(key); own && !own->as_string().empty()) return std::string(own->as_string());
    static constexpr std::array<std::pair<std::string_view, std::string_view>, 10> english{{
        {"idle", "Your agent is not linked."},
        {"waiting", "Waiting for your agent — code {code}"},
        {"paired", "{agent} is linked"},
        {"lost", "{agent}'s bridge went; looking for it again"},
        {"expired", "The code expired: link again for a new one"},
        {"stopped", "The link ended"},
        {"empty", "No agent has acted here."},
        {"undo", "Undo"},
        {"asked", "asked you"},
        {"refused", "refused"},
    }};
    for (const auto& [k, v] : english)
        if (k == key) return std::string(v);
    return std::string(key);
}

std::string ActorLog::status() const {
    if (!props.link || !attached()) return {};
    const AgentLink& l = services().agent_link();
    if (l.state == "unavailable") return {};
    return filled(phrase(l.state), l);
}

std::vector<const ActionEntry*> ActorLog::records() const {
    std::vector<const ActionEntry*> out;
    if (!attached()) return out;
    const auto journal = services().action_journal();
    const std::size_t limit = static_cast<std::size_t>(std::max<std::int64_t>(0, props.limit));
    // Not the undos asked of its own records: the scope's undo they performed is the record of it.
    const std::string own = services().path(node()) + "/";
    for (auto it = journal.rbegin(); it != journal.rend() && out.size() < limit; ++it)
        if (it->actor.starts_with(props.actors) && !it->path.starts_with(own)) out.push_back(&*it);
    std::ranges::reverse(out);
    return out;
}

std::string ActorLog::said(const ActionEntry& e) const {
    const std::string args = given(e.args);
    const std::string tag = e.exit_code == spec::exit_code::success      ? std::string()
                            : e.exit_code == spec::exit_code::needs_answer ? std::format(" — {}", phrase("asked"))
                                                                           : std::format(" — {}", phrase("refused"));
    return std::format("{} {} {}{}{}{}", who(e.actor), e.action, short_path(e.path), args.empty() ? "" : " ", args, tag);
}

Size ActorLog::measure(MeasureContext&, std::optional<float> width, std::optional<float> height, mlibs::layout::Available, mlibs::layout::Available) {
    const std::size_t n = records().size();
    const float h = (status().empty() ? 0.0f : row_h) + static_cast<float>(std::max<std::size_t>(n, 1)) * row_h;
    return {width.value_or(320), height.value_or(h)};
}

void ActorLog::paint(PaintContext& ctx) const {
    const Theme& th = ctx.theme;
    const float w = ctx.size.width;
    float y = 0;
    if (const std::string st = status(); !st.empty()) {
        // The link: a dot in its state's colour, then what it is.
        const std::string& state = services().agent_link().state;
        const Color c = state == "paired"                         ? th.color("success")
                        : state == "waiting"                      ? th.color("accent")
                        : state == "lost" || state == "expired"   ? th.color("warning")
                                                                  : th.color("text_disabled");
        ctx.list.fill_round_rect({pad_x, y + (row_h - dot) / 2, dot, dot}, CornerRadii::all(dot / 2), xrender::Paint::solid(c));
        status_.set_text(st);
        const Size t = status_.measure(ctx.services, th.color("text"), w - (2 * pad_x + dot + 8));
        status_.paint(ctx, {pad_x + dot + 8, y + (row_h - t.height) / 2}, th.color("text"));
        y += row_h;
    }
    const auto recs = records();
    rows_.clear();
    undo_at_.clear();
    shown_.clear();
    if (recs.empty()) {
        empty_.set_text(phrase("empty"));
        const Size t = empty_.measure(ctx.services, th.color("text_muted"), w - 2 * pad_x);
        empty_.paint(ctx, {pad_x, y + (row_h - t.height) / 2}, th.color("text_muted"));
        return;
    }
    times_.resize(recs.size());
    texts_.resize(recs.size());
    undos_.resize(recs.size());
    // When, from its actors' first record on: how far into their session each came.
    double since = recs.front()->at;
    for (const ActionEntry& e : services().action_journal())
        if (e.actor.starts_with(props.actors)) {
            since = e.at;
            break;
        }
    for (std::size_t i = 0; i < recs.size(); ++i) {
        const ActionEntry& e = *recs[i];
        const Rect r{0, y, w, row_h};
        rows_.push_back(r);
        shown_.push_back(e.id);
        if (hover_ == static_cast<int>(i)) ctx.list.fill_round_rect(r, CornerRadii::all(4), xrender::Paint::solid(th.color("surface_variant")));
        // When (m:ss since the first), what, and its Undo while it is the last its history holds.
        parts::Label& when = times_[i];
        when.set_style("caption");
        const auto s = static_cast<std::int64_t>(e.at - since);
        when.set_text(std::format("{}:{:02}", s / 60, s % 60));
        const Size tw = when.measure(ctx.services, th.color("text_muted"));
        when.paint(ctx, {pad_x, y + (row_h - tw.height) / 2}, th.color("text_muted"));
        float right = w - pad_x;
        Rect undo_box{};
        if (services().undoes(e.id)) {
            parts::Label& u = undos_[i];
            u.set_style("label");
            u.set_text(phrase("undo"));
            const Size uw = u.measure(ctx.services, th.color("accent"));
            undo_box = {right - uw.width - 8, y + 4, uw.width + 8, row_h - 8};
            u.paint(ctx, {undo_box.x + 4, y + (row_h - uw.height) / 2}, th.color("accent"));
            right = undo_box.x - 8;
        }
        undo_at_.push_back(undo_box);
        parts::Label& text = texts_[i];
        text.set_style("label");
        text.set_max_lines(1);
        text.set_text(said(e));
        const Color tc = e.exit_code == spec::exit_code::success ? th.color("text") : th.color("text_muted");
        const Size t = text.measure(ctx.services, tc, std::max(0.0f, right - (pad_x + time_w)));
        text.paint(ctx, {pad_x + time_w, y + (row_h - t.height) / 2}, tc);
        y += row_h;
    }
}

void ActorLog::paint_over(PaintContext& ctx) const {
    if (!(ctx.state.focused && ctx.state.focus_visible) || focus_ >= rows_.size()) return;
    parts::focus_ring(ctx, rows_[focus_], CornerRadii::all(4));
}

void ActorLog::point(int row) {
    const auto journal = services().action_journal();
    if (row >= 0 && static_cast<std::size_t>(row) < shown_.size() && shown_[row] >= 1 && shown_[row] <= journal.size()) {
        services().point_out(node(), journal[shown_[row] - 1].path);
        pointing_ = true;
    } else if (pointing_) {
        services().point_out(node(), "");
        pointing_ = false;
    }
}

bool ActorLog::on_event(EventContext& ctx, const InputEvent& e) {
    if (const auto* p = std::get_if<PointerEvent>(&e)) {
        int over = -1;
        for (std::size_t i = 0; i < rows_.size(); ++i)
            if (rows_[i].contains(ctx.local)) over = static_cast<int>(i);
        if (p->kind == PointerKind::leave) over = -1;
        if (over != hover_) {
            hover_ = over;
            point(over);  // the node it acted on, outlined: not a cursor
            ctx.repaint();
        }
        if (p->kind == PointerKind::up && over >= 0 && undo_at_[over].contains(ctx.local)) {
            (void)undo(shown_[over], nullptr);
            return true;
        }
        return over >= 0 && (p->kind == PointerKind::down || p->kind == PointerKind::up);
    }
    if (const auto* k = std::get_if<KeyEvent>(&e); k && k->kind == KeyKind::down && (k->key == Key::enter || k->key == Key::space)) {
        if (focus_ < shown_.size() && services().undoes(shown_[focus_])) (void)undo(shown_[focus_], nullptr);
        return true;
    }
    return false;
}

ActResult ActorLog::undo(std::uint64_t entry, const Actor* actor) {
    if (!services().undo_entry(entry, actor))
        return ActResult::fail(std::format("action {} is not the last its history holds now: undo what came after it first", entry));
    emit_undone(static_cast<std::int64_t>(entry));
    services().invalidate_paint(node());
    return ActResult::done(Value(static_cast<std::int64_t>(entry)));
}

spec::SemanticNode ActorLog::item(std::string_view path, const ActionEntry& e) const {
    spec::SemanticNode n;
    n.id = std::format("{}/{}", path, e.id);
    n.role = Role::list_item;
    n.name = said(e);
    n.value = Value{}.set("actor", e.actor).set("action", e.action).set("node", e.path).set("args", e.args).set("at", e.at).set(
        "exit_code", static_cast<std::int64_t>(e.exit_code));
    for (std::size_t i = 0; i < shown_.size() && attached(); ++i)
        if (shown_[i] == e.id) {
            const Rect b = services().bounds(node());
            n.bounds = {b.x + rows_[i].x, b.y + rows_[i].y, rows_[i].width, rows_[i].height};
        }
    if (attached() && services().undoes(e.id)) n.actions.push_back({"undo", Effect::reversible, {}, {}, {}});
    return n;
}

void ActorLog::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    if (node.name.empty()) node.name = "What agents did here";
    const std::string state = attached() ? services().agent_link().state : std::string("unavailable");
    node.value = Value{}.set("link", state).set("records", static_cast<std::int64_t>(records().size()));
}

std::vector<spec::SemanticNode> ActorLog::virtual_items(std::string_view path) const {
    std::vector<spec::SemanticNode> out;
    for (const ActionEntry* e : records()) out.push_back(item(path, *e));
    return out;
}

std::optional<spec::SemanticNode> ActorLog::virtual_item(std::string_view path, std::string_view key) const {
    const auto id = entry_of(key);
    if (!id) return std::nullopt;
    for (const ActionEntry* e : records())
        if (e->id == *id) return item(path, *e);
    return std::nullopt;
}

ActResult ActorLog::act_item(ActContext& ctx, std::string_view key, std::string_view action) {
    const auto id = entry_of(key);
    if (!id || action != "undo") return ActResult::fail(std::format("a record is undone, not '{}'", action));
    return undo(*id, ctx.actor);
}

ActResult ActorLog::next(ActContext&) {
    if (focus_ + 1 < shown_.size()) ++focus_, services().invalidate_paint(node());
    point(static_cast<int>(focus_));
    return ActResult::done();
}

ActResult ActorLog::previous(ActContext&) {
    if (focus_ > 0) --focus_, services().invalidate_paint(node());
    point(static_cast<int>(focus_));
    return ActResult::done();
}

// ── Another actor at work, seen ──

namespace {

// One actor's presence (`exact`), or the newest at work of those whose names begin with `actors`.
AgentPresenceState presence_of(const Services& services, std::string_view actors, bool exact, std::size_t trail, std::string_view except) {
    AgentPresenceState out;
    const auto matches = [&](std::string_view actor) { return exact ? actor == actors : actor.starts_with(actors); };
    const auto step_of = [&](const ActionEntry& e) {
        PresenceStep step{e.path, e.action, {}, e.at};
        if (const NodeId n = services.find(e.path); n) step.bounds = services.bounds(n);
        return step;
    };
    const auto journal = services.action_journal();
    for (auto it = journal.rbegin(); it != journal.rend(); ++it) {
        if (!matches(it->actor) || (!except.empty() && it->path.starts_with(except))) continue;
        if (!out.now) {
            out.actor = it->actor;
            out.now = step_of(*it);
            out.asking = it->exit_code == spec::exit_code::needs_answer;
            continue;
        }
        if (out.trail.size() >= trail) break;
        // One step a node: a slider dragged in ten writes is one step.
        const std::string_view last = out.trail.empty() ? std::string_view(out.now->path) : std::string_view(out.trail.back().path);
        if (it->path == last) continue;
        if (PresenceStep step = step_of(*it); !step.bounds.empty()) out.trail.push_back(std::move(step));
    }
    std::ranges::reverse(out.trail);
    out.taken_over = services.taken_over();
    // Its note: what the agent it shows said; with none acting yet, the newest any of these actors said.
    for (const AgentNote& note : services.agent_notes())
        if (out.actor.empty() ? matches(note.actor) : note.actor == out.actor) out.note = note;
    if (out.actor.empty() && out.note) out.actor = out.note->actor;
    for (const mlibs::agent::Claim& held : services.claims())
        if (out.actor.empty() ? matches(held.actor) : held.actor == out.actor) {
            if (out.actor.empty()) out.actor = held.actor;
            if (held.actor == out.actor) out.claims.push_back(held);
        }
    return out;
}

}  // namespace

AgentPresenceState agent_presence_state(const Services& services, std::string_view actors, std::size_t trail, std::string_view except) {
    return presence_of(services, actors, false, trail, except);
}

std::vector<AgentPresenceState> agent_presence_states(const Services& services, std::string_view actors, std::size_t trail, std::string_view except,
                                                      std::size_t limit) {
    // The actors in the order they came in — acting, saying, holding — and when each was last at work.
    std::vector<std::string> order;
    std::map<std::string, double> latest;
    const auto seen = [&](const std::string& actor, double at) {
        if (!actor.starts_with(actors)) return;
        if (std::ranges::find(order, actor) == order.end()) order.push_back(actor);
        latest[actor] = std::max(latest[actor], at);
    };
    for (const ActionEntry& e : services.action_journal())
        if (except.empty() || !e.path.starts_with(except)) seen(e.actor, e.at);
    for (const AgentNote& note : services.agent_notes()) seen(note.actor, note.at);
    for (const mlibs::agent::Claim& held : services.claims()) seen(held.actor, held.since);
    std::vector<AgentPresenceState> out;
    for (std::size_t index = 0; index < order.size(); ++index) {
        AgentPresenceState one = presence_of(services, order[index], true, trail, except);
        one.hue = index;
        out.push_back(std::move(one));
    }
    // At most `limit`: the latest at work, kept in the order they came in.
    while (out.size() > limit) {
        const auto oldest = std::ranges::min_element(out, {}, [&](const AgentPresenceState& one) { return latest[one.actor]; });
        out.erase(oldest);
    }
    return out;
}

Color agent_ink(const Theme& theme, std::size_t hue) {
    // Apart from cyan (the theme's agent) and from one another; mid-tones, read on light and dark alike.
    static constexpr std::array<std::uint32_t, 7> others{0x7c3aed, 0xd97706, 0xe11d48, 0x059669, 0x2563eb, 0xc026d3, 0xea580c};
    if (hue == 0) return theme.color("agent", Color::hex(0x0891b2));
    return Color::hex(others[(hue - 1) % others.size()]);
}

void AgentPresence::mounted() {
    tag_.set_style("caption");
    tag_.set_max_lines(1);
    card_.set_style("label");
    card_.set_max_lines(2);
    services().set(node(), "pointer_events", Value("none"));  // over the page, it takes no pointer
    services().follow_actions(node(), true);
}

void AgentPresence::prop_changed(AgentPresenceProp) {
    if (!attached()) return;
    services().invalidate_paint(node());
    services().invalidate_semantics(node());
}

Size AgentPresence::measure(MeasureContext&, std::optional<float> width, std::optional<float> height, mlibs::layout::Available aw,
                            mlibs::layout::Available ah) {
    // As large as it is let be: it lies over what it shows.
    return {width.value_or(aw.is_definite() ? aw.value : 0), height.value_or(ah.is_definite() ? ah.value : 0)};
}

AgentPresenceState AgentPresence::state() const {
    if (!attached()) return {};
    return agent_presence_state(services(), props.actors, static_cast<std::size_t>(std::max<std::int64_t>(0, props.trail)),
                                services().path(node()) + "/");
}

std::string AgentPresence::phrase(std::string_view key) const {
    if (const Value* own = props.phrases.get(key); own && !own->as_string().empty()) return std::string(own->as_string());
    if (key == "asks") return "{agent} asks you";
    if (key == "taken_over") return "You have taken over from {agent}";
    if (key == "intends") return "{agent}: {text}";
    if (key == "hands_back") return "{agent} hands back: {text}";
    return std::string(key);
}

std::string AgentPresence::said() const { return said_of(state()); }

std::vector<AgentPresenceState> AgentPresence::states() const {
    if (!attached()) return {};
    return agent_presence_states(services(), props.actors, static_cast<std::size_t>(std::max<std::int64_t>(0, props.trail)), services().path(node()) + "/");
}

std::string AgentPresence::said_of(const AgentPresenceState& now) const {
    if (!now.now && !now.note) return {};
    const std::string agent(who(now.actor));
    const auto filled = [&](std::string text, std::string_view say = {}) {
        if (const auto at = text.find("{agent}"); at != std::string::npos) text.replace(at, 7, agent);
        if (const auto at = text.find("{text}"); at != std::string::npos) text.replace(at, 6, say);
        return text;
    };
    if (now.taken_over) return filled(phrase("taken_over"));
    if (now.asking) {
        std::string text = phrase("asks");
        if (const auto at = text.find("{agent}"); at != std::string::npos) text.replace(at, 7, agent);
        return props.asking.empty() ? text : std::format("{}: {}", text, props.asking);
    }
    if (now.note) return filled(phrase(now.note->hands_back ? "hands_back" : "intends"), now.note->text);
    return std::format("{} {} {}", agent, now.now->action, short_path(now.now->path));
}

void AgentPresence::paint(PaintContext& ctx) const {
    if (props.skin == AgentPresenceSkin::none) return;
    // Each actor at work, in the order they came in; their tags and cards kept apart.
    std::vector<Rect> placed;
    for (const AgentPresenceState& now : states()) paint_one(ctx, now, placed);
}

void AgentPresence::paint_one(PaintContext& ctx, const AgentPresenceState& now, std::vector<Rect>& placed) const {
    const Theme& th = ctx.theme;
    const Color ink = agent_ink(th, now.hue);
    const Rect self = services().bounds(node());
    const auto local = [&](const Rect& r) { return Rect{r.x - self.x, r.y - self.y, r.width, r.height}; };
    const std::size_t others = placed.size();  // what the actors before it took
    // Moved down past what another's tag or card already took.
    const auto apart = [&](Rect place) {
        for (int tries = 0; tries < 8 && std::ranges::any_of(placed, [&](const Rect& other) { return overlap(place, other) > 0; }); ++tries)
            place.y += place.height + 4;
        placed.push_back(place);
        return place;
    };

    // What it holds (D-160): a dashed outline round each part, its name and what for.
    for (const mlibs::agent::Claim& held : now.claims) {
        const NodeId part = services().find(held.part);
        if (!part) continue;
        const Rect at = local(services().bounds(part));
        if (at.empty()) continue;
        const Rect box{at.x - 3, at.y - 3, at.width + 6, at.height + 6};
        ctx.part({.role = "claim", .allocation = box, .kind = xrender::PartKind::box}, [&] {
            ctx.list.fill_rect(box, xrender::Paint::solid(ink.with_alpha(0.06f)));
            constexpr float dash = 6, gap = 4, line = 1.5f;
            for (float x = box.x; x < box.right(); x += dash + gap) {
                const float length = std::min(dash, box.right() - x);
                ctx.list.fill_rect({x, box.y, length, line}, xrender::Paint::solid(ink));
                ctx.list.fill_rect({x, box.bottom() - line, length, line}, xrender::Paint::solid(ink));
            }
            for (float y = box.y; y < box.bottom(); y += dash + gap) {
                const float length = std::min(dash, box.bottom() - y);
                ctx.list.fill_rect({box.x, y, line, length}, xrender::Paint::solid(ink));
                ctx.list.fill_rect({box.right() - line, y, line, length}, xrender::Paint::solid(ink));
            }
            tag_.set_text(std::format("{} holds{}{}", who(now.actor), held.property.empty() ? std::string() : " " + held.property,
                                      held.intent.empty() ? std::string() : " · " + held.intent));
            const Size t = tag_.measure(ctx.services, ink);
            const Rect label = apart({box.x, box.bottom() + 2, t.width + 8, t.height + 2});
            ctx.list.fill_round_rect(label, th.radii(3), xrender::Paint::solid(th.color("surface").with_alpha(0.92f)));
            tag_.paint(ctx, {label.x + 4, label.y + 1}, ink);
        });
    }
    if (!now.now || now.now->bounds.empty()) return;

    // The steps before: a dot at each, joined by a dotted line to the next, fainter the older.
    std::vector<Point> stops;
    for (const PresenceStep& step : now.trail) stops.push_back(local(step.bounds).center());
    stops.push_back(local(now.now->bounds).center());
    ctx.part({.role = "trail", .kind = xrender::PartKind::content}, [&] {
        for (std::size_t i = 0; i + 1 < stops.size(); ++i) {
            const float age = static_cast<float>(i + 1) / static_cast<float>(stops.size());
            const Point a = stops[i], b = stops[i + 1];
            const float length = std::hypot(b.x - a.x, b.y - a.y);
            for (float d = 6; d < length - 6; d += 9) {
                const float t = d / length;
                ctx.list.fill_round_rect({a.x + (b.x - a.x) * t - 1.5f, a.y + (b.y - a.y) * t - 1.5f, 3, 3}, CornerRadii::all(1.5f),
                                         xrender::Paint::solid(ink.with_alpha(0.25f + 0.35f * age)));
            }
            ctx.list.fill_round_rect({a.x - 4, a.y - 4, 8, 8}, CornerRadii::all(4), xrender::Paint::solid(ink.with_alpha(0.3f + 0.4f * age)));
        }
    });

    // Where it acts: four corners round the node, a little outside it; its name and the action above.
    const Rect at = local(now.now->bounds);
    const float out = 5, arm = std::clamp(std::min(at.width, at.height) * 0.3f, 6.0f, 14.0f);
    const Rect box{at.x - out, at.y - out, at.width + 2 * out, at.height + 2 * out};
    ctx.part({.role = "frame", .allocation = box, .kind = xrender::PartKind::box}, [&] {
        parts::agent_corners(ctx.list, box, ink, arm);
        tag_.set_text(std::format("{} · {}", who(now.actor), now.now->action));
        const Size t = tag_.measure(ctx.services, th.color("on_accent", Color::white()));
        // Above the frame, or under it where the page's top leaves no room.
        const float above = box.y - t.height - 8;
        const Rect pill = apart({box.x, above >= 0 ? above : box.bottom() + 4, t.width + 12, t.height + 4});
        ctx.list.fill_round_rect(pill, th.radii(pill.height / 2), xrender::Paint::solid(ink));
        tag_.paint(ctx, {pill.x + 6, pill.y + 2}, th.color("on_accent", Color::white()));
    });

    // Waiting for the person, held by them, or saying what it is about to do: a card beside the node. Nothing on it answers (S-10).
    if (now.asking || now.taken_over || now.note) {
        card_.set_text(said_of(now));
        const Size t = card_.measure(ctx.services, th.color("text"), 260);
        // Beside the node, else under or over it — the first place on the page that hides nothing of it
        // (what it acts on least of all); where every place hides something, the one that hides least.
        const float width = t.width + 24, height = t.height + 16;
        const float across = std::clamp(box.x, 0.0f, std::max(0.0f, ctx.size.width - width));
        const std::array<Rect, 4> places{Rect{box.right() + 12, box.y + (box.height - height) / 2, width, height},
                                         Rect{box.x - 12 - width, box.y + (box.height - height) / 2, width, height},
                                         Rect{across, box.bottom() + 30, width, height}, Rect{across, box.y - 12 - height, width, height}};
        std::vector<Rect> leaves = page_leaves(services(), node(), now.now->bounds);
        for (Rect& leaf : leaves) leaf = local(leaf);
        leaves.insert(leaves.end(), placed.begin(), placed.begin() + static_cast<std::ptrdiff_t>(others));  // what another's marker took
        Rect card = places[2];
        float least = std::numeric_limits<float>::infinity();
        for (const Rect& place : places) {
            if (place.x < 0 || place.y < 0 || place.right() > ctx.size.width || place.bottom() > ctx.size.height) continue;
            float hidden = overlap(place, box) * 4;
            for (const Rect& leaf : leaves) hidden += overlap(place, leaf);
            if (hidden < least) least = hidden, card = place;
            if (hidden == 0) break;
        }
        placed.push_back(card);
        ctx.part({.role = "card", .allocation = card, .kind = xrender::PartKind::box}, [&] {
            if (auto it = th.shadows.find("md"); it != th.shadows.end()) paint_shadow(ctx.list, card, th.radii(10), it->second);
            ctx.list.fill_round_rect(card, th.radii(10), xrender::Paint::solid(th.color("surface")));
            ctx.list.fill_round_rect({card.x, card.y + 8, 3, card.height - 16}, CornerRadii::all(1.5f), xrender::Paint::solid(ink));
            card_.paint(ctx, {card.x + 14, card.y + 8}, th.color("text"));
        });
    }
}

void AgentPresence::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    const AgentPresenceState now = state();
    node.name = said();
    Value value;
    value.set("actor", now.actor).set("asking", now.asking).set("taken_over", now.taken_over);
    if (now.note) value.set("says", now.note->text).set("hands_back", now.note->hands_back);
    if (now.now) value.set("node", now.now->path).set("action", now.now->action);
    Value::List steps;
    for (const PresenceStep& step : now.trail) steps.emplace_back(step.path);
    value.set("trail", Value(std::move(steps)));
    // Every actor at work (D-160): where, what, what it said, what it holds.
    Value::List agents;
    for (const AgentPresenceState& one : states()) {
        Value::List held;
        for (const mlibs::agent::Claim& claim : one.claims) held.push_back(Value{}.set("node", claim.part).set("property", claim.property).set("intent", claim.intent));
        Value agent = Value{}.set("actor", one.actor).set("hue", static_cast<std::int64_t>(one.hue)).set("claims", Value(std::move(held)));
        if (one.now) agent.set("node", one.now->path).set("action", one.now->action);
        if (one.note) agent.set("says", one.note->text);
        agents.push_back(std::move(agent));
    }
    value.set("agents", Value(std::move(agents)));
    node.value = std::move(value);
}

}  // namespace omui
