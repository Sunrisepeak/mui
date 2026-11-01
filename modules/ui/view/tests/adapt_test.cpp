// Performance tiers in the runtime (D-94): the device's tier as its host says
// it; effects below their tiers stood in for (a backdrop's then drawn solid);
// morphs bounded by the tier; the frame-budget governor lowering motion, then
// effects, when frames keep missing the budget, and raising them with room —
// off unless a host turns it on, set aside when an adaptation is applied.
import std;
import openmui.view;
import openmui.testing;

using namespace omui::testing;
using namespace omui;

namespace {

typeset::FontCollection& fonts() {
    static typeset::FontCollection f = [] {
        typeset::FontCollection c;
        if (auto r = c.add_defaults(); !r) std::println("fonts: {}", r.error());
        return c;
    }();
    return f;
}

RuntimeOptions options() {
    RuntimeOptions o;
    o.fonts = &fonts();
    o.size = {200, 100};
    return o;
}

// Whether the frame's list draws a layer with the named effect.
bool draws(Runtime& rt, std::string_view effect) {
    const auto ref = render::effects().find(effect);
    for (const auto& c : rt.display_list().commands())
        if (const auto* l = std::get_if<render::PushLayer>(&c); l && ref && l->effect.id == ref->id) return true;
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    Suite s{"view.adapt"};

    s.test("an effect below its tiers is stood in for: neon from the Web up, none on embedded Linux; noise from T1", [] {
        Runtime rt(options());
        Element glow = box({.id = "glow", .width = 60, .height = 30, .background = "#334155"});
        glow.prop("pixel_effect", Source(Value{}.set("name", "neon")));
        Element grain = box({.id = "grain", .width = 60, .height = 30, .background = "#334155"});
        grain.prop("pixel_effect", Source(Value{}.set("name", "noise")));
        rt.mount("main", column({}, {std::move(glow), std::move(grain)}));
        rt.frame({seconds(0), {}});
        check(draws(rt, "neon") && draws(rt, "noise"), "on T3, both");
        rt.set_device_tier(pipeline::Tier::t1, "an embedded board");
        rt.frame({seconds(0.016), {}});
        check(!draws(rt, "neon"), "on T1, no neon");
        check(draws(rt, "noise"), "noise still");
        rt.set_device_tier(pipeline::Tier::t0, "an MCU");
        rt.frame({seconds(0.033), {}});
        check(!draws(rt, "noise"), "on T0, no effect at all");
        check_eq(rt.adaptation().reason, std::string("an MCU"));
    });

    s.test("the governor: off unless turned on; frames over their budget lower motion, then effects; room raises them again", [] {
        Runtime rt(options());
        for (int i = 0; i < 60; ++i) rt.frame_shown({seconds(i / 60.0), 40});
        check(rt.adaptation().motion == MotionLevel::full, "off: nothing changes");
        rt.set_governor(true);
        for (int i = 0; i < 30; ++i) rt.frame_shown({seconds(i / 60.0), 40});
        check(rt.adaptation().motion == MotionLevel::reduced, "two in three over 16.7 ms: motion reduced");
        check(rt.adaptation().reason.contains("motion reduced"), rt.adaptation().reason);
        for (int i = 0; i < 30; ++i) rt.frame_shown({seconds(i / 60.0), 40});
        check(rt.adaptation().motion == MotionLevel::off, "then off");
        for (int i = 0; i < 30; ++i) rt.frame_shown({seconds(i / 60.0), 40});
        check(rt.adaptation().effects == pipeline::Tier::t3, "then effects a tier lower");
        for (int i = 0; i < 300; ++i) rt.frame_shown({seconds(i / 60.0), 4});
        check(rt.adaptation().effects == pipeline::Tier::t4, "room: effects back");
        for (int i = 0; i < 300; ++i) rt.frame_shown({seconds(i / 60.0), 4});
        check(rt.adaptation().motion == MotionLevel::reduced, "then motion, a step at a time");
        // Applied (a replay): as given, the governor set aside.
        rt.set_adaptation({.tier = pipeline::Tier::t2, .motion = MotionLevel::full, .effects = pipeline::Tier::t2, .reason = "recorded"});
        for (int i = 0; i < 90; ++i) rt.frame_shown({seconds(i / 60.0), 40});
        check(rt.adaptation().motion == MotionLevel::full && rt.adaptation().reason == "recorded", "a replay's adaptation holds");
    });

    s.test("the governor's motion level is a ceiling over the person's own: reduced by either is reduced", [] {
        Runtime rt(options());
        rt.set_adaptation({.motion = MotionLevel::reduced});
        auto x = signal(0.0f);
        Element track = box({.id = "track", .width = 100, .height = 20}, {box({.id = "thumb", .width = 20, .height = 20, .background = "#e11d48"})});
        track.children[0].prop("translate_x", Source(derive([x] { return Value(static_cast<double>(x())); })));
        track.prop("motion", Source(Value("morph motion.spring.gentle")));
        rt.mount("main", column({}, {std::move(track)}));
        rt.frame({seconds(0), {}});
        rt.frame({seconds(0.016), {}});
        x.set(60);
        rt.frame({seconds(0.1), {}});
        check(!rt.motions().empty() && rt.motions()[0].end - rt.motions()[0].start < 0.13, "reduced: a short tween, not the gentle spring");
    });

    return s.run(argc, argv);
}
