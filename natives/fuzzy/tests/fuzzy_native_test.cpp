// openmui.fuzzy as scripts reach it: the generated define() registers the
// contract's functions; a call by position or by name is checked against the
// contract and answers the records the stubs describe.
import std;
import nlohmann.json;
import openmui.native;
import openmui.native.fuzzy;
import openmui.testing;

using namespace omui::testing;
namespace native = omui::native;
using native::Json;

int main(int argc, char** argv) {
    Suite s{"native.fuzzy"};

    s.test("define() registers the contract's module, once", [] {
        auto d = omui::natives::fuzzy::define();
        check(d.has_value(), d ? "" : d.error());
        const native::Module* m = native::find("fuzzy");
        check(m != nullptr);
        if (!m) return;
        check(m->functions.size() == 2 && m->functions[0].name == "rank" && m->functions[0].async, "rank, async, first as written");
        check(m->functions[0].params.size() == 3 && m->functions[0].params[2].type == "u32?");
        check(!omui::natives::fuzzy::define().has_value(), "a second definition is refused");
    });

    s.test("rank: by position or by name, the limit optional", [] {
        const Json files = Json::array({"src/main.cpp", "my_config.toml", "docs/mcfg.md", "README.md"});
        auto all = native::call("fuzzy", "rank", Json::array({"mcfg", files}));
        check(all.has_value() && all->size() == 2, all ? all->dump() : all.error().message);
        if (!all || all->empty()) return;
        const Json& best = (*all)[0];
        check_eq(best["text"].get<std::string>(), std::string("docs/mcfg.md"));
        check_eq(best["index"].get<int>(), 2);
        check(best["segments"].size() == 3 && best["segments"][1] == Json{{"text", "mcfg"}, {"matched", true}}, best["segments"].dump());
        auto one = native::call("fuzzy", "rank", Json{{"query", "mcfg"}, {"candidates", files}, {"limit", 1}});
        check(one.has_value() && one->size() == 1);
    });

    s.test("match: a record, or null", [] {
        auto yes = native::call("fuzzy", "match", Json::array({"fb", "foo_bar"}));
        check(yes.has_value() && (*yes)["positions"] == Json::array({0, 4}), yes ? yes->dump() : yes.error().message);
        auto no = native::call("fuzzy", "match", Json::array({"zz", "foo_bar"}));
        check(no.has_value() && no->is_null());
    });

    s.test("the contract's types are checked before the implementation runs", [] {
        auto bad = native::call("fuzzy", "rank", Json::array({"q", Json::array({"a", 3})}));
        check(!bad && bad.error().code == "invalid-argument" && bad.error().message.find("`candidates[1]` is the number 3, not a string") != std::string::npos,
              bad ? "" : bad.error().message);
    });

    return s.run(argc, argv);
}
