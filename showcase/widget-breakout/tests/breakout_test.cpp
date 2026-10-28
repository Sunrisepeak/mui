// The breakout as a player and an agent meet it: level 1's bricks are real
// components laid out by openmui and read into the simulation where they
// stand; 120 steps a second on the runtime's clock (exactly, headless); an
// agent plays it through the field's actions, and every brick it hits
// changes for real (a toggle flips, a slider loses a third, a broken one
// goes from the screen); a rewind puts every brick back as the simulation
// had it; the semantic lens shows the hidden bricks; frames are pinned.
import std;
import openmui;
import openmui.showcase.widget_breakout;
import openmui.testing;

using namespace omui::testing;
using namespace omui;
namespace bo = omui::breakout;

namespace {

struct Game {
    Headless h{HeadlessOptions{.size = {1280, 720}}};
    std::vector<std::string> broke;
    explicit Game(std::int32_t level = 1) {
        AppSpec spec = bo::app();
        h.runtime().set_theme(spec.theme);
        spec.setup(h.runtime());
        h.mount("main", spec.view());
        h.runtime().on(field(), "brick_broke", [this](const Value& a) { broke.emplace_back(a.get("id")->as_string()); });
        h.step();
        if (level != 1) {
            h.runtime().set(field(), "level", Value(static_cast<std::int64_t>(level)));
            h.step();
        }
        h.advance(seconds(0.25));  // laid out, and the simulation begun from it
    }
    NodeId field() { return h.runtime().find("main"); }
    const bo::Field& component() { return *h.runtime().component<bo::Field>(field()); }
    const bo::State& state() { return *component().state(); }
    std::vector<NodeId> bricks() {
        std::vector<NodeId> out;
        for (NodeId row : h.runtime().children(field()))
            for (NodeId b : h.runtime().children(row)) out.push_back(b);
        return out;
    }
    Value seen() {
        for (const auto& n : h.tree().snapshot({}).nodes)
            if (n.id == "main") return n.value;
        return {};
    }
    // An agent's action on the field: whether it was done.
    bool act(std::string_view action, Value args = {}) {
        Invocation c;
        c.actor = {"agent", "test", {}};
        c.args = std::move(args);
        return h.tree().act("main", action, c).exit_code() == std::optional<int>(0);
    }
    // An agent that plays paused, a frame at a time: the paddle under the
    // lowest flying ball (a little off-centre now and then), launching at once.
    void play(std::int64_t frames) {
        for (std::int64_t k = 0; k < frames && !state().over; ++k) {
            const bo::State& s = state();
            double x = bo::field_w / 2, lowest = -1;
            s.world.each<bo::Ball>([&](mgame::Entity, const bo::Ball& b) {
                if (b.on == 0 && b.at.y > lowest) lowest = b.at.y, x = b.at.x;
            });
            x += 23.0 * static_cast<double>((s.frame / 37) % 3 - 1);
            act("move_to", Value{}.set("x", x / bo::field_w));
            if (s.frame % 30 == 0) act("launch");
            act("step");
        }
    }
    std::uint64_t pixels() {
        Hash hash;
        const auto& px = h.pixels();
        for (int y = 0; y < px.height(); ++y) hash.bytes(std::as_bytes(std::span(px.row(y), static_cast<std::size_t>(px.width()))));
        return std::stoull(hash.hex(), nullptr, 16);
    }
    // Each brick's component as the simulation has the brick: gone when
    // broken; a toggle flipped by each hit; a slider at what it has left.
    std::string disagreement() {
        const auto nodes = bricks();
        const auto& def = bo::level(component().props.level);
        std::vector<std::int32_t> left(nodes.size(), 0);
        state().world.each<bo::Brick>([&](mgame::Entity, const bo::Brick& b) { left[b.id] = b.durability; });
        for (std::size_t i = 0; i < nodes.size(); ++i) {
            const auto& r = h.runtime();
            if (r.get(nodes[i], "visible").as_bool(true) != (left[i] > 0)) return std::format("brick {}: visible", i);
            if (def.bricks[i].kind == bo::Kind::toggle && r.get(nodes[i], "checked").as_bool() != ((def.bricks[i].durability - left[i]) % 2 == 0))
                return std::format("toggle {}: checked", i);
            if (def.bricks[i].kind == bo::Kind::slider && r.get(nodes[i], "value").as_double() != static_cast<double>(left[i]) / def.bricks[i].durability)
                return std::format("slider {}: value {}", i, r.get(nodes[i], "value").as_double());
        }
        return {};
    }
};

}  // namespace

