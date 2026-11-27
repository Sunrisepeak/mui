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
        const auto doc = json::Document::parse(text, "piece.music.xdef.json");
        if (!doc) return;
        const auto tempo = doc->root()["music"]["tempo"];
        const auto problem = doc->problem(tempo, "the tempo is a number", "tempo: 96");
        expect(eq(problem.text(), std::string("piece.music.xdef.json:3:14: /music/tempo: the tempo is a number (tempo: 96)")));
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

    "a document held to a schema: each break at its pointer, a choice that fits none said once"_test = [] {
        const auto schema = json::Document::parse(R"({
  "$defs": { "size": { "type": "integer", "minimum": 1 } },
  "type": "object", "required": ["id", "kind"], "additionalProperties": false,
  "properties": {
    "id": { "type": "string", "pattern": "^[a-z]+$" },
    "kind": { "enum": ["text", "button"] },
    "size": { "$ref": "#/$defs/size" },
    "tags": { "type": "array", "items": { "type": "string" }, "maxItems": 2 },
    "value": { "anyOf": [ { "type": "number" }, { "type": "string", "minLength": 1 } ] },
    "mode": { "if": { "const": "fixed" }, "then": { "const": "fixed" }, "else": { "enum": ["auto", "fit"] } }
  },
  "propertyNames": { "pattern": "^[a-z]+$" }
})", "s.json");
        expect(fatal(schema.has_value()));
        const auto fine = json::Document::parse(R"({"id": "save", "kind": "button", "size": 2, "tags": ["a"], "value": "x", "mode": "fit"})", "a.json");
        expect(fatal(fine.has_value()));
        const auto good = json::validate(*schema, *fine);
        expect(good.empty()) << (good.empty() ? "" : good.front().text());
        const auto bad = json::Document::parse(R"({"id": "Save", "kind": "knob", "size": 0, "tags": ["a", "b", 3], "value": "", "mode": "loose", "Extra": 1})", "b.json");
        expect(fatal(bad.has_value()));
        const auto found = json::validate(*schema, *bad);
        std::set<std::string> where;
        std::string all;
        for (const auto& problem : found) {
            where.insert(problem.at);
            all += problem.text() + "\n";
        }
        for (const std::string at : {"/id", "/kind", "/size", "/tags", "/tags/2", "/value", "/mode", "/Extra"})
            expect(where.contains(at)) << at << " not said:\n" << all;
        expect(eq(std::ranges::count_if(found, [](const json::Problem& problem) { return problem.at == "/value"; }), 1)) << all;
        expect(found.front().line == 1 && found.front().file == "b.json") << all;
        const auto unknown = json::Document::parse(R"({"type": "string", "fancyKeyword": 1})", "u.json");
        const auto thing = json::Document::parse(R"("x")", "x.json");
        expect(fatal(unknown.has_value() && thing.has_value()));
        const auto said = json::validate(*unknown, *thing);
        expect(said.size() == 1 && said.front().message.contains("fancyKeyword")) << "a keyword it does not know is said, not passed over";
    };

    "read in place: the module's own scanner takes what nlohmann takes and refuses what it refuses"_test = [] {
        // UTF-8 aside (the scanner takes a text's bytes as they are), the grammar's edges each way.
        const std::vector<std::string_view> texts{
            R"({})", R"([])", R"("")", "0", "-0", "-0.5e+10", "1E5", "1.25", "true", "null", " \n\t[1, {\"a\": [false]}] \r\n",
            "\xEF\xBB\xBF{\"a\": 1}", R"("\" \\ \/ \b \f \n \r \t é 😀")", R"({"a": {"b": [1, 2, {"c": "d"}]}, "e": -1})",
            "", " ", "{", "}", "[1,]", R"({"a": 1,})", "[1 2]", R"({"a" 1})", R"({a: 1})", "01", "1.", ".5", "-", "1e", "+1", "tru",
            "nul", "True", "[1]]", "1 2", R"("\x")", R"("\u12")", R"("\ud800")", R"("\udc00")", R"("\ud800A")", "\"a\nb\"", "\"open",
            R"({"a": [1, 2})", "[\"a\" \"b\"]", "{\"a\":}", "[,1]", "NaN", "Infinity", "/* */ 1"};
        for (const std::string_view text : texts) {
            const bool parsed = json::Document::parse(text, "t.json").has_value();
            const auto scanned = json::Scan::read(text, "t.json");
            expect(eq(scanned.has_value(), parsed)) << "`" << text << "`: nlohmann " << (parsed ? "takes" : "refuses") << " it"
                                                     << (scanned ? std::string() : ", the scanner: " + scanned.error().text());
        }
        const auto broken = json::Scan::read("{\n  \"a\": [1, 2\n}", "bad.json");
        expect(!broken && broken.error().line == 3 && broken.error().column == 1 && broken.error().message.contains("`]`")) << broken.error().text();
    };

    "read in place: strings and arrays of strings by pointer; a problem at its place"_test = [] {
        const std::string text = "{\n  \"$format\": \"runtime@1\",\n  \"runtime\": {\"version\": \"0.1.0\", \"capabilities\": [\"agent\", 3, \"gpu\\u00e9\"], \"n\": 1}\n}\n";
        const auto scan = json::Scan::read(text, "runtime.json");
        expect(fatal(scan.has_value())) << (scan ? "" : scan.error().text());
        expect(scan->text("/$format") == std::optional<std::string>("runtime@1") && scan->text("/runtime/version") == std::optional<std::string>("0.1.0"));
        expect(!scan->text("/runtime/n") && !scan->text("/runtime") && !scan->text("/nowhere") && !scan->text("runtime")) << "a string, or nothing";
        expect(scan->texts("/runtime/capabilities") == std::optional<std::vector<std::string>>(std::vector<std::string>{"agent", "gpu\xC3\xA9"}));
        expect(!scan->texts("/runtime/version") && scan->texts("/runtime/capabilities/0") == std::nullopt);
        const auto missing = scan->problem("/runtime/openmui", "a runtime says the specification it implements");
        expect(eq(missing.text(), std::string("runtime.json:3:14: /runtime/openmui: a runtime says the specification it implements"))) << missing.text();
        const auto document = json::Document::parse(text, "runtime.json");
        expect(fatal(document.has_value()));
        expect(eq(document->problem(document->root()["runtime"]["openmui"], "a runtime says the specification it implements").text(), missing.text()))
            << "placed as a document places it";
    };

    "quoted as nlohmann writes a string"_test = [] {
        for (const std::string_view text : {std::string_view("plain"), std::string_view("a \"b\" \\ c/d"), std::string_view("\b\f\n\r\t\x01\x1f"),
                                            std::string_view("caf\xC3\xA9 \xF0\x9F\x98\x80"), std::string_view("")}) {
            const auto quoted = json::quote(text);
            const auto document = json::Document::parse(R"({"s": )" + quoted + "}", "q.json");
            expect(fatal(document.has_value())) << quoted;
            expect(document->root()["s"].text() == std::optional<std::string>(std::string(text)) && eq(document->root()["s"].dump(), quoted)) << quoted;
        }
    };
}};
