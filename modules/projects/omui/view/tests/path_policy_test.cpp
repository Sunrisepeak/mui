// Public motion data reaches the same hanim operator through a real Runtime.
// Paint only describes endpoints. It contains no clock or interpolation code.
import std;
import omui.view;
import xrender.raster;
import mlibs.testing;

using namespace omui;
using namespace mlibs::testing;

namespace {
Value bend(double value) { return Value{}.set("bend", value); }
Value policy(Value path) { return Value{}.set("use", "morph").set("curve", "test.path").set("vertex_path", std::move(path)); }
RuntimeOptions options() {
    RuntimeOptions result; result.size = {320, 240};
    result.theme.motion["test.path"] = Transition::tween(seconds(1), hanim::ease::linear);
    return result;
}
struct Probe {
    Signal<bool> end = signal(false);
    Runtime runtime{options()};
    double now = 0;
    Probe(Value path, MotionLevel level = MotionLevel::full, bool system_reduced = false) {
        runtime.set_motion_level(level); runtime.set_reduce_motion(system_reduced);
        runtime.mount("main", column({}, {dynamic([this, recipe = policy(std::move(path))] {
            const bool circle = end.get();
            auto shape = painter({.id = "shape", .width = 320, .height = 240, .focusable = true,
                .paint = [circle](PaintContext& ctx) {
                    xrender::Path p;
                    if (circle) p.add_circle({220, 120}, 40);
                    else p.move_to({100, 80}).line_to({60, 160}).line_to({140, 160}).close();
                    ctx.list.fill_path(p, xrender::FillRule::nonzero, xrender::Paint::solid(Color::white()));
                }});
            shape.prop("motion", Source(recipe));
            return std::vector<Element>{std::move(shape)};
        })}));
        frame(0); frame(.016);
    }
    void frame(double t) { now = t; (void)runtime.frame({seconds(t), {}}); }
    void change(bool value) { end.set(value); frame(now + .1); }
    NodeId node() { return runtime.find("main/shape"); }
    void seek(double t) {
        check(runtime.hold_motion(node())); runtime.pause_motion(node()); runtime.seek_motion(node(), t); frame(now + .001);
    }
    std::uint64_t pixels() {
        xrender::raster::Surface surface(320, 240); xrender::CpuRenderer renderer;
        const auto result = renderer.render(runtime.display_list(), surface, {}, {});
        check_eq(result.invalid_paints, std::size_t{0});
        return surface.hash();
    }
};
}

int main(int argc, char** argv) {
    Suite suite{"view.path-policy"};
    suite.test("public data accepts bend and explicit controls and validates every field", [] {
        auto theme = Theme::light();
        const auto curved = parse_motion(Value{}.set("vertex_path", bend(.25)), theme);
        check(curved && curved->vertex_path == hanim::PointPath::bend(.25));
        if (!curved) return;
        const auto custom = parse_motion(Value{}.set("vertex_path", Value{}.set("control1", Value(Value::List{0., 1.}))
                                                                         .set("control2", Value(Value::List{0., -1.}))), theme);
        check(custom && custom->vertex_path == hanim::PointPath{{0, 1}, {0, -1}});
        theme.motion_library["curved"] = *curved;
        const auto same = parse_motion(Value("curved"), theme);
        check(same && *same == *curved, "theme values preserve process parameters and equality");
        for (const auto& reset : {Value{}, Value("linear")}) {
            const auto linear = parse_motion(Value{}.set("use", "curved").set("vertex_path", reset), theme);
            check(linear && linear->vertex_path.linear());
        }
        for (const auto& invalid : std::vector<Value>{Value(true), Value(.2), Value("bend"), Value(Value::Map{}),
            bend(std::numeric_limits<double>::quiet_NaN()), bend(std::numeric_limits<double>::infinity()), bend(1e7),
            bend(.2).set("unknown", 1), Value{}.set("bend", "0.2"),
            Value{}.set("control1", Value(Value::List{0., .2})),
            Value{}.set("control1", Value(Value::List{0., .2})).set("control2", Value(Value::List{1.})),
            Value{}.set("control1", Value(Value::List{0., "bad"})).set("control2", Value(Value::List{0., 1.})),
            Value{}.set("control1", Value(Value::List{0., 0.})).set("control2", Value(Value::List{0., 1e7}))})
            check(!parse_motion(Value{}.set("vertex_path", invalid), theme));
    });
    suite.test("runtime configuration changes middle pixels with exact endpoints and current-pose interruption", [] {
        Probe plain(bend(0)), curved(bend(.25));
        const auto start = curved.pixels(); check(start == plain.pixels());
        plain.change(true); curved.change(true);
        plain.seek(.37); curved.seek(.37);
        const auto middle = curved.pixels(); check(middle != start && middle != plain.pixels());
        const auto identity = curved.node();
        check(curved.runtime.hit_test({160, 120}) == identity, "painter hit policy remains its explicit border box");
        curved.runtime.focus(identity); curved.frame(curved.now + .001);
        check(curved.runtime.focused() == identity);
        curved.runtime.focus({}); curved.frame(curved.now + .001);
        curved.seek(.8); curved.seek(.37); check(curved.pixels() == middle);
        curved.change(false); check(curved.node() == identity && curved.pixels() == middle, "interruption starts on the actual process-modified shape");
        curved.seek(1); check(curved.pixels() == start);
        plain.seek(1); Probe endpoint(bend(.25)); endpoint.change(true); endpoint.seek(1);
        check(plain.pixels() == endpoint.pixels());
    });
    suite.test("reduced and off policies suppress process displacement and react while a morph is active", [] {
        Probe plain(bend(0), MotionLevel::reduced), curved(bend(.4), MotionLevel::reduced);
        plain.change(true); curved.change(true); plain.seek(.06); curved.seek(.06);
        check(plain.pixels() == curved.pixels(), "reduced policy uses a short linear spatial path");
        check(curved.runtime.motions()[0].end - curved.runtime.motions()[0].start < .13);
        for (bool system : {false, true}) {
            Probe disabled(bend(.4), system ? MotionLevel::full : MotionLevel::off, system);
            const auto before = disabled.pixels(); disabled.change(true);
            check(disabled.runtime.motions().empty() && before != disabled.pixels());
        }
        Probe target(bend(0), MotionLevel::off); target.change(true);
        for (bool system : {false, true}) {
            Probe active(bend(.4)); active.change(true); active.seek(.4);
            if (system) active.runtime.set_reduce_motion(true); else active.runtime.set_motion_level(MotionLevel::off);
            active.frame(active.now + .001);
            check(active.runtime.motions().empty());
            check(active.pixels() == target.pixels());
        }
    });
    return suite.run(argc, argv);
}