int main(int argc, char** argv) {
    Suite s{"showcase.widget_breakout"};

    s.test("level 1's bricks are its components, read into the simulation where layout put them", [] {
        Game g;
        const auto nodes = g.bricks();
        check_eq(nodes.size(), bo::level(1).bricks.size());
        check_eq(g.state().bricks, static_cast<std::int32_t>(nodes.size()));
        const Rect me = g.h.runtime().bounds(g.field());
        std::size_t matched = 0;
        g.state().world.each<bo::Body, bo::Brick>([&](mgame::Entity, const bo::Body& b, const bo::Brick& br) {
            const Rect r = g.h.runtime().bounds(nodes[br.id]);
            const double sx = bo::field_w / me.width, sy = bo::field_h / me.height;
            matched += b.box.x == (r.x - me.x) * sx && b.box.y == (r.y - me.y) * sy && b.box.w == r.width * sx && b.box.h == r.height * sy;
        });
        check_eq(matched, nodes.size());
        check(g.h.runtime().get(nodes[0], "text").as_string() == "Save", "a real button");
    });

    s.test("120 steps a second on the runtime's clock, exactly; the paddle follows the agent", [] {
        Game g;
        const auto start = g.state().frame;
        g.h.advance(seconds(1));
        check_eq(g.state().frame - start, std::int64_t{120});
        check(g.act("move_to", Value{}.set("x", 0.25)));
        g.h.advance(seconds(1));
        check(std::abs(g.state().paddles[0].x - 0.25 * bo::field_w) < 1, std::format("paddle at {}", g.state().paddles[0].x));
        check(g.act("release"));
    });

    s.test("an agent plays: every brick it hits changes for real, and the field says what it broke", [] {
        Game g;
        check(g.act("pause"));
        g.play(4000);
        check(g.state().bricks < static_cast<std::int32_t>(g.bricks().size()) - 5, std::format("{} bricks left", g.state().bricks));
        check_eq(g.disagreement(), std::string());
        check_eq(static_cast<std::int32_t>(g.broke.size()), static_cast<std::int32_t>(g.bricks().size()) - g.state().bricks);
        const Value v = g.seen();
        check_eq(v.get("score")->as_int(), g.state().score);
        check_eq(v.get("frame")->as_int(), g.state().frame);
        // The lens's feed: the tree's latest changes, what an agent reads too.
        const auto* changes = v.get("changes")->list();
        check(changes && !changes->empty() && changes->size() <= 8, "changes seen");
        check(changes->back().as_string().starts_with("brick."), std::string(changes->back().as_string()));
    });

    s.test("a rewind puts every brick back as the simulation had it; played on, a new branch", [] {
        Game g;
        check(g.act("pause"));
        std::vector<std::uint64_t> sums;
        const auto from = g.state().frame;
        for (int k = 0; k < 2400 && !g.state().over; ++k) {
            sums.push_back(bo::checksum(g.state()));
            g.play(1);
        }
        const auto now = g.state().frame;
        check(now - from > 700, "played");
        check(g.act("rewind", Value{}.set("frames", 600)));
        check(g.seen().get("changes")->list()->back().as_string() == std::format("rewound to frame {}", now - 600), "the rewind in the feed");
        check_eq(g.state().frame, now - 600);
        check_eq(bo::checksum(g.state()), sums[static_cast<std::size_t>(now - 600 - from)]);
        check_eq(g.disagreement(), std::string());
        check(!g.act("rewind", Value{}.set("frames", 40 * bo::steps_per_second)), "30 seconds at most");
        g.play(300);
        check_eq(g.disagreement(), std::string());
        check(g.act("restart"));
        check_eq(g.state().bricks, static_cast<std::int32_t>(g.bricks().size()));
        check_eq(g.disagreement(), std::string());
    });

    s.test("the semantic lens (Tab) shows the bricks the screen does not; the hidden ones are in the tree", [] {
        Game g;
        g.h.step();
        const auto plain = g.pixels();
        bool hidden_found = false;
        for (const auto& n : g.h.tree().snapshot({}).nodes)
            hidden_found = hidden_found || ((n.id == "brick.9" || n.id.ends_with("/brick.9")) && n.role == spec::Role::button);
        check(hidden_found, "a hidden brick is a button to the tree");
        check(g.h.runtime().get(g.bricks()[9], "opacity").as_double() == 0, "and invisible on screen");
        g.h.key(Key::tab);
        g.h.step();
        check(g.seen().get("lens")->as_bool(), "the lens is open");
        check(g.pixels() != plain, "it draws");
    });

    s.test("frames are pinned: the same pixels everywhere", [] {
        Game g;
        check(g.act("pause"));
        g.play(900);
        g.h.step();
        const auto played = g.pixels();
        const auto dir = scratch_dir("breakout-level1");  // to look at
        check(g.h.save_png(dir / "played.png").has_value());
        g.h.key(Key::tab);
        g.h.step();
        const auto lens = g.pixels();
        check(g.h.save_png(dir / "lens.png").has_value());
        std::println("    played {:016x}, lens {:016x}", played, lens);
        check_eq(played, std::uint64_t{0xd5452912dbfc78ca});  // GCC and LLVM
        check_eq(lens, std::uint64_t{0x0f57ad26ce85e13d});
    });

    s.test("level 2: each row cleared brings the next style pack; under every style the same play, the same hash, the same tree", [] {
        // Two games, one agent's play: one turns the style three times as it goes.
        Game a(2), b(2);
        check_eq(a.seen().get("level")->as_int(), std::int64_t{2});
        check_eq(a.bricks().size(), bo::level(2).bricks.size());
        check(a.act("pause") && b.act("pause"));
        check_eq(std::string(a.seen().get("style")->as_string()), std::string("glass"));
        std::int64_t differ = 0;
        for (int f = 0; f < 3000 && !a.state().over; ++f) {
            a.play(1);
            b.play(1);
            if (f == 300 || f == 900 || f == 1500) check(b.act("next_style"));
            differ += bo::checksum(a.state()) != bo::checksum(b.state());
        }
        check_eq(differ, std::int64_t{0});
        check_eq(a.disagreement(), std::string());
        check_eq(b.disagreement(), std::string());
        const std::string sa(a.seen().get("style")->as_string()), sb(b.seen().get("style")->as_string());
        check(sa != sb, std::format("{} and {}", sa, sb));
        check(std::string(a.seen().get("hash")->as_string()) == std::string(b.seen().get("hash")->as_string()), "the hash shown is the same");
        // The bricks' semantic nodes are the same under both styles.
        auto bricks_seen = [](Game& g) {
            std::vector<std::string> out;
            for (const auto& n : g.h.tree().snapshot({}).nodes)
                if (n.id.find("brick.") != std::string::npos) out.push_back(std::format("{} {} {} {}", n.id, static_cast<int>(n.role), n.states.bits, n.value.json()));
            return out;
        };
        check(bricks_seen(a) == bricks_seen(b), "the same tree");
        a.h.advance(seconds(1));  // the cross-fades done
        b.h.advance(seconds(1));
        const auto pa = a.pixels(), pb = b.pixels();
        check(pa != pb, "other pixels");
        const auto dir = scratch_dir("breakout-level2");  // to look at
        check(a.h.save_png(dir / std::format("{}.png", sa)).has_value() && b.h.save_png(dir / std::format("{}.png", sb)).has_value());
        std::println("    {} {:016x}, {} {:016x}", sa, pa, sb, pb);
        check_eq(pa, std::uint64_t{0xb7c31df5cb91e392});  // neon, GCC and LLVM
        check_eq(pb, std::uint64_t{0xcd3a98b98e12ad4d});  // liquid
        check(!a.act("next") || a.state().over, "next: once over");
    });

    s.test("a level over: the lesson, then Enter goes on (cleared: the next level; lost: this one again)", [] {
        Game g;
        check(!g.act("next"), "not before it is over");
        check(g.act("pause"));
        g.play(40000);
        const auto over = g.state().over;
        check(over != 0, "over");
        std::println("    over {} at frame {}, score {}", over, g.state().frame, g.state().score);
        g.h.key(Key::enter);
        g.h.step();
        g.h.advance(seconds(0.25));
        check_eq(g.component().props.level, over == 1 ? 2 : 1);
        check_eq(g.state().over, std::uint32_t{0});
        check_eq(g.state().bricks, static_cast<std::int32_t>(bo::level(g.component().props.level).bricks.size()));
    });

    return s.run(argc, argv);
}
