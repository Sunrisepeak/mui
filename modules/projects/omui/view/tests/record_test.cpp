// The interaction recorder (D-148): each input event numbered, what came of
// it recorded at the level set; a press explained from press to release —
// it activated something, or what stopped it: a disabled control, a release
// outside, nothing there to press. What a person types is counted, not kept.
import std;
import omui.view;
import mcpplibs.mtest;

using namespace omui;
using namespace mcpplibs::mtest;

namespace {

struct Fixture {
    Runtime rt{RuntimeOptions{.size = {240, 240}, .stages = Stages::layout}};
    int     saves = 0;
    Fixture() {
        expect(rt.verbs()
                  .add({.name = "task.save", .effect = Effect::reversible},
                       [&](const Value&) -> VerbResult {
                           ++saves;
                           return Value{};
                       })
                  .has_value());
        rt.mount("main", column({}, {box({.id = "save", .width = 120, .height = 40, .label = "Save"}).prop("verb", Source{Value("task.save"), {}, {}}),
                                     box({.id = "off", .width = 120, .height = 40, .disabled = true, .label = "Off"}).prop("verb", Source{Value("task.save"), {}, {}}),
                                     box({.id = "plain", .width = 120, .height = 40, .label = "Plain"})}));
        (void)rt.frame({seconds(0), {}});
    }
    Point centre(std::string_view id) { return rt.screen_bounds(rt.find(id)).center(); }
    double now = 0;
    void click(Point down, Point up) {
        now += 0.1;
        (void)rt.frame({seconds(now), {PointerEvent{.position = down},
                                       PointerEvent{.kind = PointerKind::down, .position = down, .button = MouseButton::left, .buttons = 1},
                                       PointerEvent{.position = up},
                                       PointerEvent{.kind = PointerKind::up, .position = up, .button = MouseButton::left}}});
    }
};

}  // namespace

UTest cases{"view.record", [] {
    "a click that activated: the press, what it hit, the activation and the action, one chain"_test = [] {
        Fixture f;
        f.rt.set_record_level(RecordLevel::full);
        const Point at = f.centre("main/save");
        f.click(at, at);
        expect(eq(f.saves, 1));
        const auto why = f.rt.explain(at);
        expect(why.has_value());
        if (!why) return;
        expect(why->summary.starts_with("activated main/save")) << why->summary;
        expect(std::ranges::any_of(why->records, [](const InteractionRecord& r) { return r.kind == "hit" && r.node == "main/save"; }));
        expect(!why->actions.empty() && why->actions.front().path == "main/save" && why->actions.front().cause >= why->cause) << "the action carries its cause";
        expect(std::ranges::all_of(why->records, [](const InteractionRecord& r) { return r.actor == "person:user"; }));
    };

    "why nothing happened: a disabled control, a release outside, nothing to press"_test = [] {
        Fixture f;
        f.rt.set_record_level(RecordLevel::full);
        const Point off = f.centre("main/off"), save = f.centre("main/save"), plain = f.centre("main/plain");
        f.click(off, off);
        auto why = f.rt.explain(off);
        expect(why && why->summary == "main/off: disabled: a disabled control takes no input") << (why ? why->summary : "none");
        f.click(save, plain);
        why = f.rt.explain(save);
        expect(why && why->summary.starts_with("main/save: released outside it (over main/plain)")) << (why ? why->summary : "none");
        f.click(plain, plain);
        why = f.rt.explain(plain);
        expect(why && why->summary.contains("nothing here takes a press")) << (why ? why->summary : "none");
        expect(eq(f.saves, 0)) << "none of them ran the verb";
        // By its number, as timeline gives it.
        const auto again = f.rt.explain(std::nullopt, why->cause);
        expect(again && again->summary == why->summary);
    };

    "levels: errors keeps what did not happen and not the rest; off keeps nothing"_test = [] {
        Fixture f;
        expect(f.rt.record_level() == RecordLevel::errors) << "the default outside development mode";
        const Point off = f.centre("main/off"), save = f.centre("main/save");
        f.click(save, save);
        f.click(off, off);
        const auto kept = f.rt.interactions();
        expect(!kept.empty() && std::ranges::all_of(kept, [](const InteractionRecord& r) { return r.kind == "missed" || r.kind == "refused"; }))
            << std::format("{} records", kept.size());
        expect(std::ranges::any_of(kept, [](const InteractionRecord& r) { return r.node == "main/off"; }));
        f.rt.set_record_level(RecordLevel::off);
        const auto before = f.rt.interactions().size();
        f.click(off, off);
        expect(eq(f.rt.interactions().size(), before));
        // since: only what came after a cause.
        f.rt.set_record_level(RecordLevel::full);
        const std::uint64_t mark = f.rt.interactions().empty() ? 0 : f.rt.interactions().back().cause;
        f.click(save, save);
        const auto after = f.rt.interactions(mark);
        expect(!after.empty() && std::ranges::all_of(after, [&](const InteractionRecord& r) { return r.cause > mark; }));
    };

    "what a person types is counted, not kept"_test = [] {
        Fixture f;
        f.rt.set_record_level(RecordLevel::full);
        (void)f.rt.frame({seconds(1), {TextEvent{.text = "secret password"}}});
        const auto kept = f.rt.interactions();
        expect(!kept.empty() && kept.back().text == "text: 15 bytes") << (kept.empty() ? "none" : kept.back().text);
        expect(std::ranges::none_of(kept, [](const InteractionRecord& r) { return r.text.contains("secret"); }));
    };
}};
