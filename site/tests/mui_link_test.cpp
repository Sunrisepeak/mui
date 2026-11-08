// Linking your agent, shown by Mui (the site plan §9.3, D2): Mui clicked on
// the site — the agent guide opens with a pairing prompt (here, a headless
// host: how to link one instead), and Mui goes to point at it.
import std;
import omui;
import omui.site;
import mlibs.testing;

using namespace omui;
using namespace mlibs::testing;

int main(int argc, char** argv) {
    Suite suite("site.mui_link");

    suite.test("Mui clicked: the agent guide opens with the prompt, and Mui points at it", [] {
        Headless h{HeadlessOptions{.size = {1280, 800}, .scale = 1}};
        auto app = site::app("home", "en");
        h.runtime().set_theme(app.theme);
        app.setup(h.runtime());
        h.mount("main", app.view());
        h.settle(seconds(5));  // its arrival over
        const auto mui = h.tree().node("main/body/view/mui");
        check(mui.has_value() && mui->bounds.has_value(), "Mui on the home");
        if (!mui) return;
        const Value now = mui->value;
        const auto& feet = *now.get("feet")->list();
        const auto number = [](const Value& v) { return static_cast<float>(v.type() == Value::Type::integer ? static_cast<double>(v.as_int()) : v.as_double()); };
        const Point on{mui->bounds->x + number(feet[0]), mui->bounds->y + number(feet[1]) - 20};
        check(h.runtime().hit_test(on) == h.runtime().find("main/body/view/mui"), "a click there is Mui's");
        h.click(on);
        h.settle(seconds(3));
        const auto prompt = h.tree().node("main/body/guide/prompt");
        check(prompt.has_value() && !prompt->name.empty(), "the guide open, the prompt given");
        const Value after = h.tree().node("main/body/view/mui")->value;
        check(std::string(after.get("target")->as_string()) == "main/body/guide/prompt", std::string(after.get("target")->as_string()));
        (void)h.save_png(scratch_dir("mui-link") / "guide.png");
    });

    return suite.run(argc, argv);
}
