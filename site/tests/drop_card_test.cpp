// The home's soft landing (the site plan §5 ③): a card let go lands on its
// floor, squashes and springs back to rest; its time, bound to a slider, is
// written as it falls; dragged back, the card is drawn exactly as it was at
// that step (its state's hash the same); played on, the same steps to the
// same end, bit for bit. Less motion asked: let go, it is simply at rest.
// Sampled over time and saved (a strip of frames).
import std;
import omui;
import omui.site;
import mlibs.testing;
import mcpplibs.mtest;

using namespace mlibs::testing;
using namespace mcpplibs::mtest;
using namespace omui;

namespace {

struct Scene {
    Headless h{HeadlessOptions{.size = {480, 400}, .scale = 1}};
    Signal<double> at = signal(0.0);
    explicit Scene(bool reduced = false) {
        register_base_components();
        register_type(component_type<site::DropCard>());
        if (reduced) h.runtime().set_reduce_motion(true);
        h.mount("main", column({.id = "scene", .padding = 16, .gap = 12},
                               {site::drop_card({.id = "card", .at = at, .span = 3.0, .width = Dim(448.0f), .height = Dim(320.0f)}),
                                slider({.id = "time", .label = "Time", .max = 3.0, .step = 0.01, .value = at, .width = Dim(448.0f)})}));
        h.settle();
    }
    Value state() { return h.tree().node("main/card")->value; }
    std::string text(std::string_view key) {
        const Value now = state();
        return std::string(now.get(key)->as_string());
    }
    double number(std::string_view key) {
        const Value now = state();
        const Value* v = now.get(key);
        return v->type() == Value::Type::integer ? static_cast<double>(v->as_int()) : v->as_double();
    }
    Rect card() {
        const Value now = state();
        const auto& list = *now.get("card")->list();
        const auto value = [](const Value& v) { return static_cast<float>(v.type() == Value::Type::integer ? static_cast<double>(v.as_int()) : v.as_double()); };
        return {value(list[0]), value(list[1]), value(list[2]), value(list[3])};
    }
    std::int64_t act(std::string_view action, Value args = {}) {
        Invocation call;
        call.actor = {"agent", "visitor", {}};
        call.args = std::move(args);
        return h.tree().act("main/card", action, call).exit_code().value_or(-1);
    }
};

}  // namespace

UTest cases{"site.drop_card", [] {

    "let go, it lands, squashes and springs back to rest; its time written as it falls; the strip saved"_test = [] {
        Scene scene;
        expect(eq(scene.text("state"), std::string("waiting")));
        const Rect start = scene.card();
        expect(scene.act("drop") == 0);
        const auto dir = scratch_dir("drop-card");
        std::map<std::int64_t, std::string> hashes;
        float lowest_height = 1e9f, landed_bottom = 0;
        int frame = 0;
        while (scene.text("state") != "resting" && frame < 400) {
            scene.h.step();
            ++frame;
            hashes[static_cast<std::int64_t>(scene.number("step"))] = scene.text("hash");
            const Rect now = scene.card();
            landed_bottom = std::max(landed_bottom, now.bottom());
            if (now.bottom() > 320 - 30 - 2) lowest_height = std::min(lowest_height, now.height);
            if (frame % 12 == 0 && frame <= 96) (void)scene.h.save_png(dir / std::format("{:02}.png", frame / 12));
        }
        expect(eq(scene.text("state"), std::string("resting")));
        expect(std::abs(scene.number("at") - 3.0) < 0.02) << std::format("played its span: {}", scene.number("at"));
        expect(std::abs(scene.at.peek() - scene.number("at")) < 1e-9) << std::format("its time written: {} (slider bound to it)", scene.at.peek());
        const Rect rest = scene.card();
        expect(rest.bottom() < 320 - 28 + 1.5 && rest.bottom() > 320 - 28 - 3) << std::format("at rest on its floor: bottom {}", rest.bottom());
        expect(lowest_height < rest.height - 2) << std::format("squashed on landing: {} against {} at rest", lowest_height, rest.height);
        expect(rest.y > start.y + 60) << std::format("it fell: {} → {}", start.y, rest.y);
        (void)scene.h.save_png(dir / "rest.png");
        const std::string end = scene.text("hash");

        // The slider dragged back: drawn as it was at that step.
        scene.at.set(1.0);
        scene.h.step();
        expect(eq(scene.text("state"), std::string("held")));
        const auto back = static_cast<std::int64_t>(scene.number("step"));
        expect(back == 120) << std::format("at step {}", back);
        if (hashes.contains(back)) expect(eq(scene.text("hash"), hashes[back]));
        // An agent's seek, the same.
        expect(scene.act("seek", Value{}.set("at", 0.5)) == 0);
        scene.h.step();
        expect(scene.number("step") == 60 && (!hashes.contains(60) || scene.text("hash") == hashes[60])) << std::format("step {}", scene.number("step"));
        expect(std::abs(scene.at.peek() - 0.5) < 1e-9) << "the slider follows an agent's seek";
        expect(scene.act("seek", Value{}.set("at", 9.0)) != 0) << "not past what it played";
        // Its span's end: resting, however it came there (an undo of a seek lands there too).
        expect(scene.act("seek", Value{}.set("at", 3.0)) == 0);
        scene.h.step();
        expect(scene.text("state") == "resting" && scene.text("hash") == end) << std::format("{} {}", scene.text("state"), scene.text("hash"));
        expect(scene.act("seek", Value{}.set("at", 0.5)) == 0);
        scene.h.step();
        // Played on from there: the same steps, to the same end.
        expect(scene.act("play") == 0);
        for (int k = 0; k < 400 && scene.text("state") != "resting"; ++k) {
            scene.h.step();
            const auto step = static_cast<std::int64_t>(scene.number("step"));
            if (hashes.contains(step) && hashes[step] != scene.text("hash")) {
                expect(false) << std::format("replayed step {} differs", step);
                break;
            }
        }
        expect(scene.number("step") == 360 && scene.text("hash") == end) << std::format("ended on step {}: {}", scene.number("step"), scene.text("hash"));
        // At rest, no frames asked for.
        const FrameOutput out = scene.h.step();
        expect(!out.animating && out.demand.idle()) << std::format("at rest: {} at {} Hz", out.demand.reason, out.demand.max_hz);
    };

    "less motion asked: let go, it is simply at rest; its time can still be gone through"_test = [] {
        Scene scene(true);
        expect(scene.act("drop") == 0);
        scene.h.step();
        expect(eq(scene.text("state"), std::string("resting")));
        expect(std::abs(scene.number("at") - 3.0) < 0.02) << std::format("{}", scene.number("at"));
        scene.at.set(0.25);
        scene.h.step();
        expect(scene.text("state") == "held" && scene.number("step") == 30) << std::format("{} {}", scene.text("state"), scene.number("step"));
    };
}};
