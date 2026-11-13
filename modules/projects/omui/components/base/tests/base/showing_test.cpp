// openmui.ui.base's components of what is shown (W9): chart, carousel —
// their data read as an agent reads it (each point an item), driven as people
// and agents drive them (pointing, arrows, dots, keys, a swipe), and a gallery
// whose pixels are pinned for every platform.
import std;
import omui.ui.base;
import omui.host.headless;
import mlibs.testing;
import mcpplibs.mtest;

using namespace mlibs::testing;
using namespace mcpplibs::mtest;
using namespace omui;

namespace {

mlibs::text::FontCollection& fonts() {
    static mlibs::text::FontCollection collection = [] {
        mlibs::text::FontCollection made;
        (void)made.add_defaults();
        return made;
    }();
    return collection;
}

HeadlessOptions options(Size size = {420, 320}) {
    HeadlessOptions made;
    made.size = size;
    made.fonts = &fonts();
    return made;
}

std::int64_t act(Headless& h, std::string_view id, std::string_view action, Value args = {}) {
    Invocation call;
    call.args = std::move(args);
    call.actor = {"agent", "test", {}};
    const auto answer = h.tree().act(id, action, call);
    h.step();
    return answer.exit_code().value_or(-1);
}

std::int64_t undo(Headless& h) {
    Invocation call;
    call.actor = {"agent", "test", {}};
    const auto answer = h.tree().act("app", "undo", call);
    h.step();
    return answer.exit_code().value_or(-1);
}

std::uint64_t hash_of(const xrender::raster::Surface& pixels) {
    Hash hash;
    for (int y = 0; y < pixels.height(); ++y) hash.bytes(std::as_bytes(std::span(pixels.row(y), static_cast<std::size_t>(pixels.width()))));
    return hash.value();
}

Value series(std::vector<std::pair<std::string, std::vector<double>>> all) {
    Value::List out;
    for (auto& [name, values] : all) {
        Value::List numbers;
        for (const double value : values) numbers.emplace_back(value);
        out.push_back(Value{}.set("name", name).set("values", Value(std::move(numbers))));
    }
    return Value(std::move(out));
}

std::vector<std::string> months() { return {"Jan", "Feb", "Mar", "Apr", "May", "Jun"}; }

std::int64_t index_of(Headless& h) {
    const auto node = h.tree().node("main/slides");
    return node ? node->value.as_int(-1) : -1;
}

}  // namespace

