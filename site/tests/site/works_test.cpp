// The motion page's signature works (the site plan §7.1, E1): each a hanim
// recipe on a stage whose time is yours — made, its length said (the
// slider's end); played to its end, where it holds; any moment sought drawn
// the same each time, forwards or back. Sampled over time and saved.
import std;
import omui;
import omui.site;
import mlibs.testing;
import mcpplibs.mtest;
import mlibs.hash;
import hanim.engine;

using namespace omui;
using namespace mlibs::testing;
using namespace mcpplibs::mtest;

namespace {
constexpr std::string_view root = "main/body/view/explore/page/works/grid";
std::string path(std::string_view tail) { return std::string(root) + "/" + std::string(tail); }

struct Page {
    Headless h;
    // A narrow window: the works one after another (on a desktop they are the studio's, live_wall_test).
    explicit Page(float height = 3600) : h{HeadlessOptions{.size = {860, height}, .scale = 1}} {
        auto app = site::app("explore", "en");
        h.runtime().set_theme(app.theme);
        app.setup(h.runtime());
        h.mount("main", app.view());
        h.step();
        // What the page's walls play by themselves, paused (live_wall_test watches them play).
        for (const std::string_view pause : {"main/body/view/explore/page/recipes/tools/pause", "main/body/view/explore/page/wall/tools/pause"})
            (void)h.tree().act(pause, "activate", Invocation{});
        h.settle(seconds(2));
    }
    Value value(std::string_view tail) { return h.tree().node(path(tail))->value; }
    double number(std::string_view tail, std::string_view key) {
        const Value v = value(tail);
        const Value* n = v.get(key);
        return n->type() == Value::Type::integer ? static_cast<double>(n->as_int()) : n->as_double();
    }
    std::int64_t act(std::string_view tail, std::string_view action, Value args = {}) {
        Invocation call;
        call.args = std::move(args);
        return h.tree().act(path(tail), action, call).exit_code().value_or(-1);
    }
    std::uint64_t stage_pixels(std::string_view tail) {
        const Rect box = h.runtime().screen_bounds(h.runtime().find(path(tail)));
        const auto& px = h.pixels();
        mlibs::hash::Fnv1a hash;
        for (int y = static_cast<int>(box.y); y < static_cast<int>(box.bottom()); ++y)
            hash.bytes(std::as_bytes(std::span(px.row(y) + static_cast<int>(box.x), static_cast<std::size_t>(box.width))));
        return hash.value();
    }
};
}  // namespace

