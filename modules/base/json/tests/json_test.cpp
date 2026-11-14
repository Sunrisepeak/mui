// JSON documents with their places: nodes and pointers, problems at line and
// column (an absent member at the object that should hold it), malformed text
// placed, members in order.
import std;
import mlibs.json;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace json = mlibs::json;

UTest cases{"json", [] {
    "nodes: values by type, members in the text's order, pointers escaped"_test = [] {
        const auto doc = json::Document::parse(R"({"b": 1, "a": [true, "x", 2.5], "c/d": {"~": null}})", "x.json");
        expect(doc.has_value()) << (doc ? "" : doc.error().text());
        if (!doc) return;
        const auto root = doc->root();
        expect(root["b"].integer() == std::optional<std::int64_t>(1) && root["b"].number() == std::optional<double>(1));
        expect(root["a"][0].boolean() == std::optional<bool>(true) && root["a"][1].text() == std::optional<std::string>("x"));
        expect(!root["a"][2].integer() && root["a"][2].number() == std::optional<double>(2.5)) << "2.5 is not an integer";
        expect(!root["a"][3] && root["a"][3].pointer() == "/a/3");
        expect(eq(root["c/d"]["~"].pointer(), std::string("/c~1d/~0")) && root["c/d"]["~"].is_null());
        std::vector<std::string> keys;
        for (const auto& [key, _] : root.members()) keys.push_back(key);
        expect(keys == std::vector<std::string>{"b", "a", "c/d"}) << "the text's order";
        keys.clear();
        for (const auto& [key, _] : root.members_by_key()) keys.push_back(key);
        expect(keys == std::vector<std::string>{"a", "b", "c/d"}) << "by key";
        expect(eq(root["a"].size(), std::size_t{3}) && root["a"].items()[1].pointer() == "/a/1");
    };

    "a value by its pointer, from any node: members, elements, escapes; nowhere is absent"_test = [] {
        auto parsed = mlibs::json::Document::parse(R"({"playbacks":[{"id":7,"a/b":{"~":true}}],"n":1})", "pointers");
        expect(fatal(parsed.has_value()));
        const auto root = parsed->root();
        expect(root.at("/playbacks/0/id").integer() == 7 && root.at("").dump() == root.dump());
        expect(root.at("/playbacks/0/a~1b/~0").boolean() == true) << "~1 is a slash, ~0 a tilde";
        expect(root["playbacks"].at("/0/id").integer() == 7 && root["playbacks"].at("/0/id").pointer() == "/playbacks/0/id") << "from a node, and it says where it is";
        expect(!root.at("/playbacks/1") && !root.at("/playbacks/x") && !root.at("playbacks") && !root.at("/n/deeper"));
    };

    "problems at their line and column; an absent member at its object"_test = [] {
        const std::string text = "{\n  \"music\": {\n    \"tempo\": \"fast\"\n  }\n}\n";
        const auto doc = json::Document::parse(text, "piece.music.omui.json");
        if (!doc) return;
        const auto tempo = doc->root()["music"]["tempo"];
        const auto problem = doc->problem(tempo, "the tempo is a number", "tempo: 96");
        expect(eq(problem.text(), std::string("piece.music.omui.json:3:14: /music/tempo: the tempo is a number (tempo: 96)")));
        const auto missing = doc->problem(doc->root()["music"]["key"], "no key");
        expect(missing.line == 2 && missing.column == 12 && missing.at == "/music/key") << missing.text();
        expect(json::locate(text, "/music/tempo").has_value() && !json::locate(text, "/nope").has_value());
    };

    "malformed text: one problem where it is"_test = [] {
        const auto doc = json::Document::parse("{\n  \"a\": [1, 2\n}", "bad.json");
        expect(!doc.has_value());
        if (doc) return;
        expect(doc.error().line == 3 && doc.error().file == "bad.json" && !doc.error().message.empty()) << doc.error().text();
    };
}};
