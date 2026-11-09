// openmui.ui.base's components of what is read and points on (W9): kbd,
// separator, link, description list, spinner — driven as people and agents
// drive them (keys, a click, actions), their semantics, and a gallery whose
// pixels are pinned for every platform.
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

HeadlessOptions options(Size size = {420, 300}) {
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

std::uint64_t hash_of(const xrender::raster::Surface& pixels) {
    Hash hash;
    for (int y = 0; y < pixels.height(); ++y) hash.bytes(std::as_bytes(std::span(pixels.row(y), static_cast<std::size_t>(pixels.width()))));
    return hash.value();
}

}  // namespace

UTest cases{"ui.base.reading", [] {
    register_base_components();

    "kbd: each key a cap, chords pressed in turn spaced apart; Mod the system's command key; read as the keys' names"_test = [] {
        expect(Kbd::chords("Mod+Shift+P", false) == std::vector<std::vector<std::string>>{{"Ctrl", "Shift", "P"}});
        expect(Kbd::chords("Mod+K G", true) == std::vector<std::vector<std::string>>{{"⌘", "K"}, {"G"}});
        expect(Kbd::spoken("Mod+Shift+P", false) == "Control+Shift+P");
        expect(Kbd::spoken("Mod+K G", true) == "Command+K, then G");
        Headless h(options());
        h.mount("main", column({.padding = 12}, {kbd({.id = "keys", .keys = std::string("Mod+Shift+P")})}));
        h.step();
        const auto node = h.tree().node("main/keys");
        expect(node && node->value == Value("Mod+Shift+P") && !node->name.empty()) << (node ? node->name : std::string("none"));
        const Rect drawn = h.runtime().drawn_bounds(h.runtime().find("main/keys"));
        expect(drawn.width > 60 && drawn.height >= 18) << "three caps side by side: " << drawn.width << "x" << drawn.height;
    };

    "separator: a line across as wide as it is given, or down; a word in its middle when it has one; a separator to every reader"_test = [] {
        Headless h(options());
        h.mount("main", column({.width = 240, .padding = 12, .gap = 8},
                               {separator({.id = "plain"}), separator({.id = "or", .label = std::string("or")}),
                                row({.id = "side", .height = 40}, {separator({.id = "down", .vertical = true})})}));
        h.step();
        const Rect across = h.runtime().drawn_bounds(h.runtime().find("main/plain"));
        expect(across.width > 200 && across.height <= 2) << across.width << "x" << across.height;
        const auto worded = h.tree().node("main/or");
        expect(worded && worded->role == Role::separator && worded->name == "or" && worded->value == Value("horizontal"));
        const Rect down = h.runtime().drawn_bounds(h.runtime().find("main/side/down"));
        expect(down.height >= 16 && down.width <= 2) << down.width << "x" << down.height;
    };

    "link: followed by an agent, a key or a click — the address opened by the system, or the verb invoked; external; refused when it cannot go"_test = [] {
        Headless h(options());
        std::vector<std::string> opened;
        h.runtime().set_url_opener([&](std::string_view url) {
            opened.emplace_back(url);
            return true;
        });
        int noted = 0;
        (void)h.runtime().verbs().add({.name = "notes.open", .effect = Effect::reversible}, [&](const Value&) -> VerbResult {
            ++noted;
            return Value{};
        });
        std::vector<std::string> followed;
        h.mount("main", column({.padding = 12, .gap = 8},
                               {link({.id = "docs", .href = std::string("https://example.org/docs"), .text = std::string("Read the docs"),
                                      .on_activated = [&](std::string href) { followed.push_back(href); }}),
                                link({.id = "notes", .text = std::string("Open notes"), .verb = "notes.open"}),
                                link({.id = "off", .disabled = true, .href = std::string("https://example.org/gone"), .text = std::string("Gone")}),
                                link({.id = "file", .href = std::string("file:///etc/passwd"), .text = std::string("A file")})}));
        h.step();
        const auto docs = h.tree().node("main/docs");
        expect(docs && docs->role == Role::link && docs->name == "Read the docs" && docs->value == Value("https://example.org/docs"));
        expect(docs && std::ranges::any_of(docs->actions, [](const spec::ActionDesc& action) { return action.name == "activate" && action.effect == Effect::external; }))
            << "an address: external";
        const auto notes = h.tree().node("main/notes");
        expect(notes && std::ranges::any_of(notes->actions, [](const spec::ActionDesc& action) { return action.name == "activate" && action.effect == Effect::reversible; }))
            << "a verb's link: its verb's effect";
        // An agent follows an address only on the answer "yes" (S-1): asked first, then answered.
        expect(eq(act(h, "main/docs", "activate"), 2) && opened.empty()) << "needs the answer";
        {
            Invocation call;
            call.actor = {"agent", "test", {}};
            call.answer = "yes";
            expect(eq(h.tree().act("main/docs", "activate", call).exit_code().value_or(-1), 0));
            h.step();
        }
        expect(opened == std::vector<std::string>{"https://example.org/docs"} && followed == std::vector<std::string>{"https://example.org/docs"});
        expect(eq(act(h, "main/notes", "activate"), 0) && eq(noted, 1)) << "a verb's link invokes the verb";
        expect(act(h, "main/off", "activate") != 0 && act(h, "main/file", "activate") != 0) << "disabled; a file is not an address the system opens";
        expect(eq(opened.size(), std::size_t{1}));
        // A person: Tab to it and Enter; a click.
        h.runtime().focus(h.runtime().find("main/docs"));
        h.step();
        h.key(Key::enter);
        expect(eq(opened.size(), std::size_t{2})) << "Enter follows it";
        expect(h.click("main/docs") && eq(opened.size(), std::size_t{3})) << "and a click";
    };

    "description list: terms beside what they mean, each pair an item of its own — its name the term, its value the description"_test = [] {
        Headless h(options());
        h.mount("main", column({.padding = 12},
                               {description_list({.id = "plan", .details = std::vector<std::string>{"Team, 29 a month", "1 December", "5 of 10"}, .label = "Your plan",
                                                  .terms = std::vector<std::string>{"Plan", "Renews", "Seats"}})}));
        h.step();
        const auto list = h.tree().node("main/plan");
        expect(list && list->role == Role::list && list->name == "Your plan" && list->value == Value(std::int64_t{3}));
        const auto items = h.runtime().virtual_items(h.runtime().find("main/plan"));
        expect(fatal(items.size() == 3));
        expect(items[0].name == "Plan" && items[0].value == Value("Team, 29 a month") && items[2].name == "Seats" && items[2].value == Value("5 of 10"));
        expect(items[0].bounds && items[1].bounds && items[1].bounds->y > items[0].bounds->y) << "one under another";
        const auto renews = h.tree().node("main/plan/1");
        expect(renews && renews->name == "Renews") << "an item found by its path";
    };

    "spinner: an arc turning on the runtime's clock, busy to every reader; still when motion is reduced"_test = [] {
        Headless h(options({80, 80}));
        h.mount("main", column({.padding = 20}, {spinner({.id = "busy", .label = std::string("Saving")})}));
        h.step();
        const auto node = h.tree().node("main/busy");
        expect(node && node->role == Role::progress && node->name == "Saving" && node->states.has(State::busy));
        const std::uint64_t now = hash_of(h.pixels());
        (void)h.advance(seconds(0.3));
        expect(hash_of(h.pixels()) != now) << "it turns";
        h.runtime().set_reduce_motion(true);
        h.step();
        const std::uint64_t still = hash_of(h.pixels());
        (void)h.advance(seconds(0.3));
        expect(hash_of(h.pixels()) == still) << "reduced motion: still";
    };

    "the gallery: kbd, separators, links, a description list and a spinner at rest — the same pixels on every platform"_test = [] {
        mlibs::text::FontCollection own;
        expect(own.add_defaults().has_value());
        HeadlessOptions made = options({360, 280});
        made.fonts = &own;
        made.scale = 2;
        Headless h(made);
        h.runtime().set_reduce_motion(true);  // the spinner at rest
        h.mount("main", column({.width = 360, .padding = 16, .gap = 12},
                               {row({.gap = 12, .align = "center"}, {kbd({.keys = std::string("Ctrl+Shift+P")}), kbd({.keys = std::string("G G")}), spinner({})}),
                                separator({.label = std::string("or")}),
                                row({.gap = 16}, {link({.href = std::string("https://example.org"), .text = std::string("Read the docs")}),
                                                  link({.disabled = true, .href = std::string("https://example.org"), .text = std::string("Disabled")})}),
                                separator({}),
                                description_list({.details = std::vector<std::string>{"Team, 29 a month", "1 December"}, .terms = std::vector<std::string>{"Plan", "Renews"}})}));
        h.settle();
        const std::uint64_t pinned = hash_of(h.pixels());
        std::println("ui.base reading {:016x}", pinned);
        expect(h.save_png(scratch_dir("ui-base-reading") / "reading@2x.png").has_value());
        expect(pinned == 0x037531aff5e6b3caULL) << std::format("reading {:016x}", pinned);
    };
}};
