// Local overlay placement is a runtime capability, exercised here with the
// production dialog/drawer/popover/toast, actual pixels and pointer hits.
import std;
import openmui.ui.base;
import openmui.host.headless;
import mlibs.testing;

using namespace omui;
using namespace mlibs::testing;

namespace {
mlibs::text::FontCollection& fonts() {
    static mlibs::text::FontCollection f = [] { mlibs::text::FontCollection v; (void)v.add_defaults(); return v; }();
    return f;
}
HeadlessOptions options(Size size = {760, 460}) {
    HeadlessOptions result;
    result.size = size; result.fonts = &fonts(); result.parallel_raster = false;
    return result;
}
Rect bounds(Headless& h, std::string_view path) { return h.runtime().screen_bounds(h.runtime().find(path)); }
void near(float actual, float expected) { check_near(actual, expected, 0.1); }
Element boundary(std::string id, float width, float height, std::vector<Element> children, bool enabled = true) {
    auto e = column({.id = std::move(id), .width = width, .height = height, .shrink = 0, .background = "surface_variant"}, std::move(children));
    if (enabled) e.prop("overlay_boundary", Source(Value(true)));
    return e;
}
std::uint64_t outside(Headless& h, Rect region) {
    Hash hash;
    const auto& px = h.pixels();
    for (int y = 0; y < px.height(); ++y)
        for (int x = 0; x < px.width(); ++x)
            if (!region.contains(Point{static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f}))
                hash.bytes(std::as_bytes(std::span(&px.row(y)[x], std::size_t{1})));
    return hash.value();
}
void save(Headless& h, std::string_view name) {
    static const auto dir = scratch_dir("ui-overlay-boundary");
    check(h.save_png(dir / std::format("{}.png", name)).has_value());
}
void center(Rect inner, Rect outer) { near(inner.center().x, outer.center().x); near(inner.center().y, outer.center().y); }
}

