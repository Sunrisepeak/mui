// openmui.ui.base's components of what a person gives (W9): rating, otp
// input, form field, search box, toggle group — driven as people and agents
// drive them (a pointer, keys, typing, actions, an item as a node of its
// own), their semantics, their history, and a gallery whose pixels are pinned
// for every platform.
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

std::int64_t undo(Headless& h) {
    Invocation call;
    call.actor = {"agent", "test", {}};
    const auto answer = h.tree().act("app", "undo", call);
    h.step();
    return answer.exit_code().value_or(-1);
}

Value value_of(Headless& h, std::string_view id) {
    const auto node = h.tree().node(id);
    return node ? node->value : Value{};
}

void click_at(Headless& h, Point at) {
    PointerEvent move;
    move.kind = PointerKind::move;
    move.position = at;
    PointerEvent down = move;
    down.kind = PointerKind::down;
    down.button = MouseButton::left;
    down.buttons = 1;
    PointerEvent up = down;
    up.kind = PointerKind::up;
    up.buttons = 0;
    h.step({move, down, up});
}

std::uint64_t hash_of(const xrender::raster::Surface& pixels) {
    Hash hash;
    for (int y = 0; y < pixels.height(); ++y) hash.bytes(std::as_bytes(std::span(pixels.row(y), static_cast<std::size_t>(pixels.width()))));
    return hash.value();
}

}  // namespace

