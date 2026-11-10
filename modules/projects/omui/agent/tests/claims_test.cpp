// Claims (D-160): two agents designing one settings page at once, each
// holding the part it works on — as small as the work: a node, a property of
// it, or the node and all inside it. Another's change there is refused with
// who holds it, what for and for how long; disjoint parts go on together; a
// lease ends unless renewed; a person is never held and, taking over,
// releases every claim.
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

struct Page {
    Runtime rt;
    double  time = 0;
    Page()
        : rt([] {
              RuntimeOptions options;
              options.fonts = &fonts();
              options.size = {400, 300};
              return options;
          }()) {
        rt.mount("main", column({.padding = 8, .gap = 8},
                                {text({.id = "title", .text = "Settings"}),
                                 column({.id = "notifications"}, {toggle({.id = "push", .label = "Push"}), slider({.id = "volume", .max = 100.0, .value = 20.0})}),
                                 column({.id = "appearance"}, {toggle({.id = "dark", .label = "Dark"})})}));
        frame(0);
    }
    void frame(double at) {
        time = at;
        (void)rt.frame({seconds(at), {}});
    }
};

struct Reply {
    std::int64_t code = -1;
    Value        data, error;
};

Reply talk(agent::Protocol& protocol, std::string_view actor, Value request) {
    Reply out;
    request.set("actor", actor);
    protocol.request(request.json(), [&](std::string_view line) {
        const Value record = parse_json(line).value();
        const std::string_view kind = record.get("kind") ? record.get("kind")->as_string() : std::string_view{};
        if (kind == "data") out.data = *record.get("data");
        if (kind == "error") out.error = record;
        if (kind == "result") out.code = record.get("exitCode")->as_int();
    });
    return out;
}

Value claim(std::string_view node, bool subtree = false, std::string_view intent = {}) {
    return Value{}.set("op", "claim").set("node", node).set("subtree", subtree).set("intent", intent);
}
Value toggle_of(std::string_view node) { return Value{}.set("op", "act").set("node", node).set("action", "toggle"); }

}  // namespace

UTest cases{"claims", [] {
    register_base_components();

    "two agents on one page: each holds its part; another's change there is refused with who, what for and how long; disjoint parts go on together"_test = [] {
        Page page;
        agent::Protocol first(page.rt, {}), second(page.rt, {});
        const std::string_view a = "agent:a", b = "agent:b";
        const Reply held = talk(first, a, claim("main/notifications", true, "redesigning the notifications"));
        expect(eq(held.code, std::int64_t{0}) && held.data.get("actor")->as_string().starts_with("agent:a")) << held.data.json();
        // B, inside what A holds: refused, told why.
        const Reply refused = talk(second, b, toggle_of("main/notifications/push"));
        expect(refused.code != 0 && refused.error.json().contains("conflict") && refused.error.json().contains("agent:a") &&
               refused.error.json().contains("redesigning the notifications") && refused.error.json().contains("retry_after"))
            << refused.error.json();
        expect(talk(second, b, claim("main/notifications/push")).code != 0) << "nor can it claim inside it";
        expect(talk(second, b, claim("main", true)).code != 0) << "nor the whole page around it";
        // An ancestor's own node (not inside it): stands beside A's.
        expect(eq(talk(second, b, claim("main")).code, std::int64_t{0})) << "the page itself, not what is inside";
        expect(eq(talk(second, b, Value{}.set("op", "release").set("node", "main")).code, std::int64_t{0}));
        // Disjoint parts: both go on.
        expect(eq(talk(second, b, claim("main/appearance", true, "trying a dark look")).code, std::int64_t{0}));
        expect(eq(talk(second, b, toggle_of("main/appearance/dark")).code, std::int64_t{0})) << "B in its part";
        expect(eq(talk(first, a, toggle_of("main/notifications/push")).code, std::int64_t{0})) << "A in its part";
        // Every claim, read by anyone.
        const Reply listed = talk(first, a, Value{}.set("op", "claims"));
        expect(eq(listed.data.get("claims")->list()->size(), std::size_t{2})) << listed.data.json();
        // A person is never held.
        Invocation person;
        person.actor = {"person", "pat", {}};
        semantic::Tree tree(page.rt);
        expect(eq(tree.act("main/notifications/push", "toggle", person).exit_code().value_or(1), 0)) << "the person changes what A holds";
    };

    "as small as the work: two properties of one node held by two agents; the node itself meets both"_test = [] {
        Page page;
        agent::Protocol first(page.rt, {}), second(page.rt, {});
        expect(eq(talk(first, "agent:a", claim("main/title").set("property", "text")).code, std::int64_t{0}));
        expect(eq(talk(second, "agent:b", claim("main/title").set("property", "color")).code, std::int64_t{0})) << "another property";
        expect(talk(second, "agent:b", claim("main/title").set("property", "text")).code != 0) << "the same property";
        expect(talk(second, "agent:b", claim("main/title")).code != 0) << "the whole node";
    };

    "a lease ends unless renewed; let go, another takes it; the person taking over releases every claim"_test = [] {
        Page page;
        agent::Protocol first(page.rt, {}), second(page.rt, {});
        expect(eq(talk(first, "agent:a", claim("main/appearance", true).set("seconds", 2)).code, std::int64_t{0}));
        page.frame(1.5);
        expect(eq(talk(first, "agent:a", claim("main/appearance", true).set("seconds", 2)).code, std::int64_t{0})) << "renewed at 1.5 s";
        page.frame(3);
        expect(talk(second, "agent:b", toggle_of("main/appearance/dark")).code != 0) << "still held at 3 s (renewed to 3.5)";
        page.frame(4);
        expect(eq(talk(second, "agent:b", toggle_of("main/appearance/dark")).code, std::int64_t{0})) << "free once the lease ended";
        // Let go: another takes it.
        expect(eq(talk(first, "agent:a", claim("main/notifications", true)).code, std::int64_t{0}));
        expect(talk(second, "agent:b", claim("main/notifications")).code != 0);
        expect(eq(talk(first, "agent:a", Value{}.set("op", "release").set("node", "main/notifications")).code, std::int64_t{0}));
        expect(eq(talk(second, "agent:b", claim("main/notifications")).code, std::int64_t{0})) << "taken after it was let go";
        // The person takes over: every claim released.
        page.rt.take_over();
        expect(page.rt.claims().empty()) << "taking over releases them all";
        page.rt.hand_back();
    };
}};
