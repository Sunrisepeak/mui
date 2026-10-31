// The agent (0:36–0:46): an agent works a notes panel through its semantic
// tree, never its pixels. It looks (a query over the tree), stars a note —
// reversible, so the window's history can take it back, and the agent
// does — then asks to delete every note. That is irreversible: the runtime
// stops it with `needs-answer` (S-1, S-2) until a person answers, and the
// person does. On the right, the tree as the agent reads it, and every
// request and answer as the protocol's records; at the end, the journal of
// who did what (openas).
module openmui.showcase.promo_video;

import std;
import hanim;
import openmui;
import openmui.theme;

namespace omui::promo {

namespace {

struct Note {
    std::string id, text;
    bool        starred = false;
    bool operator==(const Note&) const = default;
};

// The notes panel: notes to star, a draft to add, and "Delete all" (an
// irreversible verb that states its answer).
struct Notes {
    Signal<std::vector<Note>> notes{std::vector<Note>{{"n1", "Render the film at 4K", false},
                                                      {"n2", "Check every subtitle's reading time", true},
                                                      {"n3", "Pin frame hashes on every platform", false}}};
    Signal<std::string> draft{std::string{}};
    Signal<bool>        starred_only{false};
};

Element notes_view(Notes& m) {
    return column({.id = "notes", .width = 470, .padding = 22, .gap = 12, .background = "surface", .border_color = "border", .border_width = 1, .radius = 12},
                  {text({.id = "title", .text = "Release notes", .style = "title"}),
                   row({.id = "compose", .gap = 8},
                       {text_box({.id = "draft", .label = "New note", .placeholder = "Write a note", .text = m.draft, .grow = 1}),
                        button({.id = "add", .text = "Add", .variant = ButtonVariant::primary})}),
                   toggle({.id = "starred-only", .checked = m.starred_only, .label = "Starred only"}),
                   column({.id = "list", .gap = 6},
                          {dynamic([&m] {
                              std::vector<Element> rows;
                              const auto& all = m.notes.get();
                              untracked([&] {
                                  for (const Note& n : all) {
                                      Element r = row({.id = n.id, .gap = 8},
                                                      {checkbox({.id = "star", .checked = n.starred, .label = n.text,
                                                                 .on_changed = [&m, id = n.id](bool v) {
                                                                     auto list = m.notes.peek();
                                                                     for (Note& x : list)
                                                                         if (x.id == id) x.starred = v;
                                                                     m.notes.set(std::move(list));
                                                                 }})});
                                      r.key = n.id;
                                      rows.push_back(std::move(r));
                                  }
                              });
                              return rows;
                          })}),
                   text({.id = "count", .text = derive([&m] { return std::format("{} notes", m.notes.get().size()); }), .style = "caption"}),
                   button({.id = "delete-all", .text = "Delete all", .variant = ButtonVariant::danger, .verb = "notes.delete_all"})});
}

// A line of the protocol: a request (→) or a record of its answer (←), from a time.
struct LogLine {
    double      at = 0;
    std::string text;
    std::uint32_t colour = ink::ice;
};

struct Row {
    std::string label, id;
    int         depth = 0;
    bool        lit = false;
};

struct State {
    double t = 0;
    float  app_scale = 1.25f;
    Point  app_at;            // the panel's top left on the screen
    Rect   tree_area, log_area;
    std::vector<Row>     rows;  // the semantic tree, as it is now
    std::vector<LogLine> log;
    std::string          focus;      // the node the agent is at
    std::vector<std::pair<double, Point>> path;  // where the agent's cursor is at each step
    double asked = -1, answered = -1;
    Labels labels;
};

class Agent final : public Shot {
public:
    explicit Agent(const ShotContext& c) : c_(c), s_(std::make_shared<State>()) {
        for (const auto& [t, v] : c.steps("step")) {
            const auto f = static_cast<std::int64_t>(std::llround(t * c.rate()));
            if (f > 0 && v > 0) steps_.push_back({f, static_cast<int>(std::lround(v))});
        }
        const Size screen = c.size;
        State& s = *s_;
        if (c.portrait()) {
            s.app_scale = 1.4f;
            s.app_at = {screen.width / 2 - 470 * s.app_scale / 2, 150};
            s.tree_area = {54, 880, screen.width / 2 - 70, 760};
            s.log_area = {screen.width / 2 + 16, 880, screen.width / 2 - 70, 760};
        } else {
            s.app_at = {130, 190};
            s.tree_area = {760, 150, 520, 760};
            s.log_area = {1310, 150, 514, 760};
        }
    }

