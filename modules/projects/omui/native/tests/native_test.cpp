// The registry of native modules: a module is defined once and checked;
// calls are positional or by name; every argument is checked against the
// contract's types, with where and what in the message; the catalogue is what
// bindings read.
import std;
import nlohmann.json;
import omui.native;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace native = omui::native;
using native::Json;

namespace {

// A module as the generated define() makes one: a record, two functions.
native::Module shapes() {
    native::Module m;
    m.name = "shapes";
    m.doc = "Areas of shapes.";
    m.types.push_back({"Rect", "A rectangle.", {{"w", "f64", ""}, {"h", "f64", ""}, {"label", "string?", ""}}});
    native::Function area;
    area.name = "area";
    area.doc = "The total area of some rectangles.";
    area.params = {{"rects", "Rect[]", "the rectangles"}, {"scale", "f64?", "a factor (1)"}};
    area.returns = "f64";
    area.call = [](const Json& a) -> native::Result {
        double sum = 0;
        for (const Json& r : a["rects"]) sum += r["w"].get<double>() * r["h"].get<double>();
        return Json(sum * (a["scale"].is_null() ? 1.0 : a["scale"].get<double>()));
    };
    native::Function label;
    label.name = "label";
    label.params = {{"count", "u32", ""}};
    label.returns = "string";
    label.async = true;
    label.call = [](const Json& a) -> native::Result {
        if (a["count"].get<std::uint32_t>() == 13) return std::unexpected(native::Failure{"failed", "unlucky"});
        return Json(std::format("{} shapes", a["count"].get<std::uint32_t>()));
    };
    m.functions = {std::move(area), std::move(label)};
    return m;
}

std::string error_of(const native::Result& r) { return r ? std::string() : r.error().code + ": " + r.error().message; }

}  // namespace

UTest cases{"native", [] {

    "a module is defined once; its names and types are checked"_test = [] {
        expect(native::define(shapes()).has_value());
        auto again = native::define(shapes());
        expect(!again && again.error().find("already defined") != std::string::npos) << (again ? "" : again.error());
        native::Module bad;
        bad.name = "Bad-Name";
        expect(!native::define(bad).has_value()) << "a module's name is snake_case";
        native::Module typo;
        typo.name = "typo";
        native::Function f;
        f.name = "f";
        f.params = {{"x", "Rectangle", ""}};
        f.call = [](const Json&) -> native::Result { return Json(); };
        typo.functions = {f};
        auto r = native::define(typo);
        expect(!r && r.error().find("unknown type `Rectangle`") != std::string::npos) << (r ? "" : r.error());
        expect(native::find("shapes") != nullptr && native::find("typo") == nullptr);
    };

    "calls are positional or by name; optional arguments may be left out"_test = [] {
        const Json rects = Json::parse(R"j([{"w": 2, "h": 3}, {"w": 1, "h": 1, "label": "unit"}])j");
        auto a = native::call("shapes", "area", Json::array({rects}));
        expect(a.has_value() && *a == Json(7.0)) << error_of(a);
        auto b = native::call("shapes", "area", Json{{"rects", rects}, {"scale", 2}});
        expect(b.has_value() && *b == Json(14.0)) << error_of(b);
        auto c = native::call("shapes", "label", Json::array({3}));
        expect(c.has_value() && *c == Json("3 shapes")) << error_of(c);
        expect(native::find("shapes")->functions[1].async) << "async is the contract's";
    };

    "arguments are the contract's: what is wrong, and where"_test = [] {
        auto says = [](const native::Result& r, std::string_view code, std::string_view text) {
            expect(!r && r.error().code == code && r.error().message.find(text) != std::string::npos) << error_of(r);
        };
        says(native::call("shapes", "area", Json::array()), "invalid-argument", "`rects` (Rect[]) is missing");
        says(native::call("shapes", "area", Json::parse(R"j([[{"w": 2}]])j")), "invalid-argument", "`rects[0].h` (f64) is missing");
        says(native::call("shapes", "area", Json::parse(R"j([[{"w": 2, "h": "tall"}]])j")), "invalid-argument",
             "`rects[0].h` is a string, not a number (f64)");
        says(native::call("shapes", "area", Json::parse(R"j([[{"w": 2, "h": 1, "depth": 4}]])j")), "invalid-argument",
             "`rects[0].depth` is not a field of Rect");
        says(native::call("shapes", "area", Json::parse(R"j([[], 1, 2])j")), "invalid-argument", "3 arguments given, 2 taken");
        says(native::call("shapes", "area", Json{{"rect", Json::array()}}), "invalid-argument", "`rect` is not an argument of it");
        says(native::call("shapes", "label", Json::array({-1})), "invalid-argument", "not a non-negative integer (u32)");
        says(native::call("shapes", "label", Json::array({2.5})), "invalid-argument", "not a non-negative integer (u32)");
        says(native::call("shapes", "area", Json("rects")), "invalid-argument", "a list, or an object by name");
        // The message names the signature.
        says(native::call("shapes", "label", Json::array()), "invalid-argument", "shapes.label(count: u32)");
    };

    "what is not there, and an implementation's own failure"_test = [] {
        auto none = native::call("circles", "area", Json::array());
        expect(!none && none.error().code == "not-found") << error_of(none);
        auto fn = native::call("shapes", "volume", Json::array());
        expect(!fn && fn.error().code == "not-found" && fn.error().message.find("it has area, label") != std::string::npos) << error_of(fn);
        auto failed = native::call("shapes", "label", Json::array({13}));
        expect(!failed && failed.error().code == "failed" && failed.error().message.find("unlucky") != std::string::npos) << error_of(failed);
    };

    "the catalogue is what bindings read"_test = [] {
        const Json c = native::catalog();
        const Json& m = c["modules"]["shapes"];
        expect(eq(m["doc"].get<std::string>(), std::string("Areas of shapes.")));
        expect(eq(m["types"]["Rect"]["fields"][2]["type"].get<std::string>(), std::string("string?")));
        const Json& area = m["functions"]["area"];
        expect(eq(area["params"][0]["name"].get<std::string>(), std::string("rects")));
        expect(eq(area["params"][1]["doc"].get<std::string>(), std::string("a factor (1)")));
        expect(eq(area["returns"].get<std::string>(), std::string("f64")));
        expect(m["functions"]["label"]["async"].get<bool>());
    };

    "the conversions the generated code uses"_test = [] {
        const Json j = Json::parse(R"j([[1, 2], [], [3]])j");
        auto v = native::decode(j, std::type_identity<std::vector<std::vector<std::uint32_t>>>{});
        expect(v.size() == 3 && v[0][1] == 2 && v[2][0] == 3);
        expect(native::encode(v) == j);
        std::optional<std::string> none;
        expect(native::encode(none).is_null() && native::encode(std::optional<std::string>("x")) == Json("x"));
        expect(!native::decode(Json(nullptr), std::type_identity<std::optional<double>>{}).has_value());
    };
}};
