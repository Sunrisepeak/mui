// A course of steps on a shape of its own (G8, D-253): on a line, a ring or any path the steps lie at equal lengths;
// pressed or dragged, the shape's nearest point takes the nearest step (a ring round past its last to its first);
// keys and an agent move it as a slider's value, each step taken back; its words are the step's caption; its playhead
// is carried to the step by the style's motion.
import std;
import omui.ui.base;
import omui.host.headless;
import mlibs.testing;
import mcpplibs.mtest;

using namespace omui;
using namespace mlibs::testing;
using namespace mcpplibs::mtest;

namespace {

mlibs::text::FontCollection& fonts() {
    static mlibs::text::FontCollection collection = [] {
        mlibs::text::FontCollection made;
        (void)made.add_defaults();
        return made;
    }();
    return collection;
}

std::int64_t act(Headless& h, std::string_view path, std::string_view action, Value args = {}) {
    Invocation call;
    call.actor = {"agent", "timeline-test", {}};
    call.args = std::move(args);
    const auto done = h.tree().act(path, action, call);
    h.settle();
    return done.exit_code().value_or(-1);
}

bool near(Point point, Point expected, float within = 2.5f) {
    return std::abs(point.x - expected.x) <= within && std::abs(point.y - expected.y) <= within;
}

std::uint64_t hash_of(const xrender::raster::Surface& pixels) {
    Hash hash;
    for (int row = 0; row < pixels.height(); ++row) hash.bytes(std::as_bytes(std::span(pixels.row(row), static_cast<std::size_t>(pixels.width()))));
    return hash.value();
}

}  // namespace

