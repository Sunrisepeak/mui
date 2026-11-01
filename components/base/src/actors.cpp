module openmui.ui.base;

import std;
import openmui.core;
import openmui.view;

namespace omui {

namespace {

constexpr float row_h = 28, pad_x = 8, time_w = 44, dot = 8;

// "{code}" and "{agent}" in a phrase, filled in.
std::string filled(std::string text, const AgentLink& l) {
    for (const auto& [key, with] : {std::pair<std::string_view, std::string_view>{"{code}", l.code}, {"{agent}", l.agent.empty() ? "Your agent" : l.agent}})
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

Size ActorLog::measure(MeasureContext&, std::optional<float> width, std::optional<float> height, layout::Available, layout::Available) {
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
        ctx.list.fill_round_rect({pad_x, y + (row_h - dot) / 2, dot, dot}, CornerRadii::all(dot / 2), render::Paint::solid(c));
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
    for (std::size_t i = 0; i < recs.size(); ++i) {
        const ActionEntry& e = *recs[i];
        const Rect r{0, y, w, row_h};
        rows_.push_back(r);
        shown_.push_back(e.id);
        if (hover_ == static_cast<int>(i)) ctx.list.fill_round_rect(r, CornerRadii::all(4), render::Paint::solid(th.color("surface_variant")));
        // When (m:ss of the runtime's clock), what, and its Undo while it is the last its history holds.
        parts::Label& when = times_[i];
        when.set_style("caption");
        const auto s = static_cast<std::int64_t>(e.at);
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
    if (!props.link || (state != "waiting" && state != "paired" && state != "lost"))
        std::erase_if(node.actions, [](const spec::ActionDesc& a) { return a.name == "unlink"; });
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

ActResult ActorLog::unlink(ActContext&) {
    services().unlink_agent();
    return ActResult::done();
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

}  // namespace omui
