// omui.config.ts's openness, end to end (AG7; D-144): an application as it ships — ship_headless, what the released
// program runs without a window — its configuration saying how far interface may be generated while it runs (off,
// data, limited, all), and a user's agent asking through the agent protocol: what each level lets in, and what it
// refuses, saying why. Example 13 (a plans page with an `extras` place) under each level in turn.
import std;
import mcpplibs.mtest;
import mlibs.testing;
import omui.host.headless;
import omui.player;
import omui.entry.player;

using namespace mcpplibs::mtest;
using namespace omui;

namespace {

std::filesystem::path repository() {
    return std::filesystem::path(std::source_location::current().file_name()).parent_path().parent_path().parent_path().parent_path().parent_path().parent_path().parent_path();
}

std::string file_text(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// Example 13 as it ships, its omui.config.ts saying only how open a release is: `level`, its `extras` place, and the
// components a `limited` draft may use.
player::Manifest example_at(std::string_view level) {
    const std::filesystem::path example = repository() / "examples" / "13-live-design";
    const std::filesystem::path dir = mlibs::testing::scratch_dir(std::format("openness-{}", level));
    std::filesystem::create_directories(dir / "app");
    std::filesystem::copy_file(example / "mcpp.toml", dir / "mcpp.toml", std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(example / "app" / "main.ts", dir / "app" / "main.ts", std::filesystem::copy_options::overwrite_existing);
    std::ofstream(dir / "omui.config.ts", std::ios::binary) << std::format(R"(import {{ defineConfig }} from "omui/define";

export default defineConfig({{
  generation: {{
    open: {{ release: "{}" }},
    surfaces: {{ extras: "offers and notes a user's agent adds under the plans" }},
    components: ["text", "column"],
  }},
}});
)",
                                                                           level);
    auto manifest = read_manifest(dir / "mcpp.toml");
    expect(fatal(manifest.has_value())) << (manifest ? std::string() : manifest.error());
    return *manifest;
}

struct Answer {
    int         exit = -1;
    Value       data;
    std::string error;
};

// A user's agent, through the agent protocol (what `omui connect` and the endpoint carry).
struct Agent {
    agent::Protocol protocol;
    explicit Agent(Runtime& runtime) : protocol(runtime, {}) {}
    Answer invoke(std::string_view verb, Value args = Value(Value::Map{})) {
        Answer answer;
        protocol.request(Value{}.set("op", "invoke").set("verb", verb).set("args", std::move(args)).json(), [&](std::string_view line) {
            const auto record = parse_json(line);
            if (!record) return;
            const Value* kind = record->get("kind");
            if (kind && kind->as_string() == "data") answer.data = *record->get("data");
            if (kind && kind->as_string() == "error") answer.error += std::string(line);
            if (kind && kind->as_string() == "result") answer.exit = static_cast<int>(record->get("exitCode")->as_int());
        });
        return answer;
    }
};

// A card of words: as a view document (data), and as the code that makes it (TypeScript).
Value card(std::string_view words, std::string_view type = "text") {
    return parse_json(std::format(R"j({{"omui": "0.1", "view": {{"root": {{"type": "{}", "id": "offer", "props": {{"text": "{}"}}}}}}}})j", type, words)).value();
}
std::string card_code(std::string_view words) {
    return std::format(R"(import {{ defineView }} from "openmui/define";
export default defineView({{ omui: "0.1", view: {{ root: {{ type: "text", id: "offer", props: {{ text: "{}" }} }} }} }});
)",
                       words);
}

bool shows(Shipped& app, std::string_view name) {
    app.settle();
    return !app.host().tree().find({.name = std::string(name)}).empty();
}

}  // namespace

static UTest cases{"entry.openness", [] {
    register_official();

    "off — a release's default: nothing generated; a draft refused, saying how open the application is"_test = [] {
        auto app = ship_headless(example_at("off"));
        expect(fatal(app.has_value())) << (app ? std::string() : app.error());
        Agent agent((*app)->host().runtime());
        const Answer surfaces = agent.invoke("generate.surfaces");
        expect(eq(surfaces.exit, 0) && surfaces.data.get("openness") && surfaces.data.get("openness")->as_string() == "off") << surfaces.data.json();
        const Answer refused = agent.invoke("generate.draft", Value{}.set("surface", "extras").set("document", card("Pro, 9 a month")));
        expect(eq(refused.exit, 1) && refused.error.contains("off")) << refused.error;
        expect(!shows(**app, "Pro, 9 a month"));
    };

    "data: a view document drafted, committed and shown; code refused — data only"_test = [] {
        auto app = ship_headless(example_at("data"));
        expect(fatal(app.has_value())) << (app ? std::string() : app.error());
        Agent agent((*app)->host().runtime());
        const Answer drafted = agent.invoke("generate.draft", Value{}.set("surface", "extras").set("document", card("Pro, 9 a month")));
        expect(fatal(eq(drafted.exit, 0))) << drafted.error;
        expect(eq(agent.invoke("generate.commit", Value{}.set("draft", drafted.data.get("draft")->as_string())).exit, 0));
        expect(shows(**app, "Pro, 9 a month")) << "the version shown in its place";
        const Answer coded = agent.invoke("generate.draft", Value{}.set("surface", "extras").set("language", "ts").set("code", card_code("From code")));
        expect(eq(coded.exit, 1) && coded.error.contains("data only")) << coded.error;
    };

    "limited: code makes the document in the host's sandbox; only the components listed"_test = [] {
        auto app = ship_headless(example_at("limited"));
        expect(fatal(app.has_value())) << (app ? std::string() : app.error());
        Agent agent((*app)->host().runtime());
        const Answer made = agent.invoke("generate.draft", Value{}.set("surface", "extras").set("language", "ts").set("code", card_code("From code")));
        expect(fatal(eq(made.exit, 0))) << made.error;
        expect(eq(agent.invoke("generate.commit", Value{}.set("draft", made.data.get("draft")->as_string())).exit, 0));
        expect(shows(**app, "From code")) << "made by code, shown";
        const Answer pressed = agent.invoke("generate.draft", Value{}.set("surface", "extras").set("document", card("Upgrade", "button")));
        expect(eq(pressed.exit, 1) && pressed.error.contains("`button` is not among the components")) << pressed.error;
    };

    "all: any component, and code"_test = [] {
        auto app = ship_headless(example_at("all"));
        expect(fatal(app.has_value())) << (app ? std::string() : app.error());
        Agent agent((*app)->host().runtime());
        const Answer pressed = agent.invoke("generate.draft", Value{}.set("surface", "extras").set("document", card("Upgrade", "button")));
        expect(eq(pressed.exit, 0)) << pressed.error;
        const Answer made = agent.invoke("generate.draft", Value{}.set("surface", "extras").set("language", "ts").set("code", card_code("All from code")));
        expect(fatal(eq(made.exit, 0))) << made.error;
        expect(eq(agent.invoke("generate.commit", Value{}.set("draft", made.data.get("draft")->as_string())).exit, 0));
        expect(shows(**app, "All from code"));
    };
}};
