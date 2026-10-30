// Time (0:46–0:50, and again 0:51–0:55): the haunted settings breakout (S4)
// mid-rally, its first room's bricks real components. A player bot plays
// it through the field's own actions (move_to, aim, launch, step), two of
// the simulation's 120 steps a frame. The film then rewinds itself (a
// `film` clip of its own frames, backwards), and this shot plays again
// from its start: the same frames, and in the corner the simulation's
// checksum, the same number at the same moment both times.
module openmui.showcase.promo_video;

import std;
import hanim;
import openmui;
import openmui.showcase.widget_breakout;
import openmui.showcase.widget_breakout.sim;
import openmui.timeline;

namespace omui::promo {

namespace {

namespace bo = omui::breakout;

constexpr Size field_size{1280, 720};
constexpr int  steps_per_frame = 2;     // 120 steps a second at 60 frames
constexpr int  preroll = 3 * 120;       // the rally it starts in

// Where the ball soonest down meets the bottom paddle's line, off the walls and bricks as they stand.
struct Landing {
    double x = 0, steps = 1e9;
    bool   any = false;
};
Landing landing(const bo::State& s) {
    const double line = s.paddles[0].y - 16;
    Landing best;
    s.world.each<bo::Ball>([&](mgame::Entity, const bo::Ball& b) {
        if (b.on != 0) return;
        std::array<mgame::V2, 16> pts{};
        const std::size_t n = bo::trace(s, b.at, b.velocity, b.radius, pts, 5000);
        double travelled = 0;
        const double speed = mgame::length(b.velocity);
        for (std::size_t k = 1; k < n; ++k) {
            const mgame::V2 a = pts[k - 1], z = pts[k];
            const double len = mgame::length(z - a);
            if (a.y <= line && z.y >= line && z.y > a.y) {
                const double u = (line - a.y) / (z.y - a.y);
                const double steps = (travelled + len * u) / std::max(speed, 1e-9);
                if (steps < best.steps) best = {a.x + (z.x - a.x) * u, steps, true};
                break;
            }
            travelled += len;
        }
    });
    return best;
}

// The player: under the ball where it comes down, met off-centre by `offset` (−1 … 1 of the paddle's half)
// to send it somewhere; a waiting ball launched along the aim.
struct Bot {
    double       offset = 0;
    std::int64_t until = -1;
    struct Move {
        double x = 0.5;
        bool   launch = false;
        std::optional<double> aim;
    };
    static double worth(const bo::State& s, const bo::State& from) {
        return -1e6 * (from.lives - s.lives) + 50.0 * (from.bricks - s.bricks) + 10.0 * (s.score - from.score) + 3.0 * (s.combo);
    }
    static Move catcher(const bo::State& s, double offset) {
        Move m;
        const Landing l = landing(s);
        const double x = l.any ? l.x - offset * s.paddles[0].half : s.paddles[0].x;
        m.x = std::clamp(x, 0.0, bo::field_w) / bo::field_w;
        bool waiting = false;
        s.world.each<bo::Ball>([&](mgame::Entity, const bo::Ball& b) { waiting = waiting || b.on == 1; });
        m.launch = waiting && s.frame % 20 == 0;
        return m;
    }
    static bo::Input input_of(const Move& m) {
        bo::Input in;
        in.player.pointer_x = static_cast<std::int16_t>(std::lround(m.x * 32767));
        if (m.aim) in.aim_set = 1, in.aim = *m.aim;
        if (m.launch) in.player.pressed = in.player.held = 1u << bo::launch_button;
        return in;
    }
    Move decide(const bo::State& s) {
        const Landing l = landing(s);
        if (l.any && l.steps < 36 && s.frame > until) {
            double best = -1e18;
            for (int k = 0; k < 5; ++k) {
                const double o = -0.7 + 0.35 * k;
                bo::State r = s;
                const bo::State from = s;
                for (int i = 0; i < 300 && !r.over; ++i) bo::step(r, input_of(catcher(r, i < l.steps + 4 ? o : 0)));
                const double w = worth(r, from);
                if (w > best) best = w, offset = o;
            }
            until = s.frame + static_cast<std::int64_t>(l.steps) + 4;
        }
        Move m = catcher(s, s.frame <= until ? offset : 0);
        if (m.launch) m.aim = 0.35 * static_cast<double>((s.frame / 120) % 5 - 2);
        return m;
    }
};

// The game on a runtime: the app's field, paused, played a step at a time through its actions.
struct Game {
    Runtime* rt = nullptr;
    NodeId   field{};
    Bot      bot;
    std::vector<std::pair<std::int64_t, std::string>> heard;  // the simulation's frame, a sound