    View view() override {
        auto st = s_;
        Element back = painter({.id = "backdrop",
                                .width = Dim(layout::pct(100)),
                                .height = Dim(layout::pct(100)),
                                .role = Role::none,
                                .paint = [st](PaintContext& p) { paint_back(p, *st); }});
        // The panel at its own size, scaled about its middle into place.
        Element app = box({.id = "app",
                           .width = 470,
                           .height = 470,
                           .translate_x = s_->app_at.x - 470 * (1 - s_->app_scale) / 2,
                           .translate_y = s_->app_at.y - 470 * (1 - s_->app_scale) / 2,
                           .scale = s_->app_scale},
                          {notes_view(notes_)});
        Element front = painter({.id = "front",
                                 .width = Dim(layout::pct(100)),
                                 .height = Dim(layout::pct(100)),
                                 .role = Role::figure,
                                 .label = "The semantic tree as the agent reads it, and the protocol's records",
                                 .paint = [st](PaintContext& p) { paint_front(p, *st); }});
        // The person's answer, asked for by the runtime (needs-answer): a card over the panel.
        Element card = column({.id = "card", .width = 470, .padding = 20, .gap = 10, .background = Fill(hex(0x1a1030, 0.96f)), .border_color = Fill(hex(ink::amber)),
                               .border_width = 1.5f, .radius = 12, .opacity = 0.0f, .translate_x = s_->app_at.x,
                               .translate_y = s_->app_at.y + 330 * s_->app_scale, .visible = false},
                              {text({.id = "ask", .text = "An agent asks to delete every note", .style = "heading", .color = Fill(hex(ink::ice))}),
                               text({.id = "why", .text = "notes.delete_all · effect=irreversible · it cannot be undone", .style = "code",
                                     .color = Fill(hex(ink::amber)), .size = 14.0f}),
                               row({.id = "answers", .gap = 10, .justify = "end"},
                                   {button({.id = "keep", .text = "Keep them", .variant = ButtonVariant::secondary}),
                                    button({.id = "delete", .text = "Delete", .variant = ButtonVariant::danger})})});
        // What the last step was, as large as the point it makes.
        Element status = text({.id = "status", .text = "", .style = "title", .color = Fill(hex(ink::ice)), .wrap = false, .size = 32.0f, .weight = 700.0f});
        Element type = stack({.id = "type", .width = Dim(layout::pct(100)), .height = Dim(layout::pct(100)), .align = "start"},
                             {std::move(card), box({.id = "head", .translate_x = s_->app_at.x + 20, .translate_y = s_->app_at.y - 74}, {std::move(status)})});
        return stack({.id = "agent", .width = Dim(layout::pct(100)), .height = Dim(layout::pct(100)), .align = "start"},
                     {std::move(back), std::move(app), std::move(front), std::move(type)});
    }

    void mounted(Runtime& rt) override {
        (void)theme::apply_style(rt, look_style, false);
        register_base_components();
        Notes& m = notes_;
        (void)rt.verbs().add({"notes.delete_all", "Delete every note", Effect::irreversible, "delete", {}}, [&m](const Value&) -> VerbResult {
            const auto n = static_cast<std::int64_t>(m.notes.peek().size());
            m.notes.set({});
            return Value{}.set("deleted", n);
        });
        rt.frame({hanim::seconds(0), {}});
        // Where the agent goes, step by step: the node it acts on, on the screen.
        auto centre = [&](std::string_view path) {
            const NodeId n = rt.find(path);
            const Rect b = n ? rt.drawn_bounds(n) : Rect{};
            return Point{b.x + b.width / 2, b.y + b.height / 2};
        };
        targets_ = {centre("main/app/notes/title"), centre("main/app/notes/list/n3/star"), centre("main/app/notes/list/n3/star"), centre("main/app/notes/delete-all"),
                    centre("main/app/notes/delete-all"), centre("main/app/notes/count")};
        refresh(rt);
    }

