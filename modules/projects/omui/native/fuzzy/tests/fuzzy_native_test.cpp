// omui::fuzzy as scripts reach it: the generated define() registers the
// contract's functions; a call by position or by name is checked against the
// contract and answers the records the stubs describe.
import std;
import nlohmann.json;
import omui.native;
import omui.native.fuzzy;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace native = omui::native;
using native::Json;

UTest cases{"native.fuzzy", [] {

    "define() registers the contract's module, once"_test = [] {
        auto d = omui::natives::fuzzy::define();
        expect(d.has_value()) << (d ? "" : d.error());
        const native::Module* m = native::find("fuzzy");
        expect(m != nullptr);
        if (!m) return;
        expect(m->functions.size() == 2 && m->functions[0].name == "rank" && m->functions[0].async) << "rank, async, first as written";
        expect(m->functions[0].params.size() == 3 && m->functions[0].params[2].type == "u32?");
        expect(!omui::natives::fuzzy::define().has_value()) << "a second definition is refused";
    };

    "rank: by position or by name, the limit optional"_test = [] {
        const Json files = Json::array({"src/main.cpp", "my_config.toml", "docs/mcfg.md", "README.md"});
        auto all = native::call("fuzzy", "rank", Json::array({"mcfg", files}));
        expect(all.has_value() && all->size() == 2) << (all ? all->dump() : all.error().message);
        if (!all || all->empty()) return;
        const Json& best = (*all)[0];
        expect(eq(best["text"].get<std::string>(), std::string("docs/mcfg.md")));
        expect(eq(best["index"].get<int>(), 2));
        expect(best["segments"].size() == 3 && best["segments"][1] == Json{{"text", "mcfg"}, {"matched", true}}) << best["segments"].dump();
        auto one = native::call("fuzzy", "rank", Json{{"query", "mcfg"}, {"candidates", files}, {"limit", 1}});
        expect(one.has_value() && one->size() == 1);
    };

    "match: a record, or null"_test = [] {
        auto yes = native::call("fuzzy", "match", Json::array({"fb", "foo_bar"}));
        expect(yes.has_value() && (*yes)["positions"] == Json::array({0, 4})) << (yes ? yes->dump() : yes.error().message);
        auto no = native::call("fuzzy", "match", Json::array({"zz", "foo_bar"}));
        expect(no.has_value() && no->is_null());
    };

    "the contract's types are checked before the implementation runs"_test = [] {
        auto bad = native::call("fuzzy", "rank", Json::array({"q", Json::array({"a", 3})}));
        expect(!bad && bad.error().code == "invalid-argument" && bad.error().message.find("`candidates[1]` is the number 3, not a string") != std::string::npos) << (bad ? "" : bad.error().message);
    };
}};