static UTest cases{"site.works", [] {

    for (const auto& [id, work] : {std::pair{"mark", "mark-writing"}, std::pair{"gather", "mui-gather"}}) {
        mcpplibs::mtest::test(std::string_view(std::format("{}: made with its length; played to its end, it holds; any moment the same each time; the strip saved", work))) = [id, work] {
            Page page;
            const std::string stage = std::format("{}/stage", id);
            expect(eq(std::string(page.value(stage).get("work")->as_string()), std::string(work)));
            const double length = page.number(stage, "length");
            expect(length > 0.5) << std::format("its length: {}", length);
            const Value slider = page.h.tree().node(path(std::format("{}/controls/time", id)))->value;
            expect(std::abs(slider.get("max")->as_double() - length) < 1e-6) << "the slider ends where it does";
            expect(page.act(std::format("{}/controls/play", id), "activate") == 0);
            const auto dir = scratch_dir(std::format("works-{}", id));
            for (int frame = 0; frame < 8; ++frame) {
                page.h.advance(seconds(length / 8));
                (void)page.h.save_png(dir / std::format("{:02}.png", frame), page.h.runtime().screen_bounds(page.h.runtime().find(path(stage))));
            }
            page.h.settle(seconds(1));
            expect(!page.value(stage).get("playing")->as_bool() && std::abs(page.number(stage, "at") - length) < 1e-6) << std::format("held at its end: {}", page.number(stage, "at"));
            // A moment sought twice, from either side: the same pixels.
            expect(page.act(stage, "seek", Value{}.set("at", length * 0.4)) == 0);
            page.h.settle();
            const auto once = page.stage_pixels(stage);
            expect(page.act(stage, "seek", Value{}.set("at", length * 0.9)) == 0);
            page.h.settle();
            const auto later = page.stage_pixels(stage);
            expect(page.act(stage, "seek", Value{}.set("at", length * 0.4)) == 0);
            page.h.settle();
            expect(page.stage_pixels(stage) == once && once != later) << "the same moment, the same pixels; another moment, others";
            expect(page.act(stage, "seek", Value{}.set("at", length * 2)) != 0) << "not past its end";
        };
    }

    "a soft landing, rewound: dropped, it rests; its slider follows; dragged back, held"_test = [] {
        Page page;
        expect(page.act("soft/controls/drop", "activate") == 0);
        page.h.advance(seconds(4));
        expect(eq(std::string(page.value("soft/stage").get("state")->as_string()), std::string("resting")));
        const Value slider = page.h.tree().node(path("soft/controls/time"))->value;
        expect(std::abs(slider.get("value")->as_double() - 3.0) < 0.02) << "the slider at its end";
        expect(page.act("soft/controls/time", "set_value", Value{}.set("value", 1.0)) == 0);
        page.h.step();
        page.h.step();
        expect(eq(std::string(page.value("soft/stage").get("state")->as_string()), std::string("held")));
    };

    "only what changed moves: the line in the other language, the shared letters kept"_test = [] {
        Page page;
        expect(page.act("letters/controls/switch", "activate") == 0);
        std::size_t kept = 0;
        for (int frame = 0; frame < 30; ++frame) {
            page.h.step();
            for (const auto& motion : page.h.runtime().motions())
                if (motion.path.ends_with("letters/stage/words")) kept = std::max(kept, motion.glyphs_kept);
        }
        expect(kept >= 4) << std::format("o, m, u, i kept: {}", kept);
        page.h.settle(seconds(2));
        expect(eq(page.h.tree().node(path("letters/stage/words"))->name, std::string("omui 绘制每一个字形")));
    };

    "a list becomes cards: each item travels to its place, through the places between"_test = [] {
        Page page;
        // Where it is drawn (a travel is drawn; the layout says where it ends at once).
        const auto x_of = [&](std::string_view item) { return page.h.runtime().drawn_bounds(page.h.runtime().find(path(std::format("layout/stage/items/{}", item)))).x; };
        const float list_x = x_of("item2");
        expect(page.act("layout/controls/as", "set_value", Value{}.set("value", "cards")) == 0);
        std::vector<float> path_x;
        const auto dir = scratch_dir("works-layout");
        for (int frame = 0; frame < 24; ++frame) {
            page.h.step();
            path_x.push_back(x_of("item2"));
            if (frame % 4 == 0) (void)page.h.save_png(dir / std::format("{:02}.png", frame / 4), page.h.runtime().screen_bounds(page.h.runtime().find(path("layout/stage"))));
        }
        page.h.settle(seconds(2));
        const float card_x = x_of("item2");
        expect(card_x > list_x + 100) << std::format("the third item moved right: {} → {}", list_x, card_x);
        const auto between = std::ranges::count_if(path_x, [&](float x) { return x > list_x + 5 && x < card_x - 5; });
        expect(between >= 3) << std::format("{} frames between its places (travelled, not cut)", between);
    };

    "a spring you can throw: flung, it leaves home smoothly, overshoots and settles; dragged and let go, it carries on the way it was thrown; caught mid-flight, it stays where it is drawn"_test = [] {
        Page page;
        const std::string stage = "spring/stage";
        const auto offset = [&] {
            const Value v = page.value(stage);
            const auto number = [](const Value* n) { return n->type() == Value::Type::integer ? static_cast<double>(n->as_int()) : n->as_double(); };
            return std::pair{number(v.get("x")), number(v.get("y"))};
        };
        // Flung: from home, no jump; out, back past home, and to rest.
        expect(page.act(stage, "fling", Value{}.set("vx", 1400.0).set("vy", 0.0)) == 0);
        std::vector<double> xs;
        const auto dir = scratch_dir("works-spring");
        for (int frame = 0; frame < 90; ++frame) {
            page.h.step();
            xs.push_back(offset().first);
            if (frame % 10 == 0) (void)page.h.save_png(dir / std::format("{:02}.png", frame / 10), page.h.runtime().screen_bounds(page.h.runtime().find(path(stage))));
        }
        expect(xs.front() > 0 && xs.front() < 40) << std::format("its first frame a step from home, not a jump: {}", xs.front());
        expect(std::ranges::max(xs) > 40 && std::ranges::min(xs) < -1) << std::format("out to {:.1f}, back past home to {:.1f}", std::ranges::max(xs), std::ranges::min(xs));
        page.h.settle(seconds(3));
        expect(std::abs(offset().first) < 0.5 && !page.value(stage).get("moving")->as_bool()) << "home, at rest";
        // Dragged right and let go while moving: it carries on to the right before it springs back.
        const Rect box = page.h.runtime().screen_bounds(page.h.runtime().find(path(stage)));
        const Point centre{box.x + box.width / 2, box.y + box.height / 2};
        page.h.step({PointerEvent{.kind = PointerKind::down, .position = centre, .button = MouseButton::left, .buttons = 1}});
        for (int k = 1; k <= 6; ++k) page.h.step({PointerEvent{.kind = PointerKind::move, .position = {centre.x + 15.0f * static_cast<float>(k), centre.y}, .buttons = 1}});
        const double let_go = offset().first;
        page.h.step({PointerEvent{.kind = PointerKind::up, .position = {centre.x + 90.0f, centre.y}, .button = MouseButton::left}});
        double furthest = let_go;
        for (int frame = 0; frame < 20; ++frame) {
            page.h.step();
            furthest = std::max(furthest, offset().first);
        }
        expect(furthest > let_go + 10) << std::format("it carried on the way it was thrown: let go at {:.1f}, went to {:.1f}", let_go, furthest);
        // Caught mid-flight: where it is drawn, it stays.
        expect(page.act(stage, "fling", Value{}.set("vx", 0.0).set("vy", -900.0)) == 0);
        for (int frame = 0; frame < 6; ++frame) page.h.step();
        const auto [x, y] = offset();
        const Point at{centre.x + static_cast<float>(x), centre.y + static_cast<float>(y)};
        page.h.step({PointerEvent{.kind = PointerKind::down, .position = at, .button = MouseButton::left, .buttons = 1}});
        const auto [held_x, held_y] = offset();
        // (Read a frame before the press: it may move that frame's way, no further — no jump to the pointer, no snap home.)
        expect(std::abs(held_x - x) < 6 && std::abs(held_y - y) < 6 && page.value(stage).get("held")->as_bool()) << std::format("caught at ({:.1f}, {:.1f}), held at ({:.1f}, {:.1f})", x, y, held_x, held_y);
        for (int frame = 0; frame < 10; ++frame) page.h.step();
        const auto [still_x, still_y] = offset();
        expect(std::abs(still_x - held_x) < 1e-6 && std::abs(still_y - held_y) < 1e-6) << "held, it does not move";
        page.h.step({PointerEvent{.kind = PointerKind::up, .position = at, .button = MouseButton::left}});
    };

    "an agent changes a motion: it reads the cards' plan, asks for 0.3 s later and a little bounce; the plan says so and it plays at once; undone, as it was"_test = [] {
        Page page;
        const auto plan = [&] { return std::string(page.value("agent/stage").get("plan")->as_string()); };
        const auto starts = [&](const std::string& text) {
            std::vector<double> out;
            for (std::size_t at = 0; at < text.size();) {
                const auto dash = text.find("–", at);
                if (dash == std::string::npos) break;
                const auto from = text.rfind(' ', dash) == std::string::npos || text.rfind(' ', dash) < at ? at : text.rfind(' ', dash) + 1;
                out.push_back(std::stod(text.substr(from, dash - from)));
                const auto next = text.find(" · ", dash);
                if (next == std::string::npos) break;
                at = next + 4;
            }
            return out;
        };
        // The distinct starts: each card's (its place and its colour begin together).
        const auto distinct = [&](const std::string& text) {
            auto all = starts(text);
            std::ranges::sort(all);
            const auto [first_same, last] = std::ranges::unique(all, [](double a, double b) { return std::abs(a - b) < 1e-9; });
            all.erase(first_same, last);
            return all;
        };
        const std::string before = plan();
        const auto first = distinct(before);
        expect(first.size() == 3 && std::abs(first[0]) < 1e-9 && std::abs(first[1] - 0.15) < 1e-9 && std::abs(first[2] - 0.30) < 1e-9) << std::format("three cards, 0.15 s apart: {}", before);
        expect(page.act("agent/controls/do/ask", "activate") == 0);
        page.h.step();
        page.h.step();
        const std::string after = plan();
        const auto later = distinct(after);
        expect(later.size() == 3 && std::abs(later[0] - 0.30) < 1e-9 && std::abs(later[2] - 0.60) < 1e-9) << std::format("each 0.3 s later: {}", after);
        expect(page.number("agent/stage", "bounce") == 0.4 && page.value("agent/stage").get("playing")->as_bool()) << "a little bounce, and playing again at once";
        expect(page.h.tree().node(path("agent/controls/plan"))->name.contains(after)) << "the plan shown is hanim's";
        const auto agents = std::ranges::count_if(page.h.runtime().action_journal(), [](const ActionEntry& e) { return e.actor.starts_with("agent") && e.action == "retime"; });
        expect(agents == 1) << "the change is the agent's, in the record";
        const auto dir = scratch_dir("works-agent");
        for (int frame = 0; frame < 8; ++frame) {
            page.h.advance(seconds(0.15));
            (void)page.h.save_png(dir / std::format("{:02}.png", frame), page.h.runtime().screen_bounds(page.h.runtime().find(path("agent/stage"))));
        }
        const auto undone = page.h.runtime().undo({});  // the window's history: the last step taken back
        expect(undone.exit_code() == std::optional<int>(0)) << "taken back";
        page.h.step();
        page.h.step();
        expect(plan() == before && page.number("agent/stage", "delay") == 0) << std::format("as it was: {}", plan());
    };

    "glass that flows: brought together the drops travel on a spring and merge, the middle between them glass; apart again they part"_test = [] {
        Page page;
        expect(page.act("glass", "scroll_into_view") == 0) << "the glass in view";
        page.h.settle(seconds(1));
        const auto left_x = [&] { return page.h.runtime().drawn_bounds(page.h.runtime().find(path("glass/stage/drops/glass/left"))).x; };
        const Rect stage = page.h.runtime().screen_bounds(page.h.runtime().find(path("glass/stage")));
        const auto middle = [&] {
            mlibs::hash::Fnv1a hash;
            const auto& px = page.h.pixels();
            const int y = static_cast<int>(stage.y + stage.height / 2), x = static_cast<int>(stage.x + stage.width / 2);
            for (int dy = -4; dy <= 4; ++dy) hash.bytes(std::as_bytes(std::span(px.row(y + dy) + x - 4, 9)));
            return hash.value();
        };
        const float apart = left_x();
        const auto gap_apart = middle();
        expect(page.act("glass/controls/drops", "set_value", Value{}.set("value", "together")) == 0);
        std::vector<float> xs;
        const auto dir = scratch_dir("works-glass");
        for (int frame = 0; frame < 30; ++frame) {
            page.h.step();
            xs.push_back(left_x());
            if (frame % 6 == 0) (void)page.h.save_png(dir / std::format("{:02}.png", frame / 6), stage);
        }
        page.h.settle(seconds(2));
        (void)page.h.save_png(dir / "together.png", stage);
        const float together = left_x();
        expect(together > apart + 20) << std::format("the left drop moved toward the right: {} → {}", apart, together);
        expect(std::ranges::count_if(xs, [&](float x) { return x > apart + 2 && x < together - 2; }) >= 3) << "travelled, on a spring";
        expect(middle() != gap_apart) << "the middle between them is glass now: merged";
        expect(page.act("glass/controls/drops", "set_value", Value{}.set("value", "apart")) == 0);
        page.h.settle(seconds(2));
        expect(std::abs(left_x() - apart) < 1 && middle() == gap_apart) << "apart again: parted, the middle as it was";
    };

    "the recipe library: every recipe of hanim's by name, each played on a card; a filter by kind; its call and its step to copy"_test = [] {
        Page page(7000);
        const std::string library = "main/body/view/explore/page/recipes";
        std::size_t thumbs = 0;
        for (const auto& info : hanim::anim::recipes()) thumbs += page.h.tree().node(std::format("{}/grid/{}/stage", library, info.name)).has_value();
        expect(thumbs == hanim::anim::recipes().size()) << std::format("{} of {}", thumbs, hanim::anim::recipes().size());
        (void)page.h.save_png(scratch_dir("works-library") / "library.png", page.h.runtime().screen_bounds(page.h.runtime().find(library)));
        for (const std::string_view name : {"rise_in", "tada", "bob"}) {
            const std::string stage = std::format("{}/grid/{}/stage", library, name);
            Invocation call;
            expect(page.h.tree().act(std::format("{}/grid/{}/head/play", library, name), "activate", call).exit_code() == std::optional<int>(0));
            page.h.advance(seconds(0.2));
            const Value moving = page.h.tree().node(stage)->value;
            expect(moving.get("playing")->as_bool() && moving.get("at")->as_double() > 0) << std::format("{} plays", name);
            page.h.settle(seconds(10));
            const Value done = page.h.tree().node(stage)->value;
            expect(!done.get("playing")->as_bool() && std::abs(done.get("at")->as_double() - done.get("length")->as_double()) < 1e-6) << std::format("{} played through", name);
        }
        Invocation call;
        expect(page.h.tree().act(library + "/grid/rise_in/copy/cpp", "activate", call).exit_code() == std::optional<int>(0));
        expect(eq(page.h.runtime().clipboard_text(), std::string("hanim::anim::rise_in(card, 24)")));
        expect(page.h.tree().act(library + "/grid/spin_in/copy/json", "activate", call).exit_code() == std::optional<int>(0));
        expect(eq(page.h.runtime().clipboard_text(), std::string(R"({"do": "spin_in", "object": "card", "degrees": -68.8})")));
        expect(page.h.tree().act(library + "/tools/kind", "set_value", Invocation{.args = Value{}.set("value", "loop")}).exit_code() == std::optional<int>(0));
        page.h.settle();
        std::size_t loops = 0;
        for (const auto& info : hanim::anim::recipes()) loops += page.h.tree().node(std::format("{}/grid/{}/stage", library, info.name)).has_value();
        expect(loops == static_cast<std::size_t>(std::ranges::count(hanim::anim::recipes(), hanim::anim::RecipeKind::loop, &hanim::anim::RecipeInfo::kind))) << std::format("{} loops", loops);
    };

    "the recipe library's editor: a recipe tuned on its sliders plays as tuned; its call and its step say so; reset, its own again"_test = [] {
        Page page(7000);
        const std::string library = "main/body/view/explore/page/recipes", editor = library + "/tune/editor";
        Invocation call;
        const auto act = [&](const std::string& id, std::string_view action = "activate", Value args = {}) {
            Invocation with;
            with.args = std::move(args);
            const bool done = page.h.tree().act(id, action, with).exit_code() == std::optional<int>(0);
            page.h.settle();
            return done;
        };
        const auto stage = [&] { return page.h.tree().node(editor + "/stage")->value; };
        // From its card into the editor: shake, its amplitude a pair.
        expect(act(library + "/grid/shake/copy/tune"));
        expect(eq(page.h.tree().node(editor + "/controls/name")->name, std::string("shake")));
        expect(eq(page.h.tree().node(editor + "/controls/call")->name, std::string("hanim::anim::shake(card, {8, 0})")));
        expect(act(editor + "/controls/amount_row/amount", "set_value", Value{}.set("value", 20.0)) && act(editor + "/controls/amount_y_row/amount_y", "set_value", Value{}.set("value", 6.0)));
        expect(act(editor + "/controls/duration_row/duration", "set_value", Value{}.set("value", 0.8)));
        expect(std::abs(stage().get("amount")->as_double() - 20) < 1e-9 && std::abs(stage().get("amount_y")->as_double() - 6) < 1e-9 && std::abs(stage().get("duration")->as_double() - 0.8) < 1e-9) << stage().json();
        expect(std::abs(stage().get("length")->as_double() - 0.8) < 1e-6) << std::format("as long as tuned: {}", stage().get("length")->as_double());
        const std::string tuned_call = "hanim::anim::shake(card, {20, 6}, {.duration = seconds(0.8)})";
        expect(eq(page.h.tree().node(editor + "/controls/call")->name, tuned_call));
        expect(act(editor + "/controls/actions/cpp"));
        expect(eq(page.h.runtime().clipboard_text(), tuned_call));
        expect(act(editor + "/controls/actions/json"));
        expect(eq(page.h.runtime().clipboard_text(), std::string(R"({"do": "shake", "object": "card", "amplitude": [20, 6], "duration": 0.8})")));
        // Played as tuned: the card goes as far as 20 px to the side (drawn, sampled over its time).
        expect(act(editor + "/controls/actions/play"));
        const auto dir = scratch_dir("works-tune");
        for (int frame = 0; frame < 6; ++frame) {
            page.h.advance(seconds(0.8 / 6));
            (void)page.h.save_png(dir / std::format("shake-{:02}.png", frame), page.h.runtime().screen_bounds(page.h.runtime().find(editor + "/stage")));
        }
        page.h.settle(seconds(2));
        expect(!stage().get("playing")->as_bool() && std::abs(stage().get("at")->as_double() - 0.8) < 1e-6) << "played through, held at its end";
        // An angle in degrees on its slider (radians in the recipe), a loop's period.
        expect(act(library + "/grid/sway/copy/tune"));
        expect(act(editor + "/controls/amount_row/amount", "set_value", Value{}.set("value", 10.0)) && act(editor + "/controls/period_row/period", "set_value", Value{}.set("value", 1.5)));
        expect(std::abs(stage().get("amount")->as_double() - 10 * std::numbers::pi / 180) < 1e-9 && std::abs(stage().get("period")->as_double() - 1.5) < 1e-9) << stage().json();
        expect(std::abs(stage().get("length")->as_double() - 3.0) < 1e-6) << "a loop played twice: twice its period";
        expect(act(editor + "/controls/actions/json"));
        expect(eq(page.h.runtime().clipboard_text(), std::string(R"({"do": "sway", "object": "card", "degrees": 10.0, "period": 1.5})")));
        expect(act(editor + "/controls/actions/reset"));
        expect(eq(page.h.tree().node(editor + "/controls/call")->name, std::string("hanim::anim::sway(card, 0.08, seconds(2.4))")));
        (void)call;
    };
}};