static UTest cases{"ui.base.showing", [] {
    register_base_components();

    "chart: round steps for its scale; each point an item an agent reads exactly; what it shows said; pointing at a step shows its values"_test = [] {
        expect(Chart::step_for(0, 87) == 25.0 && Chart::step_for(0, 9) == 2.5 && Chart::step_for(0, 1000) == 250.0);
        Headless h(options());
        h.mount("main", column({.padding = 12}, {chart({.id = "visits", .label = "Visits", .labels = months(),
                                                         .series = series({{"Visits", {12, 30, 45, 41, 60, 87}}, {"Signups", {2, 5, 9, 8, 12, 20}}})})}));
        h.step();
        const auto node = h.tree().node("main/visits");
        expect(node && node->role == Role::figure && node->name == "Visits" && node->description.contains("2 series over 6 steps") &&
               node->description.contains("from 2 to 87"))
            << (node ? node->description : std::string("none"));
        const auto points = h.runtime().virtual_items(h.runtime().find("main/visits"));
        expect(eq(points.size(), std::size_t{12}));
        const auto march = h.tree().node("main/visits/0/2");
        expect(march && march->name == "Visits, Mar" && march->value == Value(45.0)) << "a point read exactly";
        const auto june = h.tree().node("main/visits/1/5");
        expect(june && june->name == "Signups, Jun" && june->value == Value(20.0));
        // Pointing at a step: its guide and its values drawn over it.
        const std::uint64_t plain = hash_of(h.pixels());
        const Rect box = h.runtime().drawn_bounds(h.runtime().find("main/visits"));
        h.move({box.x + box.width * 0.6f, box.y + box.height / 2});
        expect(hash_of(h.pixels()) != plain) << "the step pointed at, shown";
        h.move({box.x - 4, box.y - 4});
        expect(hash_of(h.pixels()) == plain) << "and gone when it leaves";
    };

    "carousel: one slide in view; an agent, its arrows, its dots, the keys and a swipe move between them; each slide an item; kept within its first and last unless it wraps; taken back"_test = [] {
        Headless h(options());
        std::vector<Element> slides;
        for (const auto* name : {"One", "Two", "Three"})
            slides.push_back(box({.id = std::string("slide-") + name, .height = 160, .background = Fill(std::string("surface_variant")), .label = name},
                                 {text({.text = name})}));
        h.mount("main", column({.padding = 12}, {carousel({.id = "slides", .label = "Pictures"}, std::move(slides))}));
        h.settle();
        const Rect frame = h.runtime().drawn_bounds(h.runtime().find("main/slides"));
        const auto in_view = [&](std::string_view slide) {
            h.settle();
            const Rect drawn = h.runtime().drawn_bounds(h.runtime().find(std::format("main/slides/{}", slide)));
            return std::abs(drawn.x - frame.x) < 1 && std::abs(drawn.width - frame.width) < 1;
        };
        expect(in_view("slide-One")) << "the first, as wide as it";
        const auto node = h.tree().node("main/slides");
        expect(node && node->role == Role::region && node->name == "Pictures" && node->description == "slide 1 of 3");
        expect(eq(act(h, "main/slides", "set_index", Value{}.set("index", 1)), 0) && in_view("slide-Two")) << "an agent";
        expect(act(h, "main/slides", "set_index", Value{}.set("index", 3)) != 0 && eq(index_of(h), std::int64_t{1})) << "within its last";
        // A person: its right arrow, its first dot, the keys.
        h.click({frame.right() - 8 - 16, frame.y + frame.height / 2});
        expect(eq(index_of(h), std::int64_t{2}) && in_view("slide-Three")) << "the right arrow";
        const auto dots = h.runtime().virtual_items(h.runtime().find("main/slides"));
        expect(fatal(dots.size() == 3 && dots[0].name == "Slide 1 of 3" && dots[2].states.has(State::selected) && dots[0].bounds.has_value()));
        h.click({static_cast<float>(dots[0].bounds->x + 4), static_cast<float>(dots[0].bounds->y + 4)});
        expect(eq(index_of(h), std::int64_t{0})) << "a dot";
        h.runtime().focus(h.runtime().find("main/slides"));
        h.key(Key::right);
        expect(eq(index_of(h), std::int64_t{1})) << "Right";
        expect(eq(undo(h), 0) && eq(index_of(h), std::int64_t{0})) << "taken back";
        // A swipe to the left: the next.
        const float y = frame.y + 40;
        PointerEvent down;
        down.kind = PointerKind::down;
        down.position = {frame.x + frame.width * 0.7f, y};
        down.button = MouseButton::left;
        down.buttons = 1;
        h.step({down});
        for (int step = 1; step <= 6; ++step) {
            PointerEvent move = down;
            move.kind = PointerKind::move;
            move.position = {frame.x + frame.width * (0.7f - 0.08f * static_cast<float>(step)), y};
            h.step({move});
        }
        PointerEvent up = down;
        up.kind = PointerKind::up;
        up.buttons = 0;
        up.position = {frame.x + frame.width * 0.22f, y};
        h.step({up});
        expect(eq(index_of(h), std::int64_t{1}) && in_view("slide-Two")) << "a swipe";
    };

    "the gallery: a line, bars and an area over the same months, and a carousel — the same pixels on every platform"_test = [] {
        mlibs::text::FontCollection own;
        expect(own.add_defaults().has_value());
        HeadlessOptions made = options({760, 500});
        made.fonts = &own;
        made.scale = 2;
        Headless h(made);
        const Value data = series({{"Visits", {12, 30, 45, 41, 60, 87}}, {"Signups", {2, 5, 9, 8, 12, 20}}});
        std::vector<Element> slides;
        for (const auto* name : {"One", "Two"})
            slides.push_back(box({.height = 180, .background = Fill(std::string("accent_subtle")), .label = name}, {text({.text = name})}));
        h.mount("main", column({.width = 760, .padding = 16, .gap = 16},
                               {row({.gap = 16}, {chart({.label = "Line", .labels = months(), .series = data}),
                                                  chart({.kind = ChartKind::bar, .label = "Bars", .labels = months(), .series = data})}),
                                row({.gap = 16}, {chart({.kind = ChartKind::area, .label = "Area", .labels = months(),
                                                         .series = series({{"Load", {20, 35, 30, 60, 55, 80}}}), .unit = "%"}),
                                                  carousel({.label = "Slides", .width = 360}, std::move(slides))})}));
        h.settle();
        const std::uint64_t pinned = hash_of(h.pixels());
        std::println("ui.base showing {:016x}", pinned);
        expect(h.save_png(scratch_dir("ui-base-showing") / "showing@2x.png").has_value());
        expect(pinned == 0xc342e5b6bf2495b5ULL) << std::format("showing {:016x}", pinned);
    };
}};
