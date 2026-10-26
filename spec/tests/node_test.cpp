import std;
import openmui.spec;
import openmui.testing;

using namespace omui::testing;
namespace spec = omui::spec;

namespace {

spec::SemanticNode delete_button() {
    spec::SemanticNode n;
    n.id = "notes/delete";
    n.role = spec::Role::button;
    n.name = "Delete";
    n.states.set(spec::State::focusable);
    n.actions.push_back({.name = "activate", .effect = spec::Effect::irreversible, .verb = "note.delete", .answer = "yes"});
    n.bounds = spec::Rect{10, 20, 80, 32};
    return n;
}

}  // namespace

int main(int argc, char** argv) {
    Suite s{"spec.node"};

    s.test("clauses S-1 and S-6", [] {
        auto n = delete_button();
        check(spec::check(n).empty());
        n.actions[0].answer.clear();
        auto v = spec::check(n);
        check(v.size() == 1 && v[0].clause == "S-1");
        n = delete_button();
        n.name.clear();
        v = spec::check(n);
        check(v.size() == 1 && v[0].clause == "S-6");
        n.role = spec::Role::text;
        check(spec::check(n).empty(), "static text needs no name");
        n.id = "bad id";
        check(spec::check(n)[0].clause == "S-8");
    });

    s.test("wire form round-trips", [] {
        auto n = delete_button();
        n.value = spec::Value(42.5);
        n.origin = spec::Origin::user_content;
        n.children = {"notes/delete/icon"};
        n.states.set(spec::State::animating);
        auto back = spec::node_from_value(spec::to_value(n));
        check(back.has_value() && *back == n);
    });

    s.test("wire form is the documented JSON", [] {
        auto json = spec::to_value(delete_button()).json();
        check_eq(json, std::string(R"({"id":"notes/delete","role":"button","name":"Delete","state":["focusable"],)"
                                   R"("actions":[{"name":"activate","effect":"irreversible","verb":"note.delete","answer":"yes"}],)"
                                   R"("bounds":[10,20,80,32]})"));
    });

    s.test("an unknown effect is read as irreversible", [] {
        auto v = spec::to_value(delete_button());
        (*v.get("actions")->list())[0];
        spec::Value patched = v;
        auto& actions = *patched.map();
        for (auto& [k, val] : actions)
            if (k == "actions") (*val.list())[0].set("effect", "teleport");
        auto n = spec::node_from_value(patched);
        check(n && n->actions[0].effect == spec::Effect::irreversible);
    });

    s.test("deltas", [] {
        spec::SemanticDelta d;
        d.revision = 7;
        check(d.empty());
        d.removed.push_back("a");
        check_eq(spec::to_value(d).json(), std::string(R"({"revision":7,"added":[],"removed":["a"],"updated":[]})"));
    });

    s.test("values", [] {
        spec::Value v;
        v.set("b", true).set("x", 1);
        v.set("n", spec::Value::List{1, 2.5, "s", nullptr});
        v.set("b", false);
        check_eq(v.json(), std::string(R"({"b":false,"x":1,"n":[1,2.5,"s",null]})"));
        check(v.get("missing") == nullptr);
        check_eq(spec::Value(1e300 * 1e300).json(), std::string("null"));
        check_eq(spec::Value("a\"b\n").json(), std::string(R"("a\"b\n")"));
        check_eq(spec::Value(3).as_double(), 3.0);
        check_eq(spec::Value(3.9).as_int(), std::int64_t{3});
        check(spec::Value("x").as_int(-1) == -1);
    });

    return s.run(argc, argv);
}
