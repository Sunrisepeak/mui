import std;
import omui.core;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;

static UTest cases{"core.json", [] {

    "values"_test = [] {
        auto v = omui::parse_json(R"({"a": [1, 2.5, "x", true, null], "b": {"c": -3}, "big": 18446744073709551615})");
        expect(v.has_value());
        expect(eq(v->json(), std::string(R"({"a":[1,2.5,"x",true,null],"b":{"c":-3},"big":18446744073709551616})")));
        expect(eq(v->get("b")->get("c")->as_int(), std::int64_t{-3}));
    };

    "round trip through the canonical form"_test = [] {
        std::string text = R"({"id":"notes/delete","role":"button","bounds":[10,20,80.5,32],"name":"删除"})";
        auto v = omui::parse_json(text);
        expect(v && v->json() == text);
    };

    "errors carry line and column"_test = [] {
        auto v = omui::parse_json("{\n  \"a\": 1,\n  \"b\": ]\n}");
        expect(!v.has_value());
        expect(eq(v.error().line, std::size_t{3}));
        expect(v.error().column >= 7 && v.error().column <= 9) << std::format("column {}", v.error().column);
        expect(!v.error().message.empty());
        expect(!omui::parse_json("").has_value());
        expect(!omui::parse_json("[1, 2] extra").has_value());
    };

    "newline-delimited JSON"_test = [] {
        auto rows = omui::parse_ndjson("{\"kind\":\"data\"}\n\n{\"kind\":\"result\",\"exitCode\":0}\n");
        expect(rows && rows->size() == 2);
        auto bad = omui::parse_ndjson("{}\n{oops}\n");
        expect(!bad && bad.error().line == 2);
    };

    "a pointer's place in the text"_test = [] {
        const std::string_view text = "{\n  \"view\": {\"root\": {\"children\": [\n    {\"type\": \"text\"},\n    {\"type\": \"butto\"}\n  ]}},\n  \"a/b\": 1, \"t~\": [true, {\"\\u00e9\\ud83d\\ude00\": null}]\n}";
        auto at = [&](std::string_view p) { return omui::json_location(text, p); };
        auto root = at("");
        expect(root && root->line == 1 && root->column == 1);
        auto bad = at("/view/root/children/1/type");
        expect(bad && bad->line == 4 && bad->column == 14) << (bad ? bad->text() : "none");
        auto second = at("/view/root/children/1");
        expect(second && second->line == 4 && second->column == 5);
        expect(at("/a~1b") && at("/a~1b")->line == 6) << "~1 is a slash";
        auto escaped = at("/t~0/1/\u00e9\U0001F600");
        expect(escaped && escaped->line == 6) << "keys compare decoded";
        expect(!at("/view/root/children/2")) << "past the end";
        expect(!at("/view/nothing"));
        expect(!at("/a~1b/0")) << "into a number";
        expect(!at("view")) << "a pointer starts with /";
    };
}};
