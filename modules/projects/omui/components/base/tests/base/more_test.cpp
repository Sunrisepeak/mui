// openmui.ui.base's second batch (C2·W4): segmented, chip, range slider,
// alert, stepper, the date, time and colour pickers, … — driven as people and agents drive them (pointer, keys,
// actions, an item of a component as a node of its own), their semantics,
// their history, and a gallery whose pixels are pinned for every platform.
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
    static mlibs::text::FontCollection f = [] {
        mlibs::text::FontCollection c;
        (void)c.add_defaults();
        return c;
    }();
    return f;
}

HeadlessOptions options(Size size = {420, 300}) {
    HeadlessOptions o;
    o.size = size;
    o.fonts = &fonts();
    return o;
}

std::optional<spec::SemanticNode> sem(Headless& h, std::string_view id) {
    auto n = h.tree().node(id);
    return n ? std::optional<spec::SemanticNode>(*n) : std::nullopt;
}

std::int64_t act(Headless& h, std::string_view id, std::string_view action, Value args = {}) {
    Invocation call;
    call.args = std::move(args);
    call.actor = {"agent", "test", {}};
    auto e = h.tree().act(id, action, call);
    h.step();
    return e.exit_code().value_or(-1);
}

bool offers(const spec::SemanticNode& n, std::string_view action) {
    return std::ranges::any_of(n.actions, [&](const spec::ActionDesc& a) { return a.name == action; });
}

// The window's undo, over the bus (as an agent: the person's own is refused there, S-10).
std::int64_t undo(Headless& h) {
    Invocation call;
    call.actor = {"agent", "test", {}};
    auto e = h.tree().act("app", "undo", call);
    h.step();
    return e.exit_code().value_or(-1);
}

std::vector<spec::SemanticNode> items(Headless& h, std::string_view id) { return h.runtime().virtual_items(h.runtime().find(id)); }

// A person's drag, in steps.
void drag(Headless& h, Point from, Point to) {
    auto at = [](PointerKind k, Point p, std::uint32_t held) {
        PointerEvent e;
        e.kind = k;
        e.position = p;
        e.button = k == PointerKind::move ? MouseButton::none : MouseButton::left;
        e.buttons = held;
        return InputEvent{e};
    };
    h.step({at(PointerKind::move, from, 0), at(PointerKind::down, from, 1)});
    for (int i = 1; i <= 8; ++i) h.step({at(PointerKind::move, {from.x + (to.x - from.x) * i / 8, from.y + (to.y - from.y) * i / 8}, 1)});
    h.step({at(PointerKind::up, to, 0)});
}

Point middle(const spec::Rect& r) { return {static_cast<float>(r.x + r.width / 2), static_cast<float>(r.y + r.height / 2)}; }

}  // namespace

