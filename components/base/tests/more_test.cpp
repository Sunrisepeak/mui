// openmui.ui.base's second batch (C2·W4): segmented, chip, range slider,
// alert, stepper, the date, time and colour pickers, … — driven as people and agents drive them (pointer, keys,
// actions, an item of a component as a node of its own), their semantics,
// their history, and a gallery whose pixels are pinned for every platform.
import std;
import openmui.ui.base;
import openmui.host.headless;
import openmui.testing;

using namespace omui::testing;
using namespace omui;

namespace {

typeset::FontCollection& fonts() {
    static typeset::FontCollection f = [] {
        typeset::FontCollection c;
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

int main(int argc, char** argv) {
    Suite s{"ui.base.more"};
    register_base_components();

    s.test("segmented: one of a few, each a radio of its own; pressed, arrowed, chosen by an agent; taken back", [] {
        Headless h(options());
        auto view = signal(std::string("week"));
        h.mount("main", column({.padding = 8}, {segmented({.id = "view", .label = "View", .labels = std::vector<std::string>{"Day", "Week", "Month"},
                                                           .options = std::vector<std::string>{"day", "week", "month"}, .value = view})}));
        h.step();
        const auto n = sem(h, "main/view");
        check(n && n->role == Role::radio_group && n->name == "View" && n->value.as_string() == "week", "a named radio group");
        auto it = items(h, "main/view");
        check(it.size() == 3 && it[1].role == Role::radio && it[1].states.has(State::checked) && it[0].name == "Day", "three radios, Week checked");
        check(offers(it[0], "activate") && !offers(it[1], "activate"), "the chosen one is not pressed again");
        check(act(h, "main/view/month", "activate") == 0 && view.peek() == "month", "an agent chooses Month");
        check(undo(h) == 0 && view.peek() == "week", "taken back");
        check(h.click("main/view"), "focused");
        h.key(Key::right);
        h.step();
        check_eq(view.peek(), std::string("month"));
        check(act(h, "main/view", "set_value", Value{}.set("value", "year")) == 1, "not a choice");
        // A press on Day: the person's, journaled and undone.
        h.click(middle(*items(h, "main/view")[0].bounds));
        h.step();
        check(view.peek() == "day" && undo(h) == 0 && view.peek() == "month", "pressed, then taken back");
    });

    s.test("chip: pressed; a filter on and off (a toggle button), taken back; a tag removed by its ×, an item of its own", [] {
        Headless h(options());
        auto on = signal(false);
        int pressed = 0, removed = 0;
        h.mount("main", row({.padding = 8, .gap = 8}, {chip({.id = "plain", .text = "Help", .on_activated = [&] { ++pressed; }}),
                                                       chip({.id = "filter", .selectable = true, .selected = on, .text = "Open"}),
                                                       chip({.id = "tag", .removable = true, .text = "design", .on_removed = [&] { ++removed; }})}));
        h.step();
        check(sem(h, "main/plain")->role == Role::button && act(h, "main/plain", "activate") == 0 && pressed == 1, "a button");
        check(sem(h, "main/filter")->role == Role::toggle_button && !sem(h, "main/filter")->states.has(State::checked), "a toggle button");
        check(act(h, "main/filter", "activate") == 0 && on.peek() && sem(h, "main/filter")->states.has(State::checked), "turned on");
        check(undo(h) == 0 && !on.peek(), "taken back");
        check(!offers(*sem(h, "main/filter"), "remove") && offers(*sem(h, "main/tag"), "remove"), "only a tag is removed");
        const auto x = items(h, "main/tag");
        check(x.size() == 1 && x[0].name == "Remove design" && x[0].role == Role::button, "its × an item of its own");
        check(act(h, "main/tag/remove", "activate") == 0 && removed == 1, "removed by an agent");
        h.click("main/tag");
        h.key(Key::delete_);
        h.step();
        check_eq(removed, 2);
    });

    s.test("range slider: two thumbs, the low never past the high; each a slider of its own; dragged and taken back whole; Tab between them", [] {
        Headless h(options());
        auto low = signal(20.0), high = signal(80.0);
        h.mount("main", column({.padding = 8}, {range_slider({.id = "price", .high = high, .label = "Price", .low = low, .step = 5.0, .width = 220})}));
        h.step();
        auto it = items(h, "main/price");
        check(it.size() == 2 && it[0].role == Role::slider && it[0].name == "Price from" && it[1].name == "Price to", "two sliders");
        check(it[0].value.get("max")->as_double() == 80 && it[1].value.get("min")->as_double() == 20, "each bounded by the other");
        check(act(h, "main/price/low", "set_value", Value{}.set("value", 95.0)) == 0 && low.peek() == 80, "kept on its side: at the high one");
        check(undo(h) == 0 && low.peek() == 20, "taken back");
        check(act(h, "main/price/high", "decrement") == 0 && high.peek() == 75, "stepped");
        check(act(h, "main/price", "set_range", Value{}.set("low", 90.0).set("high", 10.0)) == 1, "a range upside down is refused");
        // The person drags the low thumb to the middle: one action, taken back whole.
        const Rect b = h.runtime().screen_bounds(h.runtime().find("main/price"));
        const auto lo = *items(h, "main/price")[0].bounds;
        const Point from{static_cast<float>(lo.x + lo.width / 2), b.y + b.height / 2};
        drag(h, from, {b.x + b.width / 2, b.y + b.height / 2});
        h.step();
        check(low.peek() == 50, std::format("dragged to {}", low.peek()));
        check(undo(h) == 0 && low.peek() == 20, "the drag taken back as one");
        // Keys: the low thumb first, Tab to the high one.
        h.click({from.x, from.y});
        h.step();
        h.key(Key::right);
        h.key(Key::tab);
        h.key(Key::left);
        h.step();
        check(low.peek() == 25 && high.peek() == 70, std::format("{} – {}", low.peek(), high.peek()));
    });

    s.test("alert: in the page, its tone said; its action and its dismissal only when it has them", [] {
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
        check(a->role == Role::status && a->name == "Your changes are saved." && a->value.get("tone")->as_string() == "success", "a status, its tone");
        check(!offers(*a, "activate") && !offers(*a, "dismiss"), "nothing to press on the first");
        check(q->name == "Almost full: You have used 90% of your storage." && q->description == "Upgrade", "titled, its action described");
        check(act(h, "main/quota", "activate") == 0 && acted == 1, "its action");
        check(act(h, "main/quota", "dismiss") == 0 && !open.peek() && sem(h, "main/quota")->states.has(State::hidden), "dismissed: hidden to every reader");
    });

    s.test("stepper: done, current, ahead; a done step gone back to, an agent's or a press; ahead only when free; taken back", [] {
        Headless h(options({520, 200}));
        auto at = signal(std::int64_t{2});
        h.mount("main", column({.padding = 8}, {stepper({.id = "checkout", .steps = std::vector<std::string>{"Cart", "Address", "Payment", "Review"}, .value = at})}));
        h.step();
        auto it = items(h, "main/checkout");
        check(it.size() == 4 && it[0].value.as_string() == "done" && it[2].value.as_string() == "current" && it[3].value.as_string() == "ahead", "done, current, ahead");
        check(it[0].role == Role::link && offers(it[0], "activate") && !offers(it[3], "activate") && it[2].states.has(State::selected), "a done one is a link");
        check(act(h, "main/checkout/0", "activate") == 0 && at.peek() == 0, "back to the cart");
        check(undo(h) == 0 && at.peek() == 2, "taken back");
        check(act(h, "main/checkout", "set_value", Value{}.set("value", std::int64_t{3})) == 1 && at.peek() == 2, "not ahead: its steps come first");
        check(act(h, "main/checkout", "decrement") == 0 && at.peek() == 1, "one back");
        h.click(middle(*items(h, "main/checkout")[0].bounds));
        h.step();
        check_eq(at.peek(), std::int64_t{0});
    });

    s.test("date picker: any date an item an agent chooses, open or not; min and max hold; the month's days while open; arrows and Enter; taken back", [] {
        Headless h(options({420, 420}));
        auto when = signal(std::string("2026-11-02"));
        h.mount("main", column({.padding = 8}, {date_picker({.id = "when", .label = "Arrival", .max = "2026-12-20", .min = "2026-10-15", .value = when})}));
        h.step();
        const auto n = sem(h, "main/when");
        check(n && n->role == Role::combo_box && n->name == "Arrival" && !n->states.has(State::expanded), "a combo box, closed");
        check(items(h, "main/when").empty(), "no days listed while closed");
        check(act(h, "main/when/2026-11-05", "activate") == 0 && when.peek() == "2026-11-05", "an agent chooses without opening");
        check(undo(h) == 0 && when.peek() == "2026-11-02", "taken back");
        check(act(h, "main/when/2026-12-25", "activate") != 0 && when.peek() == "2026-11-02", "after max: refused");
        check(act(h, "main/when", "set_value", Value{}.set("value", "tomorrow")) == 1, "not a date");
        check(act(h, "main/when", "expand") == 0 && sem(h, "main/when")->states.has(State::expanded), "open");
        auto days = items(h, "main/when");
        check(days.size() == 2 + 30 && days[2].name == "1 November 2026" && days[2].bounds && days[3].states.has(State::selected), "November's 30 days, placed; the 2nd chosen");
        check(act(h, "main/when/next_month", "activate") == 0 && items(h, "main/when").size() == 2 + 31, "December");
        h.click(middle(*h.runtime().virtual_item(h.runtime().find("main/when"), "2026-12-03")->bounds));
        h.step();
        check(when.peek() == "2026-12-03" && !sem(h, "main/when")->states.has(State::expanded), "pressed: chosen, closed");
        check(undo(h) == 0 && when.peek() == "2026-11-02", "the press taken back");
        // Keys: open, a day on, Enter.
        check(h.click("main/when"));
        h.step();
        h.key(Key::right);
        h.key(Key::enter);
        h.step();
        check_eq(when.peek(), std::string("2026-11-03"));
    });

    s.test("time picker: its hours and minutes spin buttons of their own; stepped, typed, kept on its step; 12-hour with its AM and PM; taken back", [] {
        Headless h(options());
        auto at = signal(std::string("09:30")), late = signal(std::string("21:05"));
        h.mount("main", column({.padding = 8, .gap = 8}, {time_picker({.id = "at", .label = "Start", .step = 5, .value = at}),
                                                          time_picker({.id = "late", .hour12 = true, .label = "End", .value = late})}));
        h.step();
        auto it = items(h, "main/at");
        check(it.size() == 2 && it[0].role == Role::spin_button && it[0].value.get("value")->as_int() == 9 && it[1].value.get("value")->as_int() == 30, "hours, minutes");
        check(act(h, "main/at/hours", "increment") == 0 && at.peek() == "10:30" && undo(h) == 0 && at.peek() == "09:30", "stepped, taken back");
        check(act(h, "main/at/minutes", "set_value", Value{}.set("value", std::int64_t{47})) == 0 && at.peek() == "09:45", "kept on its 5 minutes");
        check(act(h, "main/at", "set_value", Value{}.set("value", "25:00")) == 1, "not a time");
        h.click("main/at");
        h.step();
        h.key(Key::up);
        h.key(Key::right);
        h.key(Key::digit1);
        h.key(Key::digit5);
        h.step();
        check(at.peek() == "10:15", at.peek());
        auto pm = items(h, "main/late");
        check(pm.size() == 3 && pm[0].value.get("value")->as_int() == 9 && pm[2].name == "PM", "09 PM");
        check(act(h, "main/late/period", "activate") == 0 && late.peek() == "09:05", "to the morning");
    });

    s.test("colour picker: set at once; its field, hue strip and swatches items of its own while open; grey keeps its hue; a drag taken back whole", [] {
        Headless h(options({420, 420}));
        auto ink = signal(std::string("#4f46e5"));
        h.mount("main", column({.padding = 8}, {color_picker({.id = "ink", .label = "Ink", .swatches = std::vector<std::string>{"#ef4444", "#22c55e", "#3b82f6", "#111827"},
                                                               .value = ink})}));
        h.step();
        check(sem(h, "main/ink")->role == Role::combo_box && sem(h, "main/ink")->value.as_string() == "#4f46e5", "its value said");
        check(act(h, "main/ink", "set_value", Value{}.set("value", "#FF0000")) == 0 && ink.peek() == "#ff0000", "set, written as #rrggbb");
        check(undo(h) == 0 && ink.peek() == "#4f46e5", "taken back");
        check(act(h, "main/ink", "set_value", Value{}.set("value", "red")) == 1, "not a colour");
        check(act(h, "main/ink", "expand") == 0, "open");
        auto it = items(h, "main/ink");
        check(it.size() == 2 + 4 && it[0].name == "Saturation and brightness" && it[1].name == "Hue" && it[2].name == "#ef4444" && it[2].bounds, "its parts");
        check(act(h, "main/ink/swatch_1", "activate") == 0 && ink.peek() == "#22c55e", "a swatch");
        check(act(h, "main/ink/hue", "set_value", Value{}.set("value", 0.0)) == 0 && ink.peek().starts_with("#c"), ink.peek());
        check(act(h, "main/ink/field", "set_value", Value{}.set("saturation", 0.0).set("brightness", 100.0)) == 0 && ink.peek() == "#ffffff", "white");
        check(act(h, "main/ink/field", "set_value", Value{}.set("saturation", 100.0).set("brightness", 100.0)) == 0 && ink.peek() == "#ff0000",
              std::format("its hue kept through white: {}", ink.peek()));
        // The person drags in the field: one action, taken back whole.
        const auto field = *items(h, "main/ink")[0].bounds;
        const std::size_t before = h.runtime().history(h.runtime().root()).undo.size();
        drag(h, {static_cast<float>(field.x + 10), static_cast<float>(field.y + 10)}, middle(field));
        check(ink.peek() != "#ff0000" && h.runtime().history(h.runtime().root()).undo.size() == before + 1, std::format("dragged to {}", ink.peek()));
        check(undo(h) == 0 && ink.peek() == "#ff0000", "taken back whole");
    });

    s.test("the pickers open, the same pixels on every platform", [] {
        typeset::FontCollection own;
        check(own.add_defaults().has_value());
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
        check(act(h, "main/d", "expand") == 0 && act(h, "main/right/c", "expand") == 0);
        h.settle();
        Hash hash;
        const auto& px = h.pixels();
        for (int y = 0; y < px.height(); ++y) hash.bytes(std::as_bytes(std::span(px.row(y), static_cast<std::size_t>(px.width()))));
        std::println("ui.base pickers {:016x}", hash.value());
        check(h.save_png(scratch_dir("ui-base-pickers") / "pickers@2x.png").has_value());
        check(hash.value() == 0x746751dd38b85266ULL, std::format("pickers {:016x}", hash.value()));
    });

    s.test("the gallery: the second batch, the same pixels on every platform", [] {
        typeset::FontCollection own;
        check(own.add_defaults().has_value());
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
        check(h.save_png(scratch_dir("ui-base-more") / "more@2x.png").has_value());
        check(hash.value() == 0xbfa71ef84c6042fbULL, std::format("gallery {:016x}", hash.value()));
    });

    return s.run(argc, argv);
}