    std::vector<std::int64_t> moments() const override {
        std::vector<std::int64_t> out;
        for (const auto& [f, k] : steps_) out.push_back(f);
        return out;
    }

    void moment(Runtime& rt, std::int64_t frame) override {
        State& s = *s_;
        const double at = static_cast<double>(frame) / c_.rate();
        Invocation agent;
        agent.actor = {"agent", "promo", {}};
        auto records = [&](const Envelope& e, std::uint32_t colour) {
            for (const auto& r : e.records()) {
                std::string line = r.json();
                if (line.size() > 52) line = line.substr(0, 51) + "…";
                s.log.push_back({at + 0.18, "← " + line, colour});
            }
        };
        for (const auto& [f, step] : steps_) {
            if (f != frame) continue;
            switch (step) {
                case 1: {  // looks: every button, by the tree
                    s.log.push_back({at, R"(→ find {"role":"button"})", ink::cyan});
                    std::size_t n = 0;
                    rt.walk([&](NodeId id, int) { n += rt.semantic_node(id).role == Role::button; });
                    s.log.push_back({at + 0.18, std::format(R"(← {} nodes: add, delete-all, …)", n), ink::muted});
                    s.focus = "main/app/notes/title";
                    break;
                }
                case 2: {  // stars a note: reversible
                    s.log.push_back({at, R"(→ act main/…/n3/star toggle)", ink::cyan});
                    records(rt.act(rt.find("main/app/notes/list/n3/star"), "toggle", agent), ink::mint);
                    s.log.push_back({at + 0.3, "  effect reversible · the history can undo it", ink::muted});
                    s.focus = "main/app/notes/list/n3/star";
                    break;
                }
                case 3: {  // and takes it back
                    s.log.push_back({at, "→ undo (the window's history)", ink::cyan});
                    records(rt.undo(rt.root(), agent), ink::mint);
                    break;
                }
                case 4: {  // asks to delete them all: stopped until answered
                    s.log.push_back({at, "→ act main/delete-all activate", ink::cyan});
                    records(rt.act(rt.find("main/app/notes/delete-all"), "activate", agent), ink::amber);
                    s.asked = at;
                    s.focus = "main/app/notes/delete-all";
                    break;
                }
                case 5: {  // the person answers
                    Invocation person;
                    person.actor = {"person", "reviewer", {}};
                    person.answer = "delete";
                    s.log.push_back({at, "⇢ the person answers: delete", ink::ice});
                    records(rt.act(rt.find("main/app/notes/delete-all"), "activate", person), ink::mint);
                    s.answered = at;
                    break;
                }
                case 6: {  // the journal: who did what
                    s.log.push_back({at, "→ journal", ink::cyan});
                    for (const ActionEntry& e : rt.action_journal())
                        s.log.push_back({at + 0.15, std::format("  {} · {} · {} · exit {}", e.actor, e.action, e.path.substr(e.path.rfind('/') + 1), e.exit_code),
                                         e.exit_code == 0 ? ink::muted : ink::amber});
                    s.focus = "main/app/notes/count";
                    break;
                }
                default: break;
            }
        }
        refresh(rt);
    }

