// Example 01 runs headless: the agent's view of it, typing and clicking, and a picture.
import std;
import openmui;
import openmui.testing;
import hello;

using namespace omui::testing;

int main(int argc, char** argv) {
    omui::testing::Suite s{"example.hello-im"};

    s.test("an agent finds, types and clicks; the view follows", [] {
        omui::register_base_components();
        hello::State state;
        omui::AppSpec app = hello::app(state);
        omui::HeadlessOptions o;
        o.size = app.size;
        omui::Headless h(o);
        h.mount("main", app.view());
        h.settle();
        auto field = h.tree().find({.role = omui::Role::text_box});
        check(field.size() == 1 && field[0].name == "Name");
        auto button = h.tree().find({.role = omui::Role::button});
        check(button.size() == 1 && button[0].name == "Click me" && button[0].id.ends_with("/click"));
        check(h.click(field[0].id));
        h.type("Ada");
        h.settle();
        check(state.name == "Ada");
        check(!h.tree().find({.name = "Nice to meet you, Ada!"}).empty());
        omui::Invocation agent;
        agent.actor = {"agent", "example", {}};
        check(h.tree().act(button[0].id, "activate", agent).exit_code() == std::optional<int>(0));
        h.settle();
        check(state.clicks == 1);
        auto after = h.tree().node(button[0].id);
        check(after && after->name == "Clicked once", "the label changed; the id did not (S-9)");
        check(h.tree().violations().empty());
        check(h.save_png(scratch_dir("hello-im") / "hello.png").has_value());
    });

    s.test("the command line", [] {
        hello::State state;
        std::array<std::string_view, 5> args{"--headless", "--frames", "3", "--png", "x.png"};
        auto o = omui::RunOptions::parse(args);
        check(o && o->headless && o->frames == 3 && o->png == "x.png");
        std::array<std::string_view, 1> bad{"--frobnicate"};
        check(!omui::RunOptions::parse(bad).has_value());
        const auto png = scratch_dir("hello-im-run") / "run.png";
        omui::RunOptions run;
        run.headless = true;
        run.frames = 5;
        run.png = png;
        check(omui::run(hello::app(state), run) == 0);
        check(std::filesystem::file_size(png) > 1000);
        // The framework's own frame around it (D-75): --window framework.
        std::array<std::string_view, 2> framed{"--window", "framework"};
        auto f = omui::RunOptions::parse(framed);
        check(f && f->window == "framework");
        std::array<std::string_view, 2> wrong{"--window", "fancy"};
        check(!omui::RunOptions::parse(wrong).has_value());
        run.window = "framework";
        run.png = scratch_dir("hello-im-framed") / "framed.png";
        check(omui::run(hello::app(state), run) == 0);
        check(std::filesystem::file_size(run.png) > 1000);
        omui::AppSpec odd = hello::app(state);
        odd.window = "fancy";
        run.window.clear();
        check(omui::run(std::move(odd), run) == 2, "an unknown frame mode is refused");
    });

    s.test("the development switches: overlays, log levels, a profile of the run", [] {
        hello::State state;
        std::array<std::string_view, 7> args{"--debug", "bounds,hud", "--log", "frame=debug,*=warning", "--profile", "run.trace.json", "--headless"};
        auto o = omui::RunOptions::parse(args);
        check(o && o->debug.bounds && o->debug.hud && !o->debug.inspector && o->log == "frame=debug,*=warning" && o->profile == "run.trace.json");
        std::array<std::string_view, 2> bad{"--debug", "sparkles"};
        check(!omui::RunOptions::parse(bad).has_value());
        const auto dir = scratch_dir("hello-im-profile");
        omui::RunOptions run;
        run.headless = true;
        run.frames = 4;
        run.png = dir / "debug.png";
        run.debug.bounds = true;
        run.debug.hud = true;
        run.log = "frame=debug";
        run.profile = dir / "run.trace.json";
        check(omui::run(hello::app(state), run) == 0);
        std::ifstream in(run.profile);
        const std::string json{std::istreambuf_iterator<char>(in), {}};
        check(json.starts_with("{\"displayTimeUnit\"") && json.contains("\"name\":\"layout\"") && json.contains("\"name\":\"render\""), json.substr(0, 200));
        omui::set_log_level(omui::Level::info);
    });

    return s.run(argc, argv);
}