int main(int argc, char** argv) {
    Suite suite{"ui.base.overlay-boundary"};
    register_base_components();

    suite.test("a dialog uses the nearest marked ancestor; an unmarked dialog keeps window placement", [] {
        for (bool bounded : {false, true}) {
            Headless h(options());
            h.runtime().set_motion_level(MotionLevel::off);
            h.mount("main", column({.padding = 32}, {
                boundary("outer", 600, 340, {
                    box({.height = 36}),
                    boundary("inner", 340, 220, {dialog({.id = "dialog", .modal = false, .open = true,
                        .title = "Review", .width = 180, .height = 120, .min_width = 160},
                        {button({.id = "accept", .text = "Accept"})})}, bounded)
                }, bounded)
            }));
            h.settle();
            const Rect dialog = bounds(h, "main/outer/inner/dialog");
            center(dialog, bounded ? bounds(h, "main/outer/inner") : Rect{0, 0, 760, 460});
            check(h.runtime().hit_test(dialog.center()) != h.runtime().find("main"), "the placed control receives real hits");
            save(h, bounded ? "dialog-local" : "dialog-window-compatible");
        }
    });

    suite.test("a side drawer fills its boundary height and follows local resize", [] {
        Headless h(options());
        h.runtime().set_motion_level(MotionLevel::off);
        auto width = signal(360.0), height = signal(220.0);
        auto frame = column({.id = "stage", .width = width, .height = height, .background = "surface_variant"}, {
            drawer({.id = "drawer", .modal = false, .open = true, .size = 160.0, .title = "Filters"},
                   {checkbox({.id = "mine", .label = "Only mine"})})});
        frame.prop("overlay_boundary", Source(Value(true)));
        h.mount("main", column({.padding = 40}, {std::move(frame)}));
        h.settle();
        auto verify = [&] {
            const Rect stage = bounds(h, "main/stage"), drawer = bounds(h, "main/stage/drawer");
            near(drawer.right(), stage.right()); near(drawer.y, stage.y); near(drawer.height, stage.height);
            check(stage.contains(drawer), "drawer fits the local stage");
        };
        verify(); save(h, "drawer-local");
        width.set(280); height.set(160); h.settle(); verify();
        h.runtime().resize({620, 380}, 1); h.settle(); verify();
        h.runtime().set(h.runtime().find("main/stage"), "overlay_boundary", Value(false));
        h.settle();
        const Rect unbounded = bounds(h, "main/stage/drawer");
        near(unbounded.right(), 620); near(unbounded.y, 0); near(unbounded.height, 380);
        h.runtime().set(h.runtime().find("main/stage"), "overlay_boundary", Value(true));
        h.settle(); verify();
    });

    suite.test("anchor popovers flip against their boundary rather than spare window space", [] {
        for (bool bounded : {false, true}) {
            Headless h(options());
            h.runtime().set_motion_level(MotionLevel::off);
            h.mount("main", column({.padding = 32}, {
                boundary("stage", 360, 200, {
                    box({.height = 140, .shrink = 0}),
                    button({.id = "anchor", .text = "Open actions"}),
                    popover({.id = "popup", .open = true}, {button({.id = "copy", .text = "Copy link"})})
                }, bounded)
            }));
            h.settle();
            const Rect anchor = bounds(h, "main/stage/anchor"), popup = bounds(h, "main/stage/popup");
            if (bounded) {
                check(popup.bottom() <= anchor.y, "not enough room below inside the stage: flipped above");
                check(bounds(h, "main/stage").contains(popup));
            } else check(popup.y >= anchor.bottom(), "the window still has room below: original behavior");
            save(h, bounded ? "popup-flips-locally" : "popup-window-compatible");
        }
    });

    suite.test("toast stacking is independent for different boundaries", [] {
        Headless h(options({760, 380}));
        h.runtime().set_motion_level(MotionLevel::off);
        auto notice = [](std::string id) { return toast({.id = std::move(id), .duration = 0.0, .message = "Saved", .open = true, .width = 200}); };
        h.mount("main", row({.padding = 32, .gap = 40}, {
            boundary("left", 300, 260, {notice("one"), notice("two")}),
            boundary("right", 300, 260, {notice("one")})
        }));
        h.settle();
        const Rect left = bounds(h, "main/left"), right = bounds(h, "main/right");
        const Rect first = bounds(h, "main/left/one"), second = bounds(h, "main/left/two"), other = bounds(h, "main/right/one");
        near(left.bottom() - first.bottom(), right.bottom() - other.bottom());
        near(left.right() - first.right(), right.right() - other.right());
        check(second.bottom() < first.y, "the second toast stacks above the first on its own boundary");
        check(left.contains(first) && left.contains(second) && right.contains(other));
        save(h, "independent-toast-stacks");
    });

    suite.test("a projected boundary moves overlay clipping and actual input together", [] {
        Headless h(options());h.runtime().set_motion_level(MotionLevel::off);
        int accepted=0;
        auto stage=boundary("stage",300,220,{
            button({.id="anchor",.text="Open actions"}),
            popover({.id="popup",.open=true},{button({.id="accept",.text="Accept",.on_activated=[&]{++accepted;}})})});
        stage.prop("clip",Source(Value(true)));stage.prop("radius",Source(Value(16)));
        h.mount("main",column({.padding=32},{std::move(stage)}));h.settle();
        const auto root=h.runtime().find("main/stage"),control=h.runtime().find("main/stage/popup/accept");
        const auto before=h.runtime().drawn_bounds(control);
        auto lease=h.runtime().acquire_presentation(std::array{PresentationTarget{root,PresentationChannel::translation}});
        check(lease.has_value());if(!lease)return;
        check(lease->publish(std::array{hanim::Transform2D{.tx=340,.ty=90}}).has_value());h.step();
        const auto current=h.runtime().drawn_bounds(control);
        near(current.x,before.x+340);near(current.y,before.y+90);
        check(h.runtime().hit_test(current.center())==control,"moved clip accepts the control beyond the old boundary");
        h.click(current.center());h.step();check(accepted==1);save(h,"projected-boundary");
        lease->release();h.step();near(h.runtime().drawn_bounds(control).x,before.x);
    });

    suite.test("scroll and ancestor clips constrain overlay placement paint and nonmodal hits", [] {
        Headless h(options());
        h.runtime().set_motion_level(MotionLevel::off);
        auto scroll_y = signal(0.0);
        auto open = signal(false);
        h.mount("main", column({.padding = 32}, {
            scroll({.id = "scroll", .y = scroll_y, .width = 300, .height = 200}, {
                box({.height = 80, .shrink = 0}),
                boundary("stage", 380, 180, {
                    dialog({.id = "dialog", .modal = false, .open = open, .title = "Details", .width = 240, .height = 100, .min_width = 100},
                           {button({.id = "wide", .text = "A deliberately wide control", .width = 480})})
                }),
                box({.height = 320, .shrink = 0})
            }),
            button({.id = "outside", .text = "Outside the preview"})
        }));
        h.settle();
        for (const double offset : {0.0, 90.0, 0.0}) {
            scroll_y.set(offset); open.set(false); h.settle();
            const Rect viewport = bounds(h, "main/scroll"), stage = bounds(h, "main/scroll/stage");
            const Rect area = stage.intersection(viewport).intersection({0, 0, 760, 460});
            const auto unchanged = outside(h, area);
            open.set(true); h.settle();
            center(bounds(h, "main/scroll/stage/dialog"), area);
            check_eq(outside(h, area), unchanged);
            const Point beyond{viewport.right() + 20, bounds(h, "main/scroll/stage/dialog").center().y};
            check(!h.runtime().path(h.runtime().hit_test(beyond)).starts_with("main/scroll/stage/dialog"),
                  "overflowing child paint cannot steal input outside the ancestor clip");
            const Rect outside_button = bounds(h, "main/outside");
            check(h.runtime().hit_test(outside_button.center()) == h.runtime().find("main/outside"), "outside controls stay usable");
            save(h, offset == 0 ? "scroll-boundary-original" : "scroll-boundary-moved");
        }
    });

    suite.test("transformed rounded ancestor clips also constrain overlay drawing and hits", [] {
        Headless h(options());
        h.runtime().set_motion_level(MotionLevel::off);
        auto open = signal(false);
        auto stage = boundary("stage", 300, 180, {
            dialog({.id = "dialog", .modal = false, .open = open, .width = 500, .height = 300, .min_width = 100},
                   {button({.id = "wide", .text = "Wide content", .width = 500})})
        });
        auto clipped = box({.id = "clip", .width = 240, .height = 160, .background = "surface_variant", .radius = 32, .clip = true}, {std::move(stage)});
        clipped.prop("translate_x", Source(Value(48.0)));
        h.mount("main", column({.padding = 32}, {std::move(clipped)}));
        h.settle();
        const Rect clip = bounds(h, "main/clip");
        const Point cut_corner{clip.x + 1, clip.y + 1};
        const auto before = h.pixels().at(static_cast<int>(cut_corner.x), static_cast<int>(cut_corner.y));
        open.set(true); h.settle();
        check(h.pixels().at(static_cast<int>(cut_corner.x), static_cast<int>(cut_corner.y)) == before,
              "overlay paint respects the rounded corner, not merely its rectangle");
        check(!h.runtime().path(h.runtime().hit_test(cut_corner)).starts_with("main/clip/stage/dialog"),
              "the same rounded corner does not intercept input");
        const Rect placed = bounds(h, "main/clip/stage/dialog");
        near(placed.x, clip.x); near(placed.y, clip.y);
        save(h, "transformed-rounded-clip");
    });

    suite.test("a bounded modal scrim paints locally while modal semantics remain global", [] {
        Headless h(options());
        h.runtime().set_motion_level(MotionLevel::off);
        auto open = signal(false);
        h.mount("main", column({.padding = 32, .gap = 20}, {
            boundary("stage", 360, 240, {
                dialog({.id = "dialog", .open = open, .title = "Confirm", .width = 200, .height = 120, .min_width = 160},
                       {button({.id = "ok", .text = "Confirm"})})
            }),
            button({.id = "outside", .text = "Outside"})
        }));
        h.settle();
        const Rect area = bounds(h, "main/stage");
        const auto unchanged = outside(h, area);
        open.set(true); h.settle();
        check_eq(outside(h, area), unchanged);
        const auto node = h.tree().node("main/stage/dialog");
        check(node && node->states.has(State::modal));
        check(h.runtime().hit_test(bounds(h, "main/outside").center()) == h.runtime().find("main/stage/dialog"),
              "a modal boundary changes its presentation, not its blocking semantics");
        check(h.runtime().path(h.runtime().focused()).starts_with("main/stage/dialog/"), "modal focus remains trapped");
        save(h, "scrim-local-modal-semantics");
    });

    return suite.run(argc, argv);
}