    void at(Runtime& rt, double t) override {
        State& s = *s_;
        s.t = t;
        // The agent's cursor: from step to step, there a quarter second before it acts.
        s.path.clear();
        for (std::size_t k = 0; k < steps_.size() && k < targets_.size(); ++k) s.path.push_back({static_cast<double>(steps_[k].first) / c_.rate(), targets_[k]});
        const NodeId card = rt.find("main/type/card");
        const bool up = s.asked >= 0 && (s.answered < 0 || t < s.answered + 0.35);
        rt.set(card, "visible", Value(up));
        rt.set(card, "opacity", Value(up ? smooth(t, s.asked, s.asked + 0.25) * (s.answered >= 0 ? 1 - smooth(t, s.answered + 0.1, s.answered + 0.35) : 1.0) : 0.0));
        rt.set(card, "translate_y", Value(static_cast<double>(s.app_at.y + 330 * s.app_scale) + (1 - smooth(t, s.asked, s.asked + 0.3)) * 24));
        // The headline: what the agent's last step means.
        {
            int last = 0;
            for (const auto& [f, k] : steps_)
                if (static_cast<double>(f) / c_.rate() <= t + 1e-9) last = k;
            constexpr std::array<std::pair<std::string_view, std::uint32_t>, 7> said{{{"", ink::ice},
                                                                                      {"An agent reads the tree", ink::ice},
                                                                                      {"Reversible: it can be undone", ink::mint},
                                                                                      {"…and it is", ink::mint},
                                                                                      {"Irreversible: stopped, needs an answer", ink::amber},
                                                                                      {"A person answers", ink::ice},
                                                                                      {"Every step in the journal", ink::cyan}}};
            const auto& [words, colour] = said[static_cast<std::size_t>(std::clamp(last, 0, 6))];
            const NodeId status = rt.find("main/type/head/status");
            rt.set(status, "text", Value(std::string(words)));
            rt.set(status, "color", Value(std::format("#{:06x}", colour)));
        }
        // The camera leans in, slowly, as the stakes rise.
        rt.set(rt.find("main"), "scale", Value(1 + 0.025 * smooth(t, 0.0, 9.5)));
        rt.invalidate_paint(rt.find("main/front"));
        rt.invalidate_paint(rt.find("main/backdrop"));
    }

    std::vector<Cue> cues(double seconds) override {
        std::vector<Cue> out;
        for (const auto& [f, step] : steps_) {
            const double at = static_cast<double>(f) / c_.rate();
            if (at >= seconds) continue;
            if (step == 4) out.push_back({at, "warn", 1, 0, 1});
            else if (step == 5) {
                out.push_back({at, "confirm", 1, 0, 1});
                out.push_back({at + 0.12, "crumple", 0.9f, 0, 1});
            } else out.push_back({at, "touch", 0.9f, static_cast<float>(step % 3 - 1) * 0.2f, 1});
        }
        return out;
    }

private:
    // The tree as the agent reads it now: the panel's nodes, indented.
    void refresh(Runtime& rt) {
        State& s = *s_;
        s.rows.clear();
        const NodeId root = rt.find("main/app/notes");
        std::function<void(NodeId, int)> walk = [&](NodeId n, int depth) {
            const spec::SemanticNode sn = rt.semantic_node(n);
            if (sn.role != Role::generic || depth == 0) {
                std::string label = std::format("{} {}", spec::name(sn.role), sn.id.substr(sn.id.rfind('/') + 1));
                if (!sn.name.empty() && sn.name.size() < 40) label += std::format("  \"{}\"", sn.name);
                if (sn.states.has(spec::State::checked)) label += "  ✓";
                s.rows.push_back({label, sn.id, depth, sn.id == s.focus});
                ++depth;
            }
            for (const NodeId c : rt.children(n)) walk(c, depth);
        };
        if (root) walk(root, 0);
    }

    static void paint_back(PaintContext& p, State& s) {
        paint_paper(p.list, p.size, {static_cast<float>(s.t * -5), 0}, 1);
        const Rect app{s.app_at.x, s.app_at.y, 470 * s.app_scale, 0};
        (void)app;
    }
    static void paint_front(PaintContext& p, State& s);

