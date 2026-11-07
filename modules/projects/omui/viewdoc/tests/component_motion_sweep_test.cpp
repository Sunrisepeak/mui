// Every one of the 37 component kinds with a typed default motion (M3): a
// state change that changes the drawing moves through intermediate frames
// that are neither endpoint, comes to rest on exactly the drawing motion off
// gives at once, and under reduced motion is a short real movement with the
// same rest. Built generically from the registered types (viewdoc::make), so
// no component gets a fixture of its own.
import std;
import omui.viewdoc;
import omui.ui.base;
import omui.host.headless;
import mlibs.testing;

using namespace mlibs::testing;
using namespace omui;
namespace vd = omui::viewdoc;

namespace {
mlibs::text::FontCollection& fonts() {
    static mlibs::text::FontCollection f = [] {
        mlibs::text::FontCollection c;
        (void)c.add_defaults();
        return c;
    }();
    return f;
}
HeadlessOptions options() {
    HeadlessOptions o;
    o.size = {420, 300};
    o.fonts = &fonts();
    o.parallel_raster = false;
    return o;
}
std::uint64_t pixels(Headless& h) {
    Hash hash;
    const auto& px = h.pixels();
    for (int y = 0; y < px.height(); ++y) hash.bytes(std::as_bytes(std::span(px.row(y), static_cast<std::size_t>(px.width()))));
    return hash.value();
}
struct Sample {
    Headless   h;
    Signal<bool> flag = signal(false);
    explicit Sample(std::string_view type, MotionLevel level) : h(options()) {
        h.runtime().set_motion_level(level);
        h.mount("main", column({.padding = 24}, {vd::make(type, {{"disabled", Prop<bool>(flag).source()}}, {}, "subject")}));
        h.settle();
    }
    void change() {
        flag.set(true);
        h.step();
    }
};
}  // namespace