    const bo::Field* component() const { return rt->component<bo::Field>(field); }
    const bo::State* state() const { return component() ? component()->state() : nullptr; }
    bool act(std::string_view action, Value args = {}) {
        Invocation c;
        c.actor = {"agent", "player", {}};
        c.args = std::move(args);
        return rt->act(field, action, c).exit_code() == std::optional<int>(0);
    }
    void step() {
        const bo::State* s = state();
        if (s) {
            const Bot::Move m = bot.decide(*s);
            act("move_to", Value{}.set("x", m.x));
            if (m.aim) act("aim", Value{}.set("across", *m.aim));
            if (m.launch) act("launch");
        }
        act("step");
        if (const bo::State* now = state())
            for (std::uint32_t i = 0; i < now->events; ++i) {
                const bo::Happened w = now->event[i].what;
                if (w == bo::Happened::hit || w == bo::Happened::broke || w == bo::Happened::core_hit) heard.push_back({now->frame, "brick"});
                else if (w == bo::Happened::paddle) heard.push_back({now->frame, "paddle"});
                else if (w == bo::Happened::flipped) heard.push_back({now->frame, "snap"});
            }
    }
};

struct State {
    double t = 0;
    Rect   field;
};

class Time final : public Shot {
public:
    explicit Time(const ShotContext& c) : c_(c), s_(std::make_shared<State>()) {
        const Size screen = c.size;
        const float side = c.portrait() ? 54 : 96;
        // Clear of the lines and the timeline along the bottom: the paddle is never under them.
        const float top = c.portrait() ? 260 : 112, bottom = c.portrait() ? 560 : 226;
        const float k = std::min((screen.width - 2 * side) / field_size.width, (screen.height - top - bottom) / field_size.height);
        const float w = field_size.width * k, h = field_size.height * k;
        s_->field = {std::round((screen.width - w) / 2), top, std::round(w), std::round(h)};
        scale_ = k;
    }

    View view() override {
        spec_ = bo::app();
        auto st = s_;
        const Rect r = s_->field;
        Element back = painter({.id = "backdrop",
                                .width = Dim(layout::pct(100)),
                                .height = Dim(layout::pct(100)),
                                .role = Role::none,
                                .paint = [st](PaintContext& p) {
                                    paint_paper(p.list, p.size, {0, static_cast<float>(st->t * 8)}, 1);
                                    p.list.box_shadow(st->field, CornerRadii::all(6), {0, 16}, 44, 0, hex(0x000000, 0.6f));
                                }});
        Element game = box({.id = "frame",
                            .width = Dim(field_size.width),
                            .height = Dim(field_size.height),
                            .translate_x = r.x - (field_size.width - r.width) / 2,
                            .translate_y = r.y - (field_size.height - r.height) / 2,
                            .scale = scale_,
                            .clip = true},
                           {spec_.view()});
        Element front = painter({.id = "front",
                                 .width = Dim(layout::pct(100)),
                                 .height = Dim(layout::pct(100)),
                                 .role = Role::none,
                                 .paint = [st](PaintContext& p) { paint_ticks(p.list, {st->field.x - 10, st->field.y - 10, st->field.width + 20, st->field.height + 20}, hex(ink::cyan, 0.6f), 18, 1.5f, 0); }});
        const float side = c_.portrait() ? 54 : 96;
        Element type = stack({.id = "type", .width = Dim(layout::pct(100)), .height = Dim(layout::pct(100)), .align = "start"},
                             {column({.id = "hud", .width = Dim(layout::pct(100)), .padding = Edges(Insets{r.y - 30, side, 0, side}), .align = "end"},
                                     {text({.id = "sim", .text = "", .style = "code", .color = Fill(hex(ink::ice)), .wrap = false, .size = 16.0f})}),
                              column({.id = "stamp", .width = Dim(layout::pct(100)), .padding = Edges(Insets{r.y + r.height * 0.36f, side, 0, side}),
                                      .gap = 6, .align = "center", .opacity = 0.0f},
                                     {text({.id = "match", .text = "", .style = "display", .color = Fill(hex(ink::mint)), .wrap = false, .size = 64.0f, .weight = 800.0f}),
                                      text({.id = "against", .text = "", .style = "code", .color = Fill(hex(ink::ice)), .wrap = false, .size = 18.0f})})});
        return stack({.id = "time", .width = Dim(layout::pct(100)), .height = Dim(layout::pct(100)), .align = "start"},
                     {std::move(back), std::move(game), std::move(front), std::move(type)});
    }

    void mounted(Runtime& rt) override {
        start(rt, game_);
        sim_ = rt.find("main/type/hud/sim");
        // Played again: the first play's checksums, from a run of its own, to set beside these.
        if (c_.value("compare", 0, 0) > 0.5) first_ = first_play(static_cast<std::int64_t>(std::llround(tl::to_seconds(c_.clip->length) * c_.rate())));
    }