static UTest cases{"ui.base.timeline", [] {
    register_base_components();

    "on a line: a slider's semantics, its value a step; keys and an agent move it, each step taken back; its words the step's"_test = [] {
        Headless h(HeadlessOptions{.size = {320, 120}, .fonts = &fonts()});
        auto at = signal(1.0);
        h.mount("main", column({.padding = 20}, {timeline({.id = "course", .captions = std::vector<std::string>{"Start", "The plans", "Renewal", "Saved"},
                                                           .label = "The design", .steps = 4, .value = at})}));
        h.settle();
        const auto node = h.tree().node("main/course");
        expect(fatal(node.has_value()));
        expect(node->role == Role::slider && node->name == "The design" && node->description == "Start");
        expect(eq(node->value.get("min")->as_double(), 1.0) && eq(node->value.get("max")->as_double(), 4.0) && eq(node->value.get("shape")->as_string(), std::string("line")));
        expect(act(h, "main/course", "increment") == 0 && eq(at(), 2.0) && h.tree().node("main/course")->description == "The plans");
        expect(act(h, "main/course", "set_value", Value{}.set("value", 4.0)) == 0 && eq(at(), 4.0));
        expect(act(h, "main/course", "increment") == 0 && eq(at(), 4.0)) << "the last step is the last";
        expect(act(h, "main/course", "set_value", Value{}.set("value", 9.0)) == 0 && eq(at(), 4.0)) << "held to its steps";
        expect(act(h, "main/course", "set_value", Value{}.set("value", "sideways")) != 0) << "not a number: refused";
        expect(eq(h.runtime().undo({}).exit_code().value_or(-1), std::int64_t{0})) << "taken back";
        h.settle();
        expect(at() < 4.0) << "a step back";
        // Pressed at its end: the last step; at its start: the first.
        Runtime& rt = h.runtime();
        const Rect box = rt.screen_bounds(rt.find("main/course"));
        h.click(Point{box.right() - 4, box.center().y});
        h.settle();
        expect(eq(at(), 4.0)) << "pressed at its end";
        h.click(Point{box.x + 4, box.center().y});
        h.settle();
        expect(eq(at(), 1.0)) << "pressed at its start";
    };

    "on a ring: the steps round it from the top, clockwise; pressed anywhere near it, the nearest; dragged round past the last, the first again"_test = [] {
        Headless h(HeadlessOptions{.size = {200, 200}, .fonts = &fonts()});
        auto at = signal(1.0);
        h.mount("main", stack({.width = 200.0f, .height = 200.0f}, {timeline({.id = "ring", .shape = "ring", .steps = 8, .value = at, .width = 160.0f, .height = 160.0f})}));
        h.settle();
        Runtime& rt = h.runtime();
        const NodeId ring = rt.find("main/ring");
        const auto* course = rt.component<Timeline>(ring);
        expect(fatal(course != nullptr));
        const Size size{160, 160};
        const float inset = 10, radius = 80 - inset;  // the playhead's half and a little kept clear
        expect(near(course->point_at(1, size), {80, inset})) << "the first at the top";
        expect(near(course->point_at(3, size), {80 + radius, 80})) << "a quarter round: the right";
        expect(near(course->point_at(5, size), {80, 160 - inset})) << "half round: the bottom";
        expect(course->step_at({80 - radius - 6, 82}, size) == 7.0) << "near its left, outside it: the seventh";
        expect(course->step_at({78, inset + 1}, size) == 1.0) << "just short of the top again: the first";
        // A press at the bottom, then dragged round to the left.
        const Rect box = rt.screen_bounds(ring);
        const auto screen = [&](Point local) { return Point{box.x + local.x, box.y + local.y}; };
        h.click(screen({80, 160 - inset}));
        h.settle();
        expect(eq(at(), 5.0)) << "pressed at the bottom: the fifth";
        h.step({PointerEvent{.kind = PointerKind::down, .position = screen({80 + radius, 80}), .button = MouseButton::left}});
        expect(eq(at(), 3.0));
        h.step({PointerEvent{.kind = PointerKind::move, .position = screen({80, 160 - inset}), .button = MouseButton::left}});
        h.step({PointerEvent{.kind = PointerKind::move, .position = screen({80 - radius, 80}), .button = MouseButton::left}});
        h.step({PointerEvent{.kind = PointerKind::up, .position = screen({80 - radius, 80}), .button = MouseButton::left}});
        h.settle();
        expect(eq(at(), 7.0)) << "dragged round to the left: the seventh";
        expect(eq(rt.undo({}).exit_code().value_or(-1), std::int64_t{0})) << "the drag taken back as one";
        h.settle();
        expect(eq(at(), 5.0)) << "where the press found it";
        (void)h.save_png(scratch_dir("timeline") / "ring.png");
    };

    "any path in a 1×1 box: the steps at equal lengths along it; a shape that is not a path is a line"_test = [] {
        Headless h(HeadlessOptions{.size = {240, 240}, .fonts = &fonts()});
        h.mount("main", column({}, {timeline({.id = "slope", .shape = "M0 0 L1 1", .steps = 3, .width = 200.0f, .height = 200.0f}),
                                    timeline({.id = "wrong", .shape = "not a path", .steps = 3})}));
        h.settle();
        Runtime& rt = h.runtime();
        const auto* slope = rt.component<Timeline>(rt.find("main/slope"));
        expect(fatal(slope != nullptr));
        expect(near(slope->point_at(2, {200, 200}), {100, 100})) << "the middle of the diagonal";
        expect(near(slope->point_at(1, {200, 200}), {10, 10}) && near(slope->point_at(3, {200, 200}), {190, 190}));
        expect(eq(h.tree().node("main/slope")->value.get("shape")->as_string(), std::string("path")));
        const auto* wrong = rt.component<Timeline>(rt.find("main/wrong"));
        expect(fatal(wrong != nullptr));
        expect(near(wrong->point_at(1, {240, 28}), {10, 14}) && near(wrong->point_at(3, {240, 28}), {230, 14})) << "laid as a line";
    };

    "its playhead carried to the step by the style's motion (default.value): part-way between, at rest the drawing motion off gives"_test = [] {
        const auto scene = [](Headless& h, Signal<double> at) {
            h.mount("main", column({.padding = 20}, {timeline({.id = "course", .shape = "ring", .steps = 5, .value = at, .width = 120.0f, .height = 120.0f})}));
            h.settle();
        };
        Headless moving(HeadlessOptions{.size = {180, 180}, .fonts = &fonts(), .parallel_raster = false});
        Headless still(HeadlessOptions{.size = {180, 180}, .fonts = &fonts(), .parallel_raster = false});
        still.runtime().set_motion_level(MotionLevel::off);
        auto moving_at = signal(1.0), still_at = signal(1.0);
        scene(moving, moving_at), scene(still, still_at);
        const std::uint64_t first = hash_of(moving.pixels());
        moving_at.set(4.0), still_at.set(4.0);
        moving.step(), still.settle();
        const auto motion = [&]() -> std::optional<MotionState> {
            for (const MotionState& one : moving.runtime().motions())
                if (one.path == "main/course") return one;
            return std::nullopt;
        };
        expect(fatal(motion().has_value() && motion()->matched > 0)) << "the playhead and the played part matched";
        for (int frame = 0; frame < 120 && motion() && motion()->progress < 0.5; ++frame) moving.advance(seconds(1.0 / 60));
        const std::uint64_t between = hash_of(moving.pixels()), fourth = hash_of(still.pixels());
        expect(between != first && between != fourth) << "on its way round";
        (void)moving.save_png(scratch_dir("timeline") / "carried.png");
        moving.settle();
        expect(hash_of(moving.pixels()) == fourth && moving.runtime().motions().empty()) << "at rest, as drawn without motion";
    };
}};