int main(int argc, char** argv) {
    Suite s{"viewdoc.component-motion-sweep"};
    register_base_components();

    s.test("every typed default: a drawing change moves, then rests on motion off's drawing", [] {
        std::vector<std::string> moved, still, continuous;
        for (const auto& recipe : default_component_motion_recipes()) {
            const std::string type(recipe.component);
            Sample full(type, MotionLevel::full), reduced(type, MotionLevel::reduced), off(type, MotionLevel::off);
            {
                // A drawing that is itself a function of time (an indeterminate
                // bar) has no rest to compare: listed, not judged here.
                Sample probe(type, MotionLevel::full);
                const auto first = pixels(probe.h);
                probe.h.advance(seconds(0.25));
                if (pixels(probe.h) != first) {
                    continuous.push_back(type);
                    // With less motion it holds still.
                    for (const auto level : {MotionLevel::reduced, MotionLevel::off}) {
                        Sample quiet(type, level);
                        const auto held = pixels(quiet.h);
                        quiet.h.advance(seconds(0.25));
                        check(pixels(quiet.h) == held, type + ": still with less motion");
                    }
                    continue;
                }
            }
            const auto before = pixels(off.h);
            check(pixels(full.h) == before && pixels(reduced.h) == before, type + ": the same first drawing at every level");
            full.change(); reduced.change(); off.change();
            const auto after = pixels(off.h);
            if (after == before) {  // a kind whose drawing does not show the state
                still.push_back(type);
                continue;
            }
            moved.push_back(type);
            check(off.h.runtime().motions().empty(), type + ": off moves nothing");
            if (recipe.update == "none") {  // input content: a change of it is immediate by design
                check(pixels(full.h) == after && pixels(reduced.h) == after, type + ": immediate, as its recipe says");
                continue;
            }
            full.h.advance(seconds(0.03));
            reduced.h.advance(seconds(0.03));
            const auto middle = pixels(full.h), short_middle = pixels(reduced.h);
            check(middle != before && middle != after, type + ": the full motion's middle is neither endpoint");
            check(short_middle != before && short_middle != after, type + ": reduced motion still moves, briefly");
            full.h.settle();
            reduced.h.advance(seconds(1));
            // Compared at one time: a drawing may be a function of time (an
            // indeterminate progress bar), the same at every level.
            const auto when = std::max(full.h.runtime().now(), reduced.h.runtime().now());
            for (Sample* x : {&full, &reduced, &off}) x->h.advance(when - x->h.runtime().now() + seconds(0.1));  // a frame each, then
            const auto rest = pixels(off.h);
            check(pixels(full.h) == rest && pixels(reduced.h) == rest, type + ": at rest, exactly the immediate drawing");
            if (pixels(full.h) != rest) {
                static const auto dir = scratch_dir("component-motion-sweep");
                const auto saved = full.h.save_png(dir / (type + ".full.png"));
                (void)off.h.save_png(dir / (type + ".off.png"));
                const auto& a = full.h.pixels(); const auto& b = off.h.pixels();
                int differ = 0, x0 = -1, y0 = -1;
                for (int y = 0; y < a.height(); ++y) for (int x = 0; x < a.width(); ++x) {
                    const auto p = a.at(x, y), q = b.at(x, y);
                    if (p.r != q.r || p.g != q.g || p.b != q.b || p.a != q.a) { if (!differ) x0 = x, y0 = y; ++differ; }
                }
                std::println("    {}: {} pixels differ, first ({}, {}); saved {}; animating {}", type, differ, x0, y0, saved.has_value(),
                             full.h.runtime().motions().size());
            }
            check(full.h.runtime().motions().empty() && reduced.h.runtime().motions().empty(), type + ": nothing left moving");
        }
        std::println("    moved ({}): {}", moved.size(), moved);
        std::println("    no visible state ({}): {}", still.size(), still);
        std::println("    drawn as a function of time ({}): {}", continuous.size(), continuous);
        check_eq(moved.size() + still.size() + continuous.size(), std::size_t{37});
    });

    s.test("every moving kind, mid-motion: hit where it is drawn, interrupted without a jump, focus free", [] {
        std::size_t judged = 0;
        for (const auto& recipe : default_component_motion_recipes()) {
            const std::string type(recipe.component);
            if (recipe.update == "none") continue;
            Sample full(type, MotionLevel::full);
            const auto subject = full.h.runtime().find("main/subject");
            const auto before = pixels(full.h);
            full.change();
            full.h.advance(seconds(0.03));
            if (full.h.runtime().motions().empty() || pixels(full.h) == before) continue;  // still, or drawn as a function of time
            ++judged;
            auto& rt = full.h.runtime();
            // Hit: the middle of what it draws now is it (or a part of it).
            const Rect drawn = rt.drawn_bounds(subject);
            const NodeId hit = rt.hit_test(drawn.center());
            const auto inside = [&](NodeId node) {
                for (; rt.valid(node); node = rt.parent(node))
                    if (node == subject) return true;
                return false;
            };
            check(!drawn.empty() && inside(hit),
                  std::format("{}: mid-motion, the middle of its drawing hits it (hit {})", type, rt.valid(hit) ? rt.path(hit) : std::string("nothing")));
            // Interrupted: the change undone mid-way goes on from what is on screen. Its step
            // across the interruption is no larger than an uninterrupted run's same step.
            Sample steady(type, MotionLevel::full);
            steady.change();
            steady.h.advance(seconds(0.03));
            const auto largest_step = [](const auto& a, const auto& b) {
                int largest = 0;
                for (int y = 0; y < a.height(); ++y)
                    for (int x = 0; x < a.width(); ++x) {
                        const auto p = a.at(x, y), q = b.at(x, y);
                        largest = std::max({largest, std::abs(p.r - q.r), std::abs(p.g - q.g), std::abs(p.b - q.b), std::abs(p.a - q.a)});
                    }
                return largest;
            };
            const auto steady_before = steady.h.pixels();
            steady.h.advance(seconds(1.0 / 60));
            const int steady_step = largest_step(steady_before, steady.h.pixels());
            const auto shown = full.h.pixels();
            full.flag.set(false);
            full.h.advance(seconds(1.0 / 60));
            const int interrupted_step = largest_step(shown, full.h.pixels());
            check(interrupted_step <= steady_step + 24,
                  std::format("{}: interrupted, one frame moves it by {} (uninterrupted {}): no jump", type, interrupted_step, steady_step));
            // Focus goes where it is asked while it moves.
            if (rt.semantic_node(subject).states.has(spec::State::focusable)) {
                rt.focus(subject);
                full.h.step();
                check(rt.focused() == subject, type + ": focus reaches it mid-motion");
            }
            full.h.settle();
            check(rt.motions().empty(), type + ": and then it rests");
        }
        check(judged >= 10, std::format("{} kinds judged mid-motion", judged));
    });

    s.test("every text-input kind, focused while its container moves: the IME stays on what it draws", [] {
        std::vector<std::string> judged;
        for (const auto& recipe : default_component_motion_recipes()) {
            const std::string type(recipe.component);
            Headless h(options());
            auto shift = signal(0.0);
            auto subject = vd::make(type, {}, {}, "subject");
            Element holder = column({.id = "holder", .padding = 8}, {std::move(subject)});
            holder.prop("translate_x", Source(derive([shift] { return Value(shift()); })));
            holder.prop("motion", Source(Value("morph motion.normal")));
            h.mount("main", column({.padding = 24}, {std::move(holder)}));
            h.settle();
            auto& rt = h.runtime();
            const auto node = rt.find("main/holder/subject");
            rt.focus(node);
            h.step();
            if (!rt.text_input_rect()) continue;  // not a text input
            judged.push_back(type);
            shift.set(120);
            h.step();
            h.advance(seconds(0.05));
            check(!rt.motions().empty(), type + ": its container is moving");
            const auto ime = rt.text_input_rect();
            const Rect drawn = rt.drawn_bounds(node);
            const bool inside = ime && ime->x >= drawn.x - 1 && ime->x <= drawn.x + drawn.width + 1 && ime->y >= drawn.y - 1 &&
                ime->y <= drawn.y + drawn.height + 1;
            check(inside, std::format("{}: mid-motion the IME at {} is on its drawing {}", type,
                                      ime ? std::format("({}, {})", ime->x, ime->y) : std::string("nothing"),
                                      std::format("({}, {}, {}, {})", drawn.x, drawn.y, drawn.width, drawn.height)));
            h.settle();
            const auto rest = rt.text_input_rect();
            const Rect settled = rt.drawn_bounds(node);
            check(rest && rest->x >= settled.x - 1 && rest->x <= settled.x + settled.width + 1, type + ": and at rest");
        }
        check(judged.size() >= 2, std::format("text-input kinds judged: {}", judged.size()));
        std::println("    IME judged mid-motion: {}", judged);
    });

    return s.run(argc, argv);
}
