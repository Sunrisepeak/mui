// Linking your agent, shown by Mui (the site plan §9.3, D2): Mui clicked on
// the site — the agent guide opens with a pairing prompt (here, a headless
// host: how to link one instead), and Mui goes to point at it. And the
// permission demonstration (D6): the release workspace's Publish is external,
// so an agent asking to press it waits for the person — Mui holding up the card.
import std;
import omui;
import omui.site;
import mlibs.testing;
import mcpplibs.mtest;

using namespace omui;
using namespace mlibs::testing;
using namespace mcpplibs::mtest;

UTest cases{"site.mui_link", [] {

    "Mui clicked: the agent guide opens with the prompt, and Mui points at it"_test = [] {
        Headless h{HeadlessOptions{.size = {1280, 800}, .scale = 1}};
        auto app = site::app("home", "en");
        h.runtime().set_theme(app.theme);
        app.setup(h.runtime());
        h.mount("main", app.view());
        h.settle(seconds(5));  // its arrival over
        const auto mui = h.tree().node("main/body/view/mui");
        expect(mui.has_value() && mui->bounds.has_value()) << "Mui on the home";
        if (!mui) return;
        const Value now = mui->value;
        const auto& feet = *now.get("feet")->list();
        const auto number = [](const Value& v) { return static_cast<float>(v.type() == Value::Type::integer ? static_cast<double>(v.as_int()) : v.as_double()); };
        const Point on{mui->bounds->x + number(feet[0]), mui->bounds->y + number(feet[1]) - 20};
        expect(h.runtime().hit_test(on) == h.runtime().find("main/body/view/mui")) << "a click there is Mui's";
        h.click(on);
        h.settle(seconds(3));
        const auto prompt = h.tree().node("main/body/guide/prompt");
        expect(prompt.has_value() && !prompt->name.empty()) << "the guide open, the prompt given";
        const Value after = h.tree().node("main/body/view/mui")->value;
        expect(std::string(after.get("target")->as_string()) == "main/body/guide/prompt") << std::string(after.get("target")->as_string());
        (void)h.save_png(scratch_dir("mui-link") / "guide.png");
    };

    "the permission demonstration: the guide's agent records the release, then asks to publish it — Mui holds up the card; the person answers"_test = [] {
        Headless h{HeadlessOptions{.size = {1280, 800}, .scale = 1}};
        auto app = site::app("home", "en");
        h.runtime().set_theme(app.theme);
        app.setup(h.runtime());
        for (auto& verb : app.verbs) (void)h.runtime().verbs().add(verb.desc, verb.fn);
        h.mount("main", app.view());
        h.settle(seconds(3));
        expect(h.click("main/bar/guide_toggle"));
        h.settle();
        expect(h.click("main/body/guide/tasks/task_publish")) << "the task offered";
        const std::string board = "main/body/view/release/page/hero/demo/specimen";
        const auto name_of = [&](const std::string& id) {
            const auto node = h.tree().node(id);
            return node && !node->states.has(State::hidden) ? node->name : std::string();
        };
        std::string asking;
        for (int k = 0; k < 40 && asking.empty(); ++k) {
            h.advance(seconds(0.5));
            if (const auto box = h.tree().node("main/body/guide/asking"); box && !box->states.has(State::hidden))  // the question shown
                asking = h.tree().node("main/body/guide/asking/question")->name;
        }
        expect(name_of(board + "/shipped").starts_with("Shipped")) << name_of(board + "/shipped");
        expect(asking.contains("actions/publish") && asking.contains("external")) << std::format("it asks the person: {}", asking);
        h.advance(seconds(2));
        const Value mui = h.tree().node("main/body/view/mui")->value;
        expect(eq(std::string(mui.get("state")->as_string()), std::string("asking")));
        (void)h.save_png(scratch_dir("mui-permission") / "asks.png");
        expect(h.click("main/body/guide/asking/answer/allow_once")) << "the person allows it, once";
        h.settle(seconds(2));
        expect(name_of(board + "/shipped").starts_with("Published")) << name_of(board + "/shipped");
    };
}};