static UTest cases{"ui.base.inputs", [] {
    register_base_components();

    "rating: a slider to every reader; set, stepped and taken back by an agent; a press sets the star under it, pressing its value clears it; read-only only shows"_test = [] {
        Headless h(options());
        h.mount("main", column({.padding = 12, .gap = 8}, {rating({.id = "quality", .half = true, .label = "Quality"}),
                                                          rating({.id = "fixed", .label = "Fixed", .readonly = true, .value = 4.0})}));
        h.step();
        const auto read = [&](std::string_view id) { return value_of(h, id).get("value") ? value_of(h, id).get("value")->as_double() : -1.0; };
        const auto node = h.tree().node("main/quality");
        expect(node && node->role == Role::slider && node->name == "Quality" && value_of(h, "main/quality").get("max")->as_double() == 5.0 &&
               value_of(h, "main/quality").get("step")->as_double() == 0.5);
        expect(eq(act(h, "main/quality", "set_value", Value{}.set("value", 3.4)), 0) && eq(read("main/quality"), 3.5)) << "on its half steps";
        expect(eq(act(h, "main/quality", "increment"), 0) && eq(read("main/quality"), 4.0));
        expect(eq(act(h, "main/quality", "set_value", Value{}.set("value", "max")), 0) && eq(read("main/quality"), 5.0));
        expect(eq(undo(h), 0) && eq(read("main/quality"), 4.0)) << "taken back";
        // A person: the second star's right half sets 2; pressing 2 again clears it.
        const Rect stars = h.runtime().drawn_bounds(h.runtime().find("main/quality"));
        const Point second{stars.x + 22 + 4 + 18, stars.y + 11};
        click_at(h, second);
        expect(eq(read("main/quality"), 2.0)) << "the star under the press";
        click_at(h, second);
        expect(eq(read("main/quality"), 0.0)) << "its value pressed again: cleared";
        h.runtime().focus(h.runtime().find("main/quality"));
        h.key(Key::right);
        expect(eq(read("main/quality"), 0.5)) << "a key steps it";
        expect(act(h, "main/fixed", "set_value", Value{}.set("value", 1.0)) != 0 && eq(read("main/fixed"), 4.0)) << "read-only: it only shows";
        expect(h.tree().node("main/fixed")->states.has(State::readonly));
    };

    "otp input: a box for each character — typed one by one, pasted whole (what a box cannot hold dropped), Backspace; completed when full; set by an agent, taken back"_test = [] {
        Headless h(options());
        std::vector<std::string> completed;
        h.mount("main", column({.padding = 12}, {otp_input({.id = "code", .label = "Verification code",
                                                            .on_completed = [&](std::string code) { completed.push_back(code); }})}));
        h.step();
        const auto node = h.tree().node("main/code");
        expect(node && node->role == Role::text_box && node->name == "Verification code");
        h.runtime().focus(h.runtime().find("main/code"));
        h.type("1");
        h.type("2");
        expect(value_of(h, "main/code") == Value("12"));
        h.type("34-56");  // a paste: the dash dropped
        expect(value_of(h, "main/code") == Value("123456") && completed == std::vector<std::string>{"123456"}) << value_of(h, "main/code").json();
        h.type("7");
        expect(value_of(h, "main/code") == Value("123456")) << "full: nothing more";
        h.key(Key::backspace);
        expect(value_of(h, "main/code") == Value("12345"));
        expect(act(h, "main/code", "set_value", Value{}.set("value", "12a")) != 0) << "digits only";
        expect(act(h, "main/code", "set_value", Value{}.set("value", "1234567")) != 0) << "at most six";
        (void)h.advance(seconds(1.1));  // a person typed in it a moment ago: an agent waits (S-14)
        {
            Invocation call;
            call.args = Value{}.set("value", "654321");
            call.actor = {"agent", "test", {}};
            const auto answer = h.tree().act("main/code", "set_value", call);
            h.step();
            expect(answer.exit_code().value_or(-1) == 0 && value_of(h, "main/code") == Value("654321")) << answer.ndjson() << " " << value_of(h, "main/code").json();
        }
        expect(eq(undo(h), 0) && value_of(h, "main/code") == Value("12345")) << "taken back";
    };

    "form field: its label above the control, its help under it, an error in the help's place; required and invalid said"_test = [] {
        Headless h(options());
        auto error = signal(std::string());
        h.mount("main", column({.padding = 12}, {form_field({.id = "email", .error = error, .help = std::string("We never share it."), .label = std::string("Email"),
                                                             .required = true},
                                                            {text_box({.id = "field", .label = "Email", .placeholder = "you@example.org"})})}));
        h.step();
        auto field = h.tree().node("main/email");
        expect(field && field->role == Role::group && field->name == "Email" && field->description == "We never share it." && field->states.has(State::required) &&
               !field->states.has(State::invalid));
        const Rect group = h.runtime().drawn_bounds(h.runtime().find("main/email")), control = h.runtime().drawn_bounds(h.runtime().find("main/email/field"));
        expect(control.y >= group.y + 14 && control.bottom() <= group.bottom() - 12) << "the control between its label and its help";
        error.set("That is not an email address.");
        h.step();
        field = h.tree().node("main/email");
        expect(field && field->states.has(State::invalid) && field->description == "That is not an email address.") << "the error in the help's place";
    };

    "search box: words typed in its field are its words, Enter searches, Escape or its × clears; an agent sets, clears and searches"_test = [] {
        Headless h(options());
        std::vector<std::string> searched;
        h.mount("main", column({.width = 300, .padding = 12}, {search_box({.id = "find", .on_submitted = [&](std::string words) { searched.push_back(words); }})}));
        h.step();
        expect(h.tree().node("main/find") && h.tree().node("main/find")->role == Role::search_box);
        expect(eq(act(h, "main/find", "set_value", Value{}.set("value", "cats")), 0));
        expect(value_of(h, "main/find") == Value("cats") && value_of(h, "main/find/field") == Value("cats")) << "its field shows them";
        expect(eq(act(h, "main/find", "submit"), 0) && searched == std::vector<std::string>{"cats"});
        h.runtime().focus(h.runtime().find("main/find/field"));
        h.key(Key::end);
        h.type(" and dogs");
        expect(value_of(h, "main/find") == Value("cats and dogs")) << value_of(h, "main/find").json();
        h.key(Key::enter);
        expect(searched.size() == 2 && searched.back() == "cats and dogs") << "Enter searches";
        h.key(Key::escape);
        expect(value_of(h, "main/find") == Value("")) << "Escape clears";
        expect(eq(undo(h), 0) && value_of(h, "main/find") == Value("cats and dogs")) << "taken back";
        // Its ×, a button of its own at its right end.
        expect(h.tree().node("main/find/clear") && h.tree().node("main/find/clear")->name == "Clear");
        expect(h.click("main/find/clear"));
        expect(value_of(h, "main/find") == Value("")) << "the × clears";
    };

    "toggle group: each option on or off on its own, a toggle button of its own; pressed, turned by an agent, in the options' order; taken back"_test = [] {
        Headless h(options());
        h.mount("main", column({.padding = 12}, {toggle_group({.id = "style", .label = "Text style", .labels = std::vector<std::string>{"B", "I", "U"},
                                                              .options = std::vector<std::string>{"bold", "italic", "underline"}})}));
        h.step();
        expect(h.tree().node("main/style") && h.tree().node("main/style")->role == Role::toolbar);
        expect(eq(act(h, "main/style", "set_selected", Value{}.set("selected", Value(Value::List{Value("italic"), Value("bold")}))), 0));
        expect(value_of(h, "main/style") == Value(Value::List{Value("bold"), Value("italic")})) << "in the options' order";
        const auto bold = h.tree().node("main/style/bold");
        expect(bold && bold->role == Role::toggle_button && bold->states.has(State::checked) && bold->name == "B");
        expect(eq(act(h, "main/style/underline", "activate"), 0) && h.tree().node("main/style/underline")->states.has(State::checked)) << "an item turned";
        const auto bold_at = h.tree().node("main/style/bold")->bounds;
        expect(fatal(bold_at.has_value()));
        click_at(h, {static_cast<float>(bold_at->x + bold_at->width / 2), static_cast<float>(bold_at->y + bold_at->height / 2)});
        expect(!h.tree().node("main/style/bold")->states.has(State::checked)) << "pressed off";
        expect(eq(undo(h), 0) && h.tree().node("main/style/bold")->states.has(State::checked)) << "taken back";
        expect(act(h, "main/style", "set_selected", Value{}.set("selected", Value(Value::List{Value("strike")}))) != 0) << "not one of its options";
    };

    "file drop: files dragged over it lit, let go given (what it takes; the rest said), given by an agent, the chooser asked for by a press or Enter"_test = [] {
        Headless h(options());
        std::vector<std::vector<std::string>> given;
        int asked = 0;
        h.mount("main", column({.padding = 12, .gap = 12},
                               {file_drop({.id = "zone", .accept = std::vector<std::string>{"png", "jpg"},
                                           .on_dropped = [&](std::vector<std::string> files) { given.push_back(files); }, .on_requested = [&] { ++asked; }}),
                                text({.id = "below", .text = "Elsewhere"})}));
        h.step();
        expect(h.tree().node("main/zone") && h.tree().node("main/zone")->role == Role::button);
        const Rect zone = h.runtime().drawn_bounds(h.runtime().find("main/zone"));
        const Rect below = h.runtime().drawn_bounds(h.runtime().find("main/below"));
        const Point middle{zone.x + zone.width / 2, zone.y + zone.height / 2};
        const auto files = [](FileDropEvent::Kind kind, Point at, std::vector<std::string> paths = {}) {
            FileDropEvent event;
            event.kind = kind;
            event.position = at;
            event.files = std::move(paths);
            return InputEvent{std::move(event)};
        };
        const std::uint64_t rest = hash_of(h.pixels());
        h.step({files(FileDropEvent::Kind::over, middle)});
        expect(hash_of(h.pixels()) != rest) << "lit while files are over it";
        h.step({files(FileDropEvent::Kind::over, {below.x + 4, below.y + 4})});
        expect(hash_of(h.pixels()) == rest) << "and not once they move off it";
        h.step({files(FileDropEvent::Kind::drop, middle, {"/home/ada/cat.PNG", "/home/ada/notes.txt"})});
        expect(given == std::vector<std::vector<std::string>>{{"/home/ada/cat.PNG"}}) << "what it takes";
        expect(h.tree().node("main/zone")->description == "Not taken: notes.txt") << "the rest, said";
        expect(eq(act(h, "main/zone", "drop", Value{}.set("files", Value(Value::List{Value("/b/dog.jpg")}))), 0) && given.size() == 2) << "an agent gives files";
        expect(act(h, "main/zone", "drop", Value{}.set("files", Value(Value::List{Value("/b/a.txt")}))) != 0) << "none it takes: refused";
        expect(h.click("main/zone") && eq(asked, 1)) << "a press asks for the chooser";
        h.runtime().focus(h.runtime().find("main/zone"));
        h.key(Key::enter);
        expect(eq(asked, 2)) << "and Enter";
    };

    "the gallery: ratings, a code, a field with its help and one with its error, a search box and a toggle group — the same pixels on every platform"_test = [] {
        mlibs::text::FontCollection own;
        expect(own.add_defaults().has_value());
        HeadlessOptions made = options({380, 420});
        made.fonts = &own;
        made.scale = 2;
        Headless h(made);
        h.mount("main", column({.width = 380, .padding = 16, .gap = 14},
                               {row({.gap = 16}, {rating({.half = true, .value = 3.5}), rating({.readonly = true, .value = 4.0})}),
                                otp_input({.value = std::string("1234")}),
                                form_field({.help = std::string("We never share it."), .label = std::string("Email"), .required = true},
                                           {text_box({.placeholder = "you@example.org"})}),
                                form_field({.error = std::string("Too short."), .label = std::string("Password")}, {text_box({.password = true})}),
                                search_box({.value = std::string("cats")}),
                                toggle_group({.labels = std::vector<std::string>{"Bold", "Italic", "Underline"},
                                              .options = std::vector<std::string>{"bold", "italic", "underline"}, .selected = std::vector<std::string>{"italic"}})}));
        h.settle();
        const std::uint64_t pinned = hash_of(h.pixels());
        std::println("ui.base inputs {:016x}", pinned);
        expect(h.save_png(scratch_dir("ui-base-inputs") / "inputs@2x.png").has_value());
        expect(pinned == 0xabb2a7eb4ee45138ULL) << std::format("inputs {:016x}", pinned);
    };
}};