    ShotContext                               c_;
    std::shared_ptr<State>                    s_;
    Notes                                     notes_;
    std::vector<std::pair<std::int64_t, int>> steps_;
    std::vector<Point>                        targets_;
};

void Agent::paint_front(PaintContext& p, State& s) {
    auto& list = p.list;
    auto& fonts = p.services.fonts();
    // The tree, as an inspector shows it.
    const Rect ta = s.tree_area;
    list.fill_round_rect(ta, CornerRadii::all(10), render::Paint::solid(hex(ink::panel, 0.9f)));
    list.stroke_round_rect(ta, CornerRadii::all(10), 1, render::Paint::solid(hex(ink::line, 0.8f)));
    s.labels.get(fonts, "SEMANTIC TREE · what the agent reads", 13, hex(ink::cyan), 600, "JetBrains Mono").paint(list, {ta.x + 18, ta.y + 14});
    float y = ta.y + 46;
    for (const Row& r : s.rows) {
        if (y > ta.y + ta.height - 26) break;
        const float x = ta.x + 18 + static_cast<float>(r.depth) * 16;
        if (r.depth > 0) list.fill_rect({x - 10, y + 9, 6, 1}, hex(ink::line));
        if (r.lit) list.fill_round_rect({ta.x + 8, y - 3, ta.width - 16, 24}, CornerRadii::all(4), render::Paint::solid(hex(ink::cyan, 0.16f)));
        const auto& l = s.labels.get(fonts, r.label, 15.5f, hex(r.lit ? ink::ice : ink::muted), r.lit ? 600 : 450, "JetBrains Mono");
        list.push_clip({x, y - 4, ta.x + ta.width - 14 - x, 26});
        l.paint(list, {x, y});
        list.pop_clip();
        y += 28;
    }
    // The protocol's records, each written out as it comes.
    const Rect la = s.log_area;
    list.fill_round_rect(la, CornerRadii::all(10), render::Paint::solid(hex(0x040d1f, 0.92f)));
    list.stroke_round_rect(la, CornerRadii::all(10), 1, render::Paint::solid(hex(ink::line, 0.8f)));
    s.labels.get(fonts, "openmui.tree@0.1 · agent:promo", 13, hex(ink::cyan), 600, "JetBrains Mono").paint(list, {la.x + 18, la.y + 14});
    y = la.y + 46;
    std::size_t shown = 0;
    for (const LogLine& l : s.log) shown += l.at <= s.t;
    const std::size_t room = static_cast<std::size_t>((la.height - 60) / 25);
    std::size_t skip = shown > room ? shown - room : 0;
    for (const LogLine& l : s.log) {
        if (l.at > s.t) continue;
        if (skip > 0) {
            --skip;
            continue;
        }
        const double typed = ramp(s.t, l.at, l.at + 0.25);
        const std::size_t n = static_cast<std::size_t>(static_cast<double>(l.text.size()) * typed);
        std::size_t cut = n;
        while (cut < l.text.size() && (static_cast<unsigned char>(l.text[cut]) & 0xC0) == 0x80) ++cut;
        const auto& p2 = s.labels.get(fonts, l.text.substr(0, cut), 14.5f, hex(l.colour), 450, "JetBrains Mono");
        list.push_clip({la.x + 12, y - 4, la.width - 24, 26});
        p2.paint(list, {la.x + 18, y});
        list.pop_clip();
        y += 25;
    }
    // The agent's cursor: a reticle gliding to each node it acts on.
    if (!s.path.empty() && s.t >= s.path.front().first - 0.6) {
        Point at = s.path.front().second;
        for (std::size_t k = 1; k < s.path.size(); ++k) {
            const double from = s.path[k - 1].first, to = s.path[k].first - 0.25;
            if (s.t <= from) break;
            const double u = hanim::ease::in_out_cubic(ramp(s.t, from + 0.1, std::max(from + 0.2, to)));
            at = {static_cast<float>(mix(s.path[k - 1].second.x, s.path[k].second.x, u)), static_cast<float>(mix(s.path[k - 1].second.y, s.path[k].second.y, u))};
        }
        const float appear = static_cast<float>(smooth(s.t, s.path.front().first - 0.6, s.path.front().first - 0.2));
        const Color c = hex(ink::cyan, appear);
        render::Path ring;
        ring.add_ellipse({at.x - 15, at.y - 15, 30, 30});
        render::StrokeStyle st;
        st.width = 2;
        list.stroke_path(ring, st, render::Paint::solid(c));
        for (const auto& [dx, dy] : std::array<std::pair<float, float>, 4>{{{-1, 0}, {1, 0}, {0, -1}, {0, 1}}})
            list.fill_rect({at.x + dx * 21 - (dx != 0 ? 4 : 1), at.y + dy * 21 - (dy != 0 ? 4 : 1), dx != 0 ? 8.0f : 2.0f, dy != 0 ? 8.0f : 2.0f}, c);
        const auto& tag = s.labels.get(fonts, "agent:promo", 12, c, 600, "JetBrains Mono");
        tag.paint(list, {at.x + 22, at.y + 14});
    }
}

}  // namespace

std::unique_ptr<Shot> agent_shot(const ShotContext& c) { return std::make_unique<Agent>(c); }

}  // namespace omui::promo
