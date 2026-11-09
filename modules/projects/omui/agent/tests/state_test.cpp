// What a person set, carried into another instance by semantic id (W16,
// D-154): a new build of the application takes the text typed, the switch
// turned on, the slider moved, where a list was scrolled to and the focus —
// not a step anyone takes back.
import std;
import omui.agent;
import omui.ui.base;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
using namespace omui;
namespace agent = omui::agent;

namespace {

mlibs::text::FontCollection& fonts() {
    static mlibs::text::FontCollection collection = [] {
        mlibs::text::FontCollection made;
        (void)made.add_defaults();
        return made;
    }();
    return collection;
}

// The settings page; the new build has the slider no more (and the name is a different kind of thing).
struct Settings {
    Runtime rt;
    explicit Settings(bool rebuilt)
        : rt([] {
              RuntimeOptions options;
              options.fonts = &fonts();
              options.size = {400, 300};
              return options;
          }()) {
        std::vector<Element> children{text_box({.id = "name", .label = "Name"}), toggle({.id = "dark", .label = "Dark"}), text_box({.id = "secret", .password = true})};
        if (!rebuilt) children.push_back(slider({.id = "volume", .max = 100.0, .value = 20.0}));
        std::vector<Element> rows;
        for (int index = 0; index < 30; ++index) rows.push_back(text({.id = std::format("row-{}", index), .text = std::format("Row {}", index)}));
        children.push_back(scroll({.id = "list", .height = Dim(60)}, std::move(rows)));
        rt.mount("main", column({.padding = 8, .gap = 8}, std::move(children)));
        (void)rt.frame({seconds(0), {}});
    }
};

std::vector<Value> talk(agent::Protocol& protocol, const Value& request) {
    std::vector<Value> out;
    protocol.request(request.json(), [&](std::string_view line) { out.push_back(parse_json(line).value()); });
    return out;
}

const Value* data_of(const std::vector<Value>& reply) {
    for (const Value& record : reply)
        if (record.get("kind") && record.get("kind")->as_string() == "data") return record.get("data");
    return nullptr;
}

}  // namespace

UTest cases{"state", [] {
    "a new build takes back what the person set: the text, the switch, where a list was scrolled to, the focus; what is gone is said; the password is not kept"_test = [] {
        register_base_components();
        Settings old_build(false);
        semantic::Tree before(old_build.rt);
        Invocation person;
        person.actor = {"person", "tester", {}};
        expect(eq(before.act("main/name", "set_value", Invocation{.args = Value{}.set("value", "Ada"), .actor = {"person", "tester", {}}}).exit_code().value_or(1), 0));
        expect(eq(before.act("main/dark", "toggle", person).exit_code().value_or(1), 0));
        expect(eq(before.act("main/secret", "set_value", Invocation{.args = Value{}.set("value", "hunter2"), .actor = {"person", "tester", {}}}).exit_code().value_or(1), 0));
        expect(eq(before.act("main/volume", "set_value", Invocation{.args = Value{}.set("value", 70), .actor = {"person", "tester", {}}}).exit_code().value_or(1), 0));
        const Envelope scrolled = before.act("main/list", "scroll_to", Invocation{.args = Value{}.set("x", 0).set("y", 120), .actor = {"person", "tester", {}}});
        expect(eq(scrolled.exit_code().value_or(1), 0)) << scrolled.ndjson();
        expect(eq(before.act("main/name", "focus", person).exit_code().value_or(1), 0));
        (void)old_build.rt.frame({seconds(0.1), {}});
        (void)old_build.rt.frame({seconds(1), {}});  // the scroll's glide settled

        agent::Protocol old_protocol(old_build.rt, {});
        const auto asked = talk(old_protocol, Value{}.set("op", "state"));
        const Value* state = data_of(asked);
        expect(state && state->get("focused") && state->get("focused")->as_string() == "main/name") << (state ? state->json() : std::string("none"));
        expect(state && !state->json().contains("hunter2")) << "a password is not kept";

        Settings new_build(true);
        agent::Protocol new_protocol(new_build.rt, {});
        const auto answer = talk(new_protocol, Value{}.set("op", "restore").set("state", state ? *state : Value{}));
        const Value* done = data_of(answer);
        expect(done && done->json().contains("main/name") && done->json().contains("main/dark")) << (done ? done->json() : std::string("none"));
        expect(done && done->get("missing")->json().contains("main/volume")) << "the slider is gone: said";
        semantic::Tree after(new_build.rt);
        expect(after.node("main/name") && after.node("main/name")->value == Value("Ada"));
        expect(after.node("main/dark") && after.node("main/dark")->states.has(State::checked));
        expect(after.node("main/name") && after.node("main/name")->states.has(State::focused)) << "the focus where it was";
        (void)new_build.rt.frame({seconds(0.1), {}});
        const auto list = after.node("main/list");
        expect(list && list->value.get("y") && std::abs(list->value.get("y")->as_double() - 120) < 0.5) << "the list where it was scrolled to: "
                                                                                                         << (list ? list->value.json() : std::string("none"));
        expect(after.node("main/secret") && after.node("main/secret")->value.as_string().empty()) << "the password typed again, not carried";
        const History history = new_build.rt.history(new_build.rt.undo_scope(new_build.rt.root()));
        expect(history.undo.empty()) << "putting back is no one's step";
    };
}};