static UTest cases{"ui.base.more", [] {
    register_base_components();

    "segmented: one of a few, each a radio of its own; pressed, arrowed, chosen by an agent; taken back"_test = [] {
        Headless h(options());
        auto view = signal(std::string("week"));
        h.mount("main", column({.padding = 8}, {segmented({.id = "view", .label = "View", .labels = std::vector<std::string>{"Day", "Week", "Month"},
                                                           .options = std::vector<std::string>{"day", "week", "month"}, .value = view})}));
        h.step();
        const auto n = sem(h, "main/view");
        expect(n && n->role == Role::radio_group && n->name == "View" && n->value.as_string() == "week") << "a named radio group";
        auto it = items(h, "main/view");
        expect(it.size() == 3 && it[1].role == Role::radio && it[1].states.has(State::checked) && it[0].name == "Day") << "three radios, Week checked";
        expect(offers(it[0], "activate") && !offers(it[1], "activate")) << "the chosen one is not pressed again";
        expect(act(h, "main/view/month", "activate") == 0 && view.peek() == "month") << "an agent chooses Month";
        expect(undo(h) == 0 && view.peek() == "week") << "taken back";
        expect(h.click("main/view")) << "focused";
        h.key(Key::right);
        h.step();
        expect(eq(view.peek(), std::string("month")));
        expect(act(h, "main/view", "set_value", Value{}.set("value", "year")) == 1) << "not a choice";
        // A press on Day: the person's, journaled and undone.
        h.click(middle(*items(h, "main/view")[0].bounds));
        h.step();
        expect(view.peek() == "day" && undo(h) == 0 && view.peek() == "month") << "pressed, then taken back";
    };

    "chip: pressed; a filter on and off (a toggle button), taken back; a tag removed by its ×, an item of its own"_test = [] {
        Headless h(options());
        auto on = signal(false);
        int pressed = 0, removed = 0;
        h.mount("main", row({.padding = 8, .gap = 8}, {chip({.id = "plain", .text = "Help", .on_activated = [&] { ++pressed; }}),
                                                       chip({.id = "filter", .selectable = true, .selected = on, .text = "Open"}),
                                                       chip({.id = "tag", .removable = true, .text = "design", .on_removed = [&] { ++removed; }})}));
        h.step();
        expect(sem(h, "main/plain")->role == Role::button && act(h, "main/plain", "activate") == 0 && pressed == 1) << "a button";
        expect(sem(h, "main/filter")->role == Role::toggle_button && !sem(h, "main/filter")->states.has(State::checked)) << "a toggle button";
        expect(act(h, "main/filter", "activate") == 0 && on.peek() && sem(h, "main/filter")->states.has(State::checked)) << "turned on";
        expect(undo(h) == 0 && !on.peek()) << "taken back";
        expect(!offers(*sem(h, "main/filter"), "remove") && offers(*sem(h, "main/tag"), "remove")) << "only a tag is removed";
        const auto x = items(h, "main/tag");
        expect(x.size() == 1 && x[0].name == "Remove design" && x[0].role == Role::button) << "its × an item of its own";
        expect(act(h, "main/tag/remove", "activate") == 0 && removed == 1) << "removed by an agent";
        h.click("main/tag");
        h.key(Key::delete_);
        h.step();
        expect(eq(removed, 2));
    };

    "range slider: two thumbs, the low never past the high; each a slider of its own; dragged and taken back whole; Tab between them"_test = [] {
        Headless h(options());
        auto low = signal(20.0), high = signal(80.0);
        h.mount("main", column({.padding = 8}, {range_slider({.id = "price", .high = high, .label = "Price", .low = low, .step = 5.0, .width = 220})}));
        h.step();
        auto it = items(h, "main/price");
        expect(it.size() == 2 && it[0].role == Role::slider && it[0].name == "Price from" && it[1].name == "Price to") << "two sliders";
        expect(it[0].value.get("max")->as_double() == 80 && it[1].value.get("min")->as_double() == 20) << "each bounded by the other";
        expect(act(h, "main/price/low", "set_value", Value{}.set("value", 95.0)) == 0 && low.peek() == 80) << "kept on its side: at the high one";
        expect(undo(h) == 0 && low.peek() == 20) << "taken back";
        expect(act(h, "main/price/high", "decrement") == 0 && high.peek() == 75) << "stepped";
        expect(act(h, "main/price", "set_range", Value{}.set("low", 90.0).set("high", 10.0)) == 1) << "a range upside down is refused";
        // The person drags the low thumb to the middle: one action, taken back whole.
        const Rect b = h.runtime().screen_bounds(h.runtime().find("main/price"));
        const auto lo = *items(h, "main/price")[0].bounds;
        const Point from{static_cast<float>(lo.x + lo.width / 2), b.y + b.height / 2};
        drag(h, from, {b.x + b.width / 2, b.y + b.height / 2});
        h.step();
        expect(low.peek() == 50) << std::format("dragged to {}", low.peek());
        expect(undo(h) == 0 && low.peek() == 20) << "the drag taken back as one";
        // Keys: the low thumb first, Tab to the high one.
        h.click({from.x, from.y});
        h.step();
        h.key(Key::right);
        h.key(Key::tab);
        h.key(Key::left);
        h.step();
        expect(low.peek() == 25 && high.peek() == 70) << std::format("{} – {}", low.peek(), high.peek());
    };

    "alert: in the page, its tone said; its action and its dismissal only when it has them"_test = [] {
        Headless h(options({480, 300}));
        auto open = signal(true);
        int acted = 0;
        h.mount("main", column({.padding = 8, .gap = 8}, {
                                   alert({.id = "saved", .message = "Your changes are saved.", .tone = AlertTone::success}),
                                   alert({.id = "quota", .action = "Upgrade", .dismissible = true, .message = "You have used 90% of your storage.", .open = open,
                                          .title = "Almost full", .tone = AlertTone::warning, .on_acted = [&] { ++acted; }}),
                               }));
        h.step();
        const auto a = sem(h, "main/saved"), q = sem(h, "main/quota");
        expect(a->role == Role::status && a->name == "Your changes are saved." && a->value.get("tone")->as_string() == "success") << "a status, its tone";
        expect(!offers(*a, "activate") && !offers(*a, "dismiss")) << "nothing to press on the first";
        expect(q->name == "Almost full: You have used 90% of your storage." && q->description == "Upgrade") << "titled, its action described";
        expect(act(h, "main/quota", "activate") == 0 && acted == 1) << "its action";
        expect(act(h, "main/quota", "dismiss") == 0 && !open.peek() && sem(h, "main/quota")->states.has(State::hidden)) << "dismissed: hidden to every reader";
    };

    "stepper: done, current, ahead; a done step gone back to, an agent's or a press; ahead only when free; taken back"_test = [] {
        Headless h(options({520, 200}));
        auto at = signal(std::int64_t{2});
        h.mount("main", column({.padding = 8}, {stepper({.id = "checkout", .steps = std::vector<std::string>{"Cart", "Address", "Payment", "Review"}, .value = at})}));
        h.step();
        auto it = items(h, "main/checkout");
        expect(it.size() == 4 && it[0].value.as_string() == "done" && it[2].value.as_string() == "current" && it[3].value.as_string() == "ahead") << "done, current, ahead";
        expect(it[0].role == Role::link && offers(it[0], "activate") && !offers(it[3], "activate") && it[2].states.has(State::selected)) << "a done one is a link";
        expect(act(h, "main/checkout/0", "activate") == 0 && at.peek() == 0) << "back to the cart";
        expect(undo(h) == 0 && at.peek() == 2) << "taken back";
        expect(act(h, "main/checkout", "set_value", Value{}.set("value", std::int64_t{3})) == 1 && at.peek() == 2) << "not ahead: its steps come first";
        expect(act(h, "main/checkout", "decrement") == 0 && at.peek() == 1) << "one back";
        h.click(middle(*items(h, "main/checkout")[0].bounds));
        h.step();
        expect(eq(at.peek(), std::int64_t{0}));
    };

    "date picker: any date an item an agent chooses, open or not; min and max hold; the month's days while open; arrows and Enter; taken back"_test = [] {
        Headless h(options({420, 420}));
        auto when = signal(std::string("2026-11-02"));
        h.mount("main", column({.padding = 8}, {date_picker({.id = "when", .label = "Arrival", .max = "2026-12-20", .min = "2026-10-15", .value = when})}));
        h.step();
        const auto n = sem(h, "main/when");
        expect(n && n->role == Role::combo_box && n->name == "Arrival" && !n->states.has(State::expanded)) << "a combo box, closed";
        expect(items(h, "main/when").empty()) << "no days listed while closed";
        expect(act(h, "main/when/2026-11-05", "activate") == 0 && when.peek() == "2026-11-05") << "an agent chooses without opening";
        expect(undo(h) == 0 && when.peek() == "2026-11-02") << "taken back";
        expect(act(h, "main/when/2026-12-25", "activate") != 0 && when.peek() == "2026-11-02") << "after max: refused";
        expect(act(h, "main/when", "set_value", Value{}.set("value", "tomorrow")) == 1) << "not a date";
        expect(act(h, "main/when", "expand") == 0 && sem(h, "main/when")->states.has(State::expanded)) << "open";
        auto days = items(h, "main/when");
        expect(days.size() == 2 + 30 && days[2].name == "1 November 2026" && days[2].bounds && days[3].states.has(State::selected)) << "November's 30 days, placed; the 2nd chosen";
        expect(act(h, "main/when/next_month", "activate") == 0 && items(h, "main/when").size() == 2 + 31) << "December";
        h.click(middle(*h.runtime().virtual_item(h.runtime().find("main/when"), "2026-12-03")->bounds));
        h.step();
        expect(when.peek() == "2026-12-03" && !sem(h, "main/when")->states.has(State::expanded)) << "pressed: chosen, closed";
        expect(undo(h) == 0 && when.peek() == "2026-11-02") << "the press taken back";
        // Keys: open, a day on, Enter.
        expect(h.click("main/when"));
        h.step();
        h.key(Key::right);
        h.key(Key::enter);
        h.step();
        expect(eq(when.peek(), std::string("2026-11-03")));
    };

    "time picker: its hours and minutes spin buttons of their own; stepped, typed, kept on its step; 12-hour with its AM and PM; taken back"_test = [] {
        Headless h(options());
        auto at = signal(std::string("09:30")), late = signal(std::string("21:05"));
        h.mount("main", column({.padding = 8, .gap = 8}, {time_picker({.id = "at", .label = "Start", .step = 5, .value = at}),
                                                          time_picker({.id = "late", .hour12 = true, .label = "End", .value = late})}));
        h.step();
        auto it = items(h, "main/at");
        expect(it.size() == 2 && it[0].role == Role::spin_button && it[0].value.get("value")->as_int() == 9 && it[1].value.get("value")->as_int() == 30) << "hours, minutes";
        expect(act(h, "main/at/hours", "increment") == 0 && at.peek() == "10:30" && undo(h) == 0 && at.peek() == "09:30") << "stepped, taken back";
        expect(act(h, "main/at/minutes", "set_value", Value{}.set("value", std::int64_t{47})) == 0 && at.peek() == "09:45") << "kept on its 5 minutes";
        expect(act(h, "main/at", "set_value", Value{}.set("value", "25:00")) == 1) << "not a time";
        h.click("main/at");
        h.step();
        h.key(Key::up);
        h.key(Key::right);
        h.key(Key::digit1);
        h.key(Key::digit5);
        h.step();
        expect(at.peek() == "10:15") << at.peek();
        auto pm = items(h, "main/late");
        expect(pm.size() == 3 && pm[0].value.get("value")->as_int() == 9 && pm[2].name == "PM") << "09 PM";
        expect(act(h, "main/late/period", "activate") == 0 && late.peek() == "09:05") << "to the morning";
    };

    "colour picker: set at once; its field, hue strip and swatches items of its own while open; grey keeps its hue; a drag taken back whole"_test = [] {
        Headless h(options({420, 420}));
        auto ink = signal(std::string("#4f46e5"));
        h.mount("main", column({.padding = 8}, {color_picker({.id = "ink", .label = "Ink", .swatches = std::vector<std::string>{"#ef4444", "#22c55e", "#3b82f6", "#111827"},
                                                               .value = ink})}));
        h.step();
        expect(sem(h, "main/ink")->role == Role::combo_box && sem(h, "main/ink")->value.as_string() == "#4f46e5") << "its value said";
        expect(act(h, "main/ink", "set_value", Value{}.set("value", "#FF0000")) == 0 && ink.peek() == "#ff0000") << "set, written as #rrggbb";
        expect(undo(h) == 0 && ink.peek() == "#4f46e5") << "taken back";
        expect(act(h, "main/ink", "set_value", Value{}.set("value", "red")) == 1) << "not a colour";
        expect(act(h, "main/ink", "expand") == 0) << "open";
        auto it = items(h, "main/ink");
        expect(it.size() == 2 + 4 && it[0].name == "Saturation and brightness" && it[1].name == "Hue" && it[2].name == "#ef4444" && it[2].bounds) << "its parts";
        expect(act(h, "main/ink/swatch_1", "activate") == 0 && ink.peek() == "#22c55e") << "a swatch";
        expect(act(h, "main/ink/hue", "set_value", Value{}.set("value", 0.0)) == 0 && ink.peek().starts_with("#c")) << ink.peek();
        expect(act(h, "main/ink/field", "set_value", Value{}.set("saturation", 0.0).set("brightness", 100.0)) == 0 && ink.peek() == "#ffffff") << "white";
        expect(act(h, "main/ink/field", "set_value", Value{}.set("saturation", 100.0).set("brightness", 100.0)) == 0 && ink.peek() == "#ff0000") << std::format("its hue kept through white: {}", ink.peek());
        // The person drags in the field: one action, taken back whole.
        const auto field = *items(h, "main/ink")[0].bounds;
        const std::size_t before = h.runtime().history(h.runtime().root()).undo.size();
        drag(h, {static_cast<float>(field.x + 10), static_cast<float>(field.y + 10)}, middle(field));
        expect(ink.peek() != "#ff0000" && h.runtime().history(h.runtime().root()).undo.size() == before + 1) << std::format("dragged to {}", ink.peek());
        expect(undo(h) == 0 && ink.peek() == "#ff0000") << "taken back whole";
    };

    "the pickers open, the same pixels on every platform"_test = [] {
        mlibs::text::FontCollection own;
        expect(own.add_defaults().has_value());
        HeadlessOptions o = options({660, 400});
        o.fonts = &own;
        o.scale = 2;
        Headless h(o);
        h.mount("main", row({.padding = 16, .gap = 24, .align = "start"}, {
                                date_picker({.id = "d", .label = "Arrival", .min = "2026-11-03", .value = std::string("2026-11-12")}),
                                spacer(),
                                column({.id = "right", .gap = 12, .align = "end"}, {time_picker({.id = "t", .hour12 = true, .value = std::string("21:05")}),
                                                     color_picker({.id = "c", .swatches = std::vector<std::string>{"#ef4444", "#f59e0b", "#22c55e", "#06b6d4", "#3b82f6",
                                                                                                                   "#6366f1", "#d946ef", "#111827"},
                                                                   .value = std::string("#3b82f6")})}),
                            }));
        h.step();
        expect(act(h, "main/d", "expand") == 0 && act(h, "main/right/c", "expand") == 0);
        h.settle();
        Hash hash;
        const auto& px = h.pixels();
        for (int y = 0; y < px.height(); ++y) hash.bytes(std::as_bytes(std::span(px.row(y), static_cast<std::size_t>(px.width()))));
        std::println("ui.base pickers {:016x}", hash.value());
        expect(h.save_png(scratch_dir("ui-base-pickers") / "pickers@2x.png").has_value());
        expect(hash.value() == 0xe0ca452249dd50ebULL) << std::format("pickers {:016x}", hash.value());
    };

    "split view: two panes sharing their room as the handle says; the handle a splitter of its own; dragged and taken back whole; kept within min"_test = [] {
        Headless h(options({600, 300}));
        auto share = signal(0.5);
        h.mount("main", column({.padding = 0}, {split_view({.id = "split", .label = "Panels", .ratio = share, .width = Dim(600.0f), .height = Dim(300.0f)},
                                                           {box({.id = "list", .background = "surface_variant"}), box({.id = "detail", .background = "surface"})})}));
        h.settle();
        auto width = [&](std::string_view id) { return h.runtime().screen_bounds(h.runtime().find(id)).width; };
        expect(std::abs(width("main/split/list") - 296) < 1 && std::abs(width("main/split/detail") - 296) < 1) << std::format("halves: {}", width("main/split/list"));
        auto it = items(h, "main/split");
        expect(it.size() == 1 && it[0].role == Role::splitter && it[0].name == "Resize Panels" && it[0].value.get("value")->as_double() == 50) << "its handle";
        expect(act(h, "main/split/handle", "set_value", Value{}.set("value", 30.0)) == 0 && std::abs(share.peek() - 0.3) < 1e-9) << "an agent moves it";
        h.settle();
        expect(std::abs(width("main/split/list") - 0.3f * 592) < 1) << std::format("the list narrower: {}", width("main/split/list"));
        expect(undo(h) == 0 && share.peek() == 0.5) << "taken back";
        expect(act(h, "main/split/handle", "set_value", Value{}.set("value", 2.0)) == 0 && share.peek() == 0.15) << "kept within min";
        expect(undo(h) == 0 && share.peek() == 0.5);
        // The person drags the handle: one action.
        const auto handle = *items(h, "main/split")[0].bounds;
        drag(h, middle(handle), {middle(handle).x + 120, middle(handle).y});
        h.settle();
        expect(share.peek() > 0.65 && share.peek() < 0.75) << std::format("dragged to {}", share.peek());
        expect(undo(h) == 0 && share.peek() == 0.5) << "the drag taken back whole";
        h.key(Key::right);
        h.step();
        expect(std::abs(share.peek() - 0.55) < 1e-9) << std::format("a step by the arrows: {}", share.peek());
    };

    "drawer: along the window's edge, as tall as it, over the page; Escape, its × or an agent close it"_test = [] {
        Headless h(options({640, 400}));
        auto open = signal(false);
        int closed = 0;
        h.mount("main", column({.padding = 16}, {button({.id = "filters", .text = "Filters", .on_activated = [&] { open.set(true); }}),
                                                 drawer({.id = "panel", .open = open, .title = "Filters", .on_closed = [&] { ++closed; }},
                                                        {checkbox({.id = "mine", .label = "Only mine"})})}));
        h.settle();
        expect(sem(h, "main/panel")->states.has(State::hidden)) << "closed: hidden";
        expect(h.click("main/filters"));
        h.settle();
        const Rect b = h.runtime().screen_bounds(h.runtime().find("main/panel"));
        expect(b.x == 640 - 360 && b.y == 0 && b.height == 400 && b.width == 360) << std::format("along the right edge, as tall as the window: {} {} {}×{}", b.x, b.y, b.width, b.height);
        expect(sem(h, "main/panel")->role == Role::dialog && sem(h, "main/panel")->name == "Filters" && offers(*sem(h, "main/panel"), "dismiss")) << "a named dialog";
        expect(h.runtime().path(h.runtime().focused()) == "main/panel/mine") << "its first control focused (modal)";
        h.key(Key::escape);
        h.settle();
        expect(!open.peek() && closed == 1) << "Escape closes it";
        open.set(true);
        h.settle();
        expect(act(h, "main/panel", "dismiss") == 0 && !open.peek() && closed == 2) << "an agent closes it";
    };

    "a split view and a drawer, the same pixels on every platform"_test = [] {
        mlibs::text::FontCollection own;
        expect(own.add_defaults().has_value());
        HeadlessOptions o = options({560, 320});
        o.fonts = &own;
        o.scale = 2;
        Headless h(o);
        h.mount("main", column({.padding = 16}, {
                                   split_view({.id = "split", .ratio = 0.4, .width = Dim(528.0f), .height = Dim(288.0f)},
                                              {column({.id = "list", .padding = 12, .gap = 6, .background = "surface_variant"},
                                                      {text({.text = "Inbox", .style = "label"}), text({.text = "Drafts"}), text({.text = "Sent"})}),
                                               column({.id = "detail", .padding = 12, .background = "surface"}, {text({.text = "No message chosen.", .style = "caption"})})}),
                                   drawer({.id = "panel", .modal = false, .open = true, .size = 200.0, .title = "Filters"},
                                          {checkbox({.id = "mine", .label = "Only mine"}), checkbox({.id = "unread", .checked = true, .label = "Unread"})}),
                               }));
        h.settle();
        Hash hash;
        const auto& px = h.pixels();
        for (int y = 0; y < px.height(); ++y) hash.bytes(std::as_bytes(std::span(px.row(y), static_cast<std::size_t>(px.width()))));
        std::println("ui.base containers {:016x}", hash.value());
        expect(h.save_png(scratch_dir("ui-base-containers") / "containers@2x.png").has_value());
        expect(hash.value() == 0x7dcb343857ea88b7ULL) << std::format("containers {:016x}", hash.value());
    };

    "tree view: a model's rows as a hierarchy; each a tree item opened, closed, chosen by the arrows, a press or an agent; closed ones' rows not stated; taken back"_test = [] {
        Headless h(options({360, 360}));
        Model& files = h.runtime().model("files");
        auto row = [](std::string key, std::string label, std::string parent = {}) {
            Value f = Value{}.set("label", label);
            if (!parent.empty()) f.set("parent", parent);
            return Row{std::move(key), std::move(f)};
        };
        expect(files.push({row("src", "src"), row("app", "app.cpp", "src"), row("ui", "ui", "src"), row("button", "button.cpp", "ui"), row("readme", "README.md")}).has_value());
        auto open = signal(std::vector<std::string>{});
        auto chosen = signal(std::string());
        h.mount("main", column({.padding = 8}, {tree_view({.id = "files", .expanded = open, .label = "Files", .model = "files", .value = chosen})}));
        h.step();
        auto it = items(h, "main/files");
        expect(sem(h, "main/files")->role == Role::tree && it.size() == 2 && it[0].name == "src" && it[0].states.has(State::collapsible) && !it[0].states.has(State::expanded)) << "two roots, src closed";
        expect(act(h, "main/files/src", "expand") == 0 && open.peek() == std::vector<std::string>{"src"} && items(h, "main/files").size() == 4) << "opened: its rows stated";
        expect(undo(h) == 0 && open.peek().empty()) << "taken back";
        expect(act(h, "main/files/button", "activate") == 0 && chosen.peek() == "button" && open.peek().size() == 2) << "a row inside closed ones chosen: they open";
        it = items(h, "main/files");
        expect(it.size() == 5 && it[3].name == "button.cpp" && it[3].value.get("level")->as_int() == 3 && it[3].states.has(State::selected)) << "its level said";
        expect(undo(h) == 0 && chosen.peek().empty()) << "the choice taken back";
        // Keys: down, right opens, right goes in, left goes up, left closes.
        h.click("main/files");
        h.step();
        expect(act(h, "main/files", "set_value", Value{}.set("value", "readme")) == 0);
        h.key(Key::up);
        h.step();
        expect(eq(chosen.peek(), std::string("button")));
        h.key(Key::left);
        h.key(Key::left);
        h.step();
        expect(chosen.peek() == "ui" && std::ranges::find(open.peek(), "ui") == open.peek().end()) << std::format("up to ui, closed: {}", chosen.peek());
        // The model changes: shown and stated again.
        expect(files.push({row("todo", "TODO.md")}).has_value());
        h.step();
        expect(items(h, "main/files").back().name == "TODO.md") << "a new row stated";
    };

    "data grid: a model's rows in columns; sorted by a header (ascending, descending, none); rows chosen one or several; each row and header an item of its own"_test = [] {
        Headless h(options({480, 300}));
        Model& people = h.runtime().model("people");
        auto person = [](std::string key, std::string name, double age, std::string city) {
            return Row{std::move(key), Value{}.set("name", name).set("age", age).set("city", city)};
        };
        expect(people.push({person("ada", "Ada Lovelace", 36, "London"), person("alan", "Alan Turing", 41, "Wilmslow"), person("grace", "Grace Hopper", 85, "Arlington"),
                           person("linus", "linus torvalds", 56, "Portland")}).has_value());
        auto sort = signal(std::string());
        auto chosen = signal(std::vector<std::string>{});
        std::vector<std::string> activated;
        h.mount("main", column({.padding = 8}, {data_grid({.id = "people", .fields = std::vector<std::string>{"name", "age", "city"},
                                                           .headers = std::vector<std::string>{"Name", "Age", "City"}, .label = "People", .model = "people",
                                                           .multiple = true, .selected = chosen, .sort = sort,
                                                           .on_activated = [&](std::string k) { activated.push_back(k); }, .width = Dim(460.0f)})}));
        h.step();
        auto it = items(h, "main/people");
        expect(sem(h, "main/people")->role == Role::grid && it.size() == 3 + 4 && it[0].role == Role::column_header && it[0].name == "Name") << "three headers, four rows";
        expect(it[3].role == Role::row && it[3].name == "Ada Lovelace" && it[3].value.get("age")->as_double() == 36) << "a row, its fields its value";
        expect(act(h, "main/people/col_age", "activate") == 0 && sort.peek() == "age") << "sorted by age";
        expect(act(h, "main/people/col_age", "activate") == 0 && sort.peek() == "-age" && items(h, "main/people")[3].name == "Grace Hopper") << "descending: the eldest first";
        expect(undo(h) == 0 && sort.peek() == "age") << "taken back";
        expect(act(h, "main/people", "set_sort", Value{}.set("sort", "name")) == 0 && items(h, "main/people")[6].name == "linus torvalds") << "names, regardless of case";
        expect(act(h, "main/people", "set_sort", Value{}.set("sort", "salary")) == 1) << "not a column";
        expect(act(h, "main/people/alan", "activate") == 0 && chosen.peek() == std::vector<std::string>{"alan"} && activated == std::vector<std::string>{"alan"}) << "chosen, activated";
        expect(act(h, "main/people/grace", "toggle") == 0 && chosen.peek().size() == 2) << "a second added";
        expect(undo(h) == 0 && chosen.peek().size() == 1) << "taken back";
        // Keys: the row below; Space adds it.
        h.click("main/people");
        h.step();
        h.key(Key::down);
        h.key(Key::space);
        h.step();
        expect(chosen.peek().size() == 2) << std::format("{} chosen", chosen.peek().size());
    };

    "a tree view and a data grid, the same pixels on every platform"_test = [] {
        mlibs::text::FontCollection own;
        expect(own.add_defaults().has_value());
        HeadlessOptions o = options({720, 260});
        o.fonts = &own;
        o.scale = 2;
        Headless h(o);
        Model& files = h.runtime().model("files");
        for (const auto& [k, l, p] : std::vector<std::tuple<std::string, std::string, std::string>>{
                 {"src", "src", ""}, {"app", "app.cpp", "src"}, {"ui", "ui", "src"}, {"button", "button.cpp", "ui"}, {"readme", "README.md", ""}}) {
            Value f = Value{}.set("label", l);
            if (!p.empty()) f.set("parent", p);
            expect(files.push({Row{k, f}}).has_value());
        }
        Model& people = h.runtime().model("people");
        for (const auto& [k, n, a, c] : std::vector<std::tuple<std::string, std::string, double, std::string>>{
                 {"ada", "Ada Lovelace", 36, "London"}, {"alan", "Alan Turing", 41, "Wilmslow"}, {"grace", "Grace Hopper", 85, "Arlington"}})
            expect(people.push({Row{k, Value{}.set("name", n).set("age", a).set("city", c)}}).has_value());
        h.mount("main", row({.padding = 16, .gap = 16, .align = "start"}, {
                                tree_view({.id = "files", .expanded = std::vector<std::string>{"src", "ui"}, .model = "files", .value = std::string("button"),
                                           .width = Dim(200.0f), .height = Dim(220.0f)}),
                                data_grid({.id = "people", .fields = std::vector<std::string>{"name", "age", "city"},
                                           .headers = std::vector<std::string>{"Name", "Age", "City"}, .model = "people", .selected = std::vector<std::string>{"alan"},
                                           .sort = std::string("-age"), .widths = std::vector<std::string>{"*", "70", "*"}, .width = Dim(472.0f)}),
                            }));
        h.settle();
        Hash hash;
        const auto& px = h.pixels();
        for (int y = 0; y < px.height(); ++y) hash.bytes(std::as_bytes(std::span(px.row(y), static_cast<std::size_t>(px.width()))));
        std::println("ui.base collections {:016x}", hash.value());
        expect(h.save_png(scratch_dir("ui-base-collections") / "collections@2x.png").has_value());
        expect(hash.value() == 0xbdf489c318862501ULL) << std::format("collections {:016x}", hash.value());
    };

    "combobox: typing filters the options; arrows and Enter choose; each option an item an agent chooses; free text only when free; taken back"_test = [] {
        Headless h(options({420, 360}));
        auto lang = signal(std::string("cpp"));
        h.mount("main", column({.padding = 8}, {combobox({.id = "lang", .label = "Language", .labels = std::vector<std::string>{"C++", "TypeScript", "Python", "Lua"},
                                                          .options = std::vector<std::string>{"cpp", "ts", "py", "lua"}, .value = lang})}));
        h.step();
        expect(sem(h, "main/lang")->role == Role::combo_box && sem(h, "main/lang/field")->name == "Language") << "a combo box, its field named";
        expect(sem(h, "main/lang/field")->value.as_string() == "C++") << std::format("the field shows C++'s label: {}", sem(h, "main/lang/field")->value.json());
        auto it = items(h, "main/lang");
        expect(it.size() == 4 && it[0].role == Role::option && it[0].states.has(State::selected)) << "four options, C++ chosen";
        expect(act(h, "main/lang/py", "activate") == 0 && lang.peek() == "py") << "an agent chooses Python";
        expect(undo(h) == 0 && lang.peek() == "cpp") << "taken back";
        expect(act(h, "main/lang", "set_value", Value{}.set("value", "rust")) == 1) << "not an option (it is not free)";
        // A person types: the options holding what is typed, the arrows, Enter.
        expect(h.click("main/lang/field"));
        h.key(Key::a, Modifiers(Modifier::ctrl));
        h.type("t");
        h.step();
        expect(sem(h, "main/lang")->states.has(State::expanded)) << "open as one types";
        h.key(Key::down);
        h.key(Key::enter);
        h.step();
        expect(lang.peek() == "py" && !sem(h, "main/lang")->states.has(State::expanded)) << std::format("TypeScript, then Python (both hold t): {}", lang.peek());
    };

    "tag input: tags typed and kept with Enter or a comma, or chosen from suggestions; each a chip removed by its ×; an agent adds and removes; taken back"_test = [] {
        Headless h(options({420, 360}));
        auto tags = signal(std::vector<std::string>{"design"});
        std::vector<std::string> added;
        h.mount("main", column({.padding = 8}, {tag_input({.id = "topics", .label = "Topics", .suggestions = std::vector<std::string>{"motion", "layout", "agents", "design"},
                                                           .tags = tags, .on_added = [&](std::string t) { added.push_back(t); }})}));
        h.step();
        expect(sem(h, "main/topics")->role == Role::group && sem(h, "main/topics/tag_0")->name == "design") << "its tags are chips";
        expect(act(h, "main/topics", "add", Value{}.set("tag", "layout")) == 0 && tags.peek() == std::vector<std::string>{"design", "layout"} && added.size() == 1) << "added";
        expect(act(h, "main/topics", "add", Value{}.set("tag", "layout")) == 1) << "not twice";
        expect(undo(h) == 0 && tags.peek().size() == 1) << "taken back";
        expect(act(h, "main/topics/tag_0/remove", "activate") == 0 && tags.peek().empty()) << "its × removes it";
        expect(undo(h) == 0 && tags.peek() == std::vector<std::string>{"design"}) << "put back where it was";
        // Typed: a comma keeps it; the suggestions holding what is typed, Enter takes the one lit.
        expect(h.click("main/topics/field"));
        h.type("rust,");
        h.step();
        expect(tags.peek().size() == 2 && tags.peek()[1] == "rust") << "a comma keeps it";
        h.type("ag");
        h.step();
        h.key(Key::enter);
        h.step();
        expect(tags.peek().size() == 3 && tags.peek()[2] == "agents") << std::format("the suggestion: {}", tags.peek().back());
    };

    "a combobox open and a tag input, the same pixels on every platform"_test = [] {
        mlibs::text::FontCollection own;
        expect(own.add_defaults().has_value());
        HeadlessOptions o = options({600, 300});
        o.fonts = &own;
        o.scale = 2;
        Headless h(o);
        h.mount("main", row({.padding = 16, .gap = 16, .align = "start"}, {
                                combobox({.id = "lang", .labels = std::vector<std::string>{"C++", "TypeScript", "Python", "Lua"},
                                          .options = std::vector<std::string>{"cpp", "ts", "py", "lua"}, .value = std::string("ts"), .width = Dim(220.0f)}),
                                tag_input({.id = "topics", .tags = std::vector<std::string>{"design", "motion", "agents"}, .width = Dim(330.0f)}),
                            }));
        h.step();
        expect(act(h, "main/lang", "expand") == 0);
        h.settle();
        Hash hash;
        const auto& px = h.pixels();
        for (int y = 0; y < px.height(); ++y) hash.bytes(std::as_bytes(std::span(px.row(y), static_cast<std::size_t>(px.width()))));
        std::println("ui.base entry {:016x}", hash.value());
        expect(h.save_png(scratch_dir("ui-base-entry") / "entry@2x.png").has_value());
        expect(hash.value() == 0x69641969a6aab991ULL) << std::format("entry {:016x}", hash.value());
    };

    "code editor: its field a text box in code; set whole and taken back; Enter keeps the indent, Tab indents, Escape then Tab leaves"_test = [] {
        Headless h(options({480, 360}));
        auto code = signal(std::string("int main() {\n    return 0;"));
        h.mount("main", column({.padding = 8, .gap = 8}, {code_editor({.id = "code", .text = code}), button({.id = "after", .text = "Run"})}));
        h.step();
        expect(sem(h, "main/code")->name == "C++ code" && sem(h, "main/code")->value.get("lines")->as_int() == 2) << "named for its language, its lines counted";
        expect(sem(h, "main/code/text")->role == Role::text_box && sem(h, "main/code/text")->states.has(State::multiline)) << "its field a multi-line text box";
        expect(act(h, "main/code", "set_value", Value{}.set("value", "x = 1")) == 0 && code.peek() == "x = 1") << "set whole";
        expect(undo(h) == 0 && code.peek() == "int main() {\n    return 0;") << "taken back";
        // Typed at the end: Enter keeps the indent; Tab indents.
        expect(h.click("main/code/text"));
        h.key(Key::end, Modifiers(Modifier::ctrl));
        h.key(Key::enter);
        h.type("x;");
        h.step();
        expect(code.peek().ends_with("return 0;\n    x;")) << std::format("{:?}", code.peek());
        h.key(Key::tab);
        h.step();
        expect(code.peek().ends_with("x;    ") && h.runtime().path(h.runtime().focused()) == "main/code/text") << "Tab indents, the focus kept";
        h.key(Key::escape);
        h.key(Key::tab);
        h.step();
        expect(eq(h.runtime().path(h.runtime().focused()), std::string("main/after")));
    };

    "a code editor, the same pixels on every platform"_test = [] {
        mlibs::text::FontCollection own;
        expect(own.add_defaults().has_value());
        HeadlessOptions o = options({480, 250});
        o.fonts = &own;
        o.scale = 2;
        Headless h(o);
        h.mount("main", column({.padding = 16}, {code_editor({.id = "code", .lines = 7,
                                                              .text = std::string("// A greeting, counted.\nimport std;\n\nint main() {\n    int n = 3;\n"
                                                                                  "    std::println(\"hello, openmui\");\n    return n - 3;\n}")})}));
        h.settle();
        Hash hash;
        const auto& px = h.pixels();
        for (int y = 0; y < px.height(); ++y) hash.bytes(std::as_bytes(std::span(px.row(y), static_cast<std::size_t>(px.width()))));
        std::println("ui.base code {:016x}", hash.value());
        expect(h.save_png(scratch_dir("ui-base-code") / "code@2x.png").has_value());
        expect(hash.value() == 0x300f5cb5bfc41d5aULL) << std::format("code {:016x}", hash.value());
    };

    "the gallery: the second batch, the same pixels on every platform"_test = [] {
        mlibs::text::FontCollection own;
        expect(own.add_defaults().has_value());
        HeadlessOptions o = options({480, 380});
        o.fonts = &own;
        o.scale = 2;
        Headless h(o);
        h.mount("main", column({.padding = 16, .gap = 14}, {
                                   segmented({.id = "seg", .labels = std::vector<std::string>{"Day", "Week", "Month"},
                                              .options = std::vector<std::string>{"day", "week", "month"}, .value = std::string("week")}),
                                   row({.gap = 8}, {chip({.id = "c1", .text = "Help"}), chip({.id = "c2", .selectable = true, .selected = true, .text = "Open"}),
                                                    chip({.id = "c3", .removable = true, .text = "design"})}),
                                   range_slider({.id = "range", .high = 70.0, .low = 25.0, .width = 260}),
                                   alert({.id = "al", .action = "Upgrade", .dismissible = true, .message = "You have used 90% of your storage.",
                                          .title = "Almost full", .tone = AlertTone::warning, .width = 448}),
                                   stepper({.id = "steps", .steps = std::vector<std::string>{"Cart", "Address", "Payment", "Review"}, .value = std::int64_t{2},
                                            .width = 448}),
                               }));
        h.settle();
        Hash hash;
        const auto& px = h.pixels();
        for (int y = 0; y < px.height(); ++y) hash.bytes(std::as_bytes(std::span(px.row(y), static_cast<std::size_t>(px.width()))));
        std::println("ui.base second batch gallery {:016x}", hash.value());
        expect(h.save_png(scratch_dir("ui-base-more") / "more@2x.png").has_value());
        expect(hash.value() == 0xbe5531ecf232a8a2ULL) << std::format("gallery {:016x}", hash.value());
    };
}};
