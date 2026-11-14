// Live Design, played whole (W14's signature scene, D-149, D-151): an agent
// designs a subscription settings page while it runs — the TypeScript it
// writes, nodes added where they belong, a title set — every step a commit.
// It says what the person expects, the renewal details springing open among
// it; the person tries the switch; the agent reads the facts back (the spring
// springs too far), fixes it, keeps the person's demonstration as a test, goes
// back through the history and out on a branch, films the design and writes it
// to the file. Each edit within a frame (p95 ≤ 16 ms). The application starts
// as `omui new` leaves one.
import std;
import mcpplibs.mtest;
import omui;
import omui.player;
import omui.entry.player;
import omui.designer;
import omui.lang.js;
import omui.lang.ts;

using namespace mcpplibs::mtest;
using namespace omui;

namespace {

std::filesystem::path here() { return std::filesystem::path(std::source_location::current().file_name()).parent_path().parent_path(); }

std::string file_text(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::string name_of(Headless& h, std::string_view id) {
    auto node = h.tree().node(id);
    return node ? node->name : std::string("<none>");
}

// The application again, headless and unseen, as `omui dev`'s entry makes one.
class TypeScriptTwin final : public player::Twin {
public:
    TypeScriptTwin(const player::Manifest& manifest, bool design)
        : host_(HeadlessOptions{.level = HeadlessLevel::pixels, .size = manifest.size}),
          shown_(host_.runtime(), manifest, {lang::js::typescript()}, {.design = design}) {
        (void)shown_.start();
        settle();
    }
    player::Player& player() override { return shown_; }
    Runtime& runtime() override { return host_.runtime(); }
    seconds time() override { return host_.runtime().now(); }
    void step(std::optional<seconds> at) override { (void)host_.step({}, at); }
    void settle() override {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!shown_.idle() && std::chrono::steady_clock::now() < deadline) {
            (void)host_.step();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        host_.settle();
    }
    std::expected<std::vector<std::uint8_t>, std::string> png() override { return host_.png(); }
    std::expected<std::vector<std::uint8_t>, std::string> framed_png(Rect frame, std::string_view fixed) override {
        return host_.framed_png(frame, fixed);
    }

private:
    Headless       host_;
    player::Player shown_;
};

// The page as the agent first writes it: what it is for, the plans.
constexpr std::string_view first_page = R"ts(// Subscription settings: the plan, how it is paid, whether it renews.
import { signal, select, column, row, text, button, toggle, radio, box, mount } from "openmui";

const plan = signal("pro");
const yearly = signal(false);
const renew = signal(false);

mount("main", column({ padding: 20, gap: 12 }, [
  text({ id: "title", text: "Subscription", style: "title" }),
  row({ id: "plans", gap: 10 }, [
    radio({ id: "basic", value: "basic", group: plan, label: "Basic · $4" }),
    radio({ id: "pro", value: "pro", group: plan, label: "Pro · $9" }),
    radio({ id: "team", value: "team", group: plan, label: "Team · $19" }),
  ]),
]));
)ts";

}  // namespace