    bool simulates() const override { return true; }

    void moment(Runtime&, std::int64_t) override {
        for (int i = 0; i < steps_per_frame; ++i) game_.step();
    }

    void at(Runtime& rt, double t) override {
        s_->t = t;
        if (const bo::State* s = game_.state()) {
            const std::uint64_t now = bo::checksum(*s);
            rt.set(sim_, "text", Value(std::format("room 1 · step {} · checksum {:016x}", s->frame, now)));
            if (!first_.empty()) {
                const auto f = static_cast<std::size_t>(std::clamp<std::int64_t>(std::llround(t * c_.rate()), 0, static_cast<std::int64_t>(first_.size()) - 1));
                const bool same = first_[f] == now;
                const double on = smooth(t, 0.8, 1.1) * (1 - smooth(t, 3.4, 3.8));
                rt.set(rt.find("main/type/stamp"), "opacity", Value(on));
                rt.set(rt.find("main/type/stamp/match"), "text", Value(std::string(same ? "MATCH ✓" : "DIFFERS ✗")));
                rt.set(rt.find("main/type/stamp/against"), "text", Value(std::format("first play {:016x} · now {:016x}", first_[f], now)));
            }
        }
        rt.invalidate_paint(rt.find("main/backdrop"));
    }

    // The first play's checksum at each of its frames: the game played again on a runtime of its own.
    std::vector<std::uint64_t> first_play(std::int64_t frames) {
        std::vector<std::uint64_t> out;
        Headless h(HeadlessOptions{.level = HeadlessLevel::layout, .size = c_.size, .fps = {static_cast<std::int32_t>(c_.rate()), 1}, .app = "openmui-promo-first",
                                   .complete_frames = true});
        Time replica(c_);
        h.mount("main", replica.view());
        Game g;
        replica.start(h.runtime(), g);
        for (std::int64_t f = 0; f <= frames; ++f) {
            if (f > 0)
                for (int i = 0; i < steps_per_frame; ++i) g.step();
            h.runtime().frame({hanim::seconds(static_cast<double>(f) / c_.rate()), {}});
            out.push_back(g.state() ? bo::checksum(*g.state()) : 0);
        }
        return out;
    }

    // The game's sounds: played on a runtime of its own, the same way, to the end of the clip.
    std::vector<Cue> cues(double seconds) override {
        std::vector<Cue> out;
        Headless h(HeadlessOptions{.level = HeadlessLevel::layout, .size = c_.size, .fps = {static_cast<std::int32_t>(c_.rate()), 1}, .app = "openmui-promo-sound",
                                   .complete_frames = true});
        Time replica(c_);
        h.mount("main", replica.view());
        Game g;
        replica.start(h.runtime(), g);
        const std::int64_t first = g.state() ? g.state()->frame : 0;
        g.heard.clear();
        const auto frames = static_cast<std::int64_t>(std::ceil(seconds * c_.rate()));
        for (std::int64_t f = 1; f <= frames; ++f) {
            for (int i = 0; i < steps_per_frame; ++i) g.step();
            h.runtime().frame({hanim::seconds(static_cast<double>(f) / c_.rate()), {}});
        }
        for (const auto& [frame, sound] : g.heard) {
            const double at = static_cast<double>(frame - first) / 120.0;
            if (at >= 0 && at < seconds) out.push_back({at, sound, sound == "paddle" ? 0.8f : 0.55f, 0, 1});
        }
        return out;
    }

private:
    // The game laid out, paused, and played into its rally, all before the shot's first frame.
    void start(Runtime& rt, Game& g) {
        rt.set_theme(spec_.theme);
        spec_.setup(rt);
        rt.walk([&](NodeId n, int) {
            if (!g.field && rt.path(n).ends_with("/field")) g.field = n;
        });
        g.rt = &rt;
        for (int f = 0; f <= 36; ++f) rt.frame({hanim::seconds(-1.2 + f / 60.0), {}});  // laid out, its style's cross-fade done
        g.act("pause");
        for (int i = 0; i < preroll; ++i) g.step();
        rt.frame({hanim::seconds(-0.5), {}});
    }

    ShotContext            c_;
    std::shared_ptr<State> s_;
    AppSpec                spec_;
    Game                   game_;
    std::vector<std::uint64_t> first_;  // a replay: the first play's checksums, frame by frame
    float                  scale_ = 1;
    NodeId                 sim_{};
};

}  // namespace

std::unique_ptr<Shot> time_shot(const ShotContext& c) { return std::make_unique<Time>(c); }

}  // namespace omui::promo
