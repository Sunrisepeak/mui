// C++26 reflection for openmui: a form from a struct's members (controls by
// type, ids and labels from names, annotations), bound both ways; a struct
// as openmui's values and back, with where a value is wrong.
import std;
import openmui.core;
import openmui.view;
import openmui.ui.base;
import openmui.host.headless;
import openmui.reflect;
import openmui.testing;

using namespace omui::testing;
using namespace omui;

namespace {

enum class Quality { low, medium, high, very_high };

struct Audio {
    [[=reflect::range{0, 11, 1}]] int volume = 5;
    bool muted = false;
};

struct Settings {
    std::string                       display_name = "Ada";
    [[=reflect::range{0.5, 2, 0.25}]] double zoom = 1;
    [[=reflect::label{"Dark appearance"}]] bool dark = false;
    Quality                           quality = Quality::medium;
    Audio                             audio;
    [[=reflect::hidden{}]] std::string token = "secret";
};

struct Row {
    std::string                key;
    std::vector<std::string>   tags;
    std::optional<double>      score;
    Quality                    quality = Quality::low;
};

std::int64_t act(Headless& h, std::string_view node, std::string_view action, Value args = {}) {
    Invocation call;
    call.actor = {"agent", "test", {}};
    call.args = std::move(args);
    return h.tree().act(node, action, call).exit_code().value_or(-1);
}

}  // namespace

int main(int argc, char** argv) {
    Suite s{"reflect"};
    register_base_components();

    s.test("a form: a control per member by its type, ids and labels from names, annotations", [] {
        Headless h(HeadlessOptions{.size = {480, 640}});
        auto settings = signal(Settings{});
        h.mount("main", reflect::form(settings));
        h.settle();
        auto role = [&](std::string_view id) {
            auto n = h.tree().node(id);
            return n ? std::string(spec::name(n->role)) : std::string("<none>");
        };
        check_eq(role("main/display_name"), std::string("text_box"));
        check_eq(role("main/zoom"), std::string("slider"));
        check_eq(role("main/dark"), std::string("switch"));
        check_eq(role("main/quality"), std::string("combo_box"));
        check_eq(role("main/audio/volume"), std::string("slider"));
        check_eq(h.tree().node("main/display_name")->name, std::string("Display name"));
        check_eq(h.tree().node("main/dark")->name, std::string("Dark appearance"));
        check(!h.tree().node("main/token"), "hidden");
        const Value* max = h.tree().node("main/audio/volume")->value.get("max");
        check(max && max->as_double() == 11, "the range annotation");
    });

    s.test("bound both ways: a control writes the struct; the struct shows in the controls", [] {
        Headless h(HeadlessOptions{.size = {480, 640}});
        auto settings = signal(Settings{});
        h.mount("main", reflect::form(settings));
        h.settle();
        check_eq(act(h, "main/display_name", "set_value", Value{}.set("value", "Grace")), std::int64_t{0});
        check_eq(act(h, "main/zoom", "set_value", Value{}.set("value", 1.6)), std::int64_t{0});
        check_eq(act(h, "main/dark", "toggle"), std::int64_t{0});
        check_eq(act(h, "main/quality", "set_value", Value{}.set("value", "very_high")), std::int64_t{0});
        check_eq(act(h, "main/audio/volume", "set_value", Value{}.set("value", 9)), std::int64_t{0});
        h.settle();
        const Settings& got = settings.peek();
        check(got.display_name == "Grace" && got.zoom == 1.5 && got.dark && got.quality == Quality::very_high && got.audio.volume == 9,
              std::format("{} {} {} {}", got.display_name, got.zoom, got.dark, got.audio.volume));
        Settings next = got;
        next.audio.muted = true;
        next.display_name = "Lin";
        settings.set(next);
        h.settle();
        check(h.tree().node("main/audio/muted")->states.has(State::checked), "the struct shows");
        check_eq(h.tree().node("main/display_name")->value.as_string(), std::string_view("Lin"));
    });

    s.test("a struct as openmui's values and back; what is wrong, and where", [] {
        const Row r{"r1", {"a", "b"}, 0.5, Quality::high};
        const Value v = reflect::to_value(r);
        check_eq(v.json(), std::string(R"j({"key":"r1","tags":["a","b"],"score":0.5,"quality":"high"})j"));
        auto back = reflect::from_value<Row>(v);
        check(back && back->key == "r1" && back->tags.size() == 2 && back->score == 0.5 && back->quality == Quality::high);
        auto partial = reflect::from_value<Row>(Value{}.set("key", "r2"));
        check(partial && partial->key == "r2" && !partial->score && partial->quality == Quality::low, "what is missing keeps its default");
        auto bad = reflect::from_value<Row>(Value{}.set("key", "r3").set("tags", Value(Value::List{Value("a"), Value(3)})));
        check(!bad && bad.error() == "`value.tags[1]` is not a string", bad ? "" : bad.error());
        auto quality = reflect::from_value<Row>(Value{}.set("quality", "extreme"));
        check(!quality && quality.error().find("is not one of [\"low\",\"medium\",\"high\",\"very_high\"]") != std::string::npos);
        check_eq(reflect::fields<Row>().json(),
                 std::string(R"j([{"name":"key","type":"string"},{"name":"tags","type":"list"},{"name":"score","type":"optional"},{"name":"quality","type":["low","medium","high","very_high"]}])j"));
        check_eq(reflect::human("display_name"), std::string("Display name"));
        check_eq(reflect::human("maxLength"), std::string("Max length"));
    });

    return s.run(argc, argv);
}