UTest cases{"example.live-design", [] {
    register_base_components();

    "an agent designs the page live; the person tries it; the facts, a fix, a test, the history and a film"_test = [] {
        // The application as `omui new` leaves it, in a scratch copy (the scenario and the film are written beside it).
        const auto dir = std::filesystem::temp_directory_path() / "omui-example-live-design";
        std::error_code error;
        std::filesystem::remove_all(dir, error);
        std::filesystem::create_directories(dir / "app", error);
        std::filesystem::copy_file(here() / "mcpp.toml", dir / "mcpp.toml", error);
        std::filesystem::copy_file(here() / "app" / "main.ts", dir / "app" / "main.ts", error);
        auto manifest = read_manifest(dir / "mcpp.toml");
        expect(manifest.has_value()) << (manifest ? "" : manifest.error());
        if (!manifest) return;

        Headless h(HeadlessOptions{.level = HeadlessLevel::pixels, .size = manifest->size, .app = manifest->id});
        player::Player app(h.runtime(), *manifest, {lang::js::typescript()},
                           {.design = true, .twin = [&](bool design) -> std::expected<std::unique_ptr<player::Twin>, std::string> {
                                return std::make_unique<TypeScriptTwin>(*manifest, design);
                            }});
        expect(app.start().has_value());
        const auto settle = [&] {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (!app.idle() && std::chrono::steady_clock::now() < deadline) {
                (void)h.step();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            h.settle();
        };
        settle();
        expect(eq(name_of(h, "main/count"), std::string("Clicked 0 times"))) << "as omui new leaves it";

        std::vector<double> took;  // each edit's time, in ms
        const auto invoke = [&](std::string_view verb, Value args = Value(Value::Map{})) {
            Invocation call;
            call.args = std::move(args);
            call.actor = {"agent", "designer", {}};
            const Envelope answer = h.runtime().verbs().invoke(verb, call);
            settle();
            Value data = answer.data_value() ? *answer.data_value() : Value{answer.ndjson()};
            if (const Value* ms = data.get("took_ms"); ms && ms->is_number()) took.push_back(ms->as_double());
            return std::pair{answer.exit_code().value_or(-1), std::move(data)};
        };
        const auto done = [&](std::string_view verb, Value args = Value(Value::Map{})) {
            const auto [code, data] = invoke(verb, std::move(args));
            expect(eq(code, 0)) << verb << ": " << data.json();
            return data;
        };

        // The agent designs it: the page in TypeScript, then each part where it belongs, then the title.
        (void)done("design.begin", Value{}.set("intent", "a subscription settings page"));
        (void)done("design.edit", Value{}.set("path", "app/main.ts").set("text", first_page));
        expect(eq(name_of(h, "main/title"), std::string("Subscription")));
        for (const std::string_view part :
             {R"(toggle({ id: "yearly", checked: yearly, label: "Pay yearly: two months free" }))",
              R"(toggle({ id: "renew", checked: renew, label: "Renew automatically" }))",
              R"(box({ id: "details", height: select(renew, 72, 0), background: "#e0f2fe", radius: 10, animate: { height: { spring: { stiffness: 260, damping: 9 } } } }))",
              R"(button({ id: "save", text: "Save", variant: "primary" }))"})
            (void)done("design.insert", Value{}.set("parent", "main").set("code", part));
        (void)done("design.set", Value{}.set("node", "main/title").set("props", Value{}.set("text", "Your subscription")));
        expect(eq(name_of(h, "main/title"), std::string("Your subscription")));
        expect(h.tree().node("main/save").has_value() && h.tree().node("main/details").has_value());
        const std::string written = file_text(dir / "app" / "main.ts");
        expect(written.contains("Clicked {} times")) << "the file untouched until it is committed";

        // What the person expects, in their words: the details open briskly and spring only a little.
        (void)done("design.expect",
                   Value{}
                       .set("say", "turning on renewal opens the details briskly, springing at most 10%")
                       .set("motion", Value{}
                                          .set("act", Value{}.set("node", "main/renew").set("action", "toggle"))
                                          .set("node", "main/details")
                                          .set("measure", "height")
                                          .set("within_ms", Value(Value::List{Value(150), Value(1200)}))
                                          .set("overshoot", 0.1)));
        (void)done("design.expect", Value{}.set("say", "there is a Save button").set("query", Value{}.set("id", "main/save")));

        // "Let me try it": the person turns renewal on and off.
        (void)done("design.handover");
        Invocation person;
        person.actor = {"person", "tester", {}};
        for (int press = 0; press < 2; ++press) {
            expect(eq(semantic::Tree(h.runtime()).act("main/renew", "toggle", person).exit_code().value_or(-1), 0));
            settle();
        }
        // "Have a look": the facts against the words.
        const Value facts = done("design.check");
        const auto& results = *facts.get("results")->list();
        expect(results.size() == 2 && !results[0].get("holds")->as_bool() && results[1].get("holds")->as_bool()) << facts.json();
        const std::string why(results[0].get("why")->as_string());
        expect(why.contains("height 0 → 72 px") && why.contains("more than 10%")) << why;
        const double sprang = results[0].get("measured")->get("overshoot")->as_double();
        expect(sprang > 0.2) << sprang;

        // The fix the person agrees to: more damping. The facts again.
        (void)done("design.set", Value{}.set("node", "main/details").set("props", Value{}.set("animate", Value{}.set("height", Value{}.set("spring", Value{}.set("stiffness", 260).set("damping", 26))))));
        const Value again = done("design.check");
        expect(again.get("held")->as_int() == 2) << again.json();

        // The demonstration as a test, played again on a fresh start.
        (void)done("design.save_scenario", Value{}.set("name", "renewal"));
        expect(std::filesystem::exists(dir / "tests" / "renewal.scenario.omui.json"));
        const Value replayed = done("design.replay", Value{}.set("name", "renewal"));
        expect(replayed.get("held")->as_int() == 2) << replayed.json();

        // Back through the history and out on a branch: a tighter layout tried from two steps back, then back again.
        expect(eq(invoke("design.timeline", Value{}.set("at", "HEAD~2")).first, 0));
        expect(eq(name_of(h, "main/title"), std::string("Subscription"))) << "two steps back: the title not yet set";
        (void)done("design.history.branch", Value{}.set("name", "compact").set("at", "HEAD~2"));
        (void)done("design.history.switch", Value{}.set("to", "compact"));
        (void)done("design.set", Value{}.set("node", "main").set("props", Value{}.set("gap", 6)));
        (void)done("design.history.switch", Value{}.set("to", "main"));
        expect(eq(name_of(h, "main/title"), std::string("Your subscription")));
        const Value history = done("design.history");
        expect(history.get("branches")->get("compact") != nullptr && !history.get("uncommitted")->as_bool()) << history.json();

        // The design as a film: each step as it was presented, captioned.
        const Value film = done("design.film", Value{}.set("fps", 12).set("seconds", 0.5));
        expect(film.get("steps")->as_int() >= 8 && film.get("frames")->as_int() == film.get("steps")->as_int() * 6) << film.json();

        // Written to the file at last: the TypeScript is the one source, and it still compiles.
        (void)done("design.commit");
        const std::string final_text = file_text(dir / "app" / "main.ts");
        expect(final_text.contains("Your subscription") && final_text.contains("\"damping\":26") && final_text.contains("id: \"save\"")) << final_text;
        auto compiled = lang::ts::compile(dir / "app" / "main.ts");
        expect(compiled.has_value()) << (compiled ? "" : compiled.error());

        // Each edit within a frame.
        expect(took.size() >= 7) << took.size();
        std::ranges::sort(took);
        const double p95 = took[std::min(took.size() - 1, static_cast<std::size_t>(std::ceil(took.size() * 0.95)) - 1)];
        std::println("live design: {} edits, p95 {:.2f} ms, overshoot {:.0f}% then fixed", took.size(), p95, sprang * 100);
        expect(p95 <= 16.0) << p95;
    };
}};
