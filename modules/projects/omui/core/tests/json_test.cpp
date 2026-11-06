import std;
import openmui.core;
import openmui.testing;

using namespace omui::testing;

int main(int argc, char** argv) {
    Suite s{"core.json"};

    s.test("values", [] {
        auto v = omui::parse_json(R"({"a": [1, 2.5, "x", true, null], "b": {"c": -3}, "big": 18446744073709551615})");
        check(v.has_value());
        check_eq(v->json(), std::string(R"({"a":[1,2.5,"x",true,null],"b":{"c":-3},"big":18446744073709551616})"));
        check_eq(v->get("b")->get("c")->as_int(), std::int64_t{-3});
    });

    s.test("round trip through the canonical form", [] {
        std::string text = R"({"id":"notes/delete","role":"button","bounds":[10,20,80.5,32],"name":"删除"})";
        auto v = omui::parse_json(text);
        check(v && v->json() == text);
    });

    s.test("errors carry line and column", [] {
        auto v = omui::parse_json("{\n  \"a\": 1,\n  \"b\": ]\n}");
        check(!v.has_value());
        check_eq(v.error().line, std::size_t{3});
        check(v.error().column >= 7 && v.error().column <= 9, std::format("column {}", v.error().column));
        check(!v.error().message.empty());
        check(!omui::parse_json("").has_value());
        check(!omui::parse_json("[1, 2] extra").has_value());
    });

    s.test("newline-delimited JSON", [] {
        auto rows = omui::parse_ndjson("{\"kind\":\"data\"}\n\n{\"kind\":\"result\",\"exitCode\":0}\n");
        check(rows && rows->size() == 2);
        auto bad = omui::parse_ndjson("{}\n{oops}\n");
        check(!bad && bad.error().line == 2);
    });

    s.test("a pointer's place in the text", [] {
        const std::string_view text = "{\n  \"view\": {\"root\": {\"children\": [\n    {\"type\": \"text\"},\n    {\"type\": \"butto\"}\n  ]}},\n  \"a/b\": 1, \"t~\": [true, {\"\\u00e9\\ud83d\\ude00\": null}]\n}";
        auto at = [&](std::string_view p) { return omui::json_location(text, p); };
        auto root = at("");
        check(root && root->line == 1 && root->column == 1);
        auto bad = at("/view/root/children/1/type");
        check(bad && bad->line == 4 && bad->column == 14, bad ? bad->text() : "none");
        auto second = at("/view/root/children/1");
        check(second && second->line == 4 && second->column == 5);
        check(at("/a~1b") && at("/a~1b")->line == 6, "~1 is a slash");
        auto escaped = at("/t~0/1/\u00e9\U0001F600");
        check(escaped && escaped->line == 6, "keys compare decoded");
        check(!at("/view/root/children/2"), "past the end");
        check(!at("/view/nothing"));
        check(!at("/a~1b/0"), "into a number");
        check(!at("view"), "a pointer starts with /");
    });

    return s.run(argc, argv);
}
