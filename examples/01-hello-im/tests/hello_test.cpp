// Example 01 runs headless: the agent's view of it, typing and clicking, and a picture.
import std;
import openmui;
import mlibs.testing;
import hello;

using namespace mlibs::testing;

int main(int argc, char** argv) {
    mlibs::testing::Suite s{"example.hello-im"};

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

    s.test("--agent-guide: what an agent needs to operate it, beside its page — agent.md, its JSON twin, the page's link to them (once)", [] {
        hello::State state;
        const auto dir = scratch_dir("hello-im-agent-guide");
        std::array<std::string_view, 2> args{"--agent-guide", "out"};
        auto o = omui::RunOptions::parse(args);
        check(o && o->headless && o->agent_guide == "out", "a headless run that writes it");
        check(omui::RunOptions::takes_value("--agent-guide"));
        std::ofstream(dir / "index.html") << "<html><head><title>hello</title></head><body></body></html>";
        omui::RunOptions run;
        run.headless = true;
        run.agent_guide = dir;
        check(omui::run(hello::app(state), run) == 0);
        check(omui::run(hello::app(state), run) == 0, "again: the same files, the link once");
        auto read = [](const std::filesystem::path& f) {
            std::ifstream in(f, std::ios::binary);
            return std::string{std::istreambuf_iterator<char>(in), {}};
        };
        const std::string md = read(dir / "agent.md"), page = read(dir / "index.html");
        check(md.starts_with("# Hello, openmui — for agents") && md.contains("omui connect --code") && md.contains("/omui/v1/hello"), md.substr(0, 200));
        const auto json = omui::parse_json(read(dir / ".well-known" / "openmui-agent.json"));
        check(json && json->get("app") && json->get("app")->get("id")->as_string() == "org.openmui.examples.hello-im" &&
                  json->get("protocol")->as_string() == "openmui.tree@0.1",
              "for a program");
        const std::string link = R"(<link rel="openmui-agent" href="agent.md">)";
        check(page.contains(link) && page.find(link) == page.rfind(link) && page.find(link) < page.find("</head>"), page);
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
        mlibs::log::set_log_level(mlibs::log::Level::info);
    });

    return s.run(argc, argv);
}
