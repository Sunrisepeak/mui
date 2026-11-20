module omui.entry.player;

import std;
import mcpplibs.os;
import mcpplibs.cmdline;
import omui;
import omui.player;
import omui.abi;
import omui.ui.markdown;
import omui.lang.js;
import omui.native;
import omui.native.fuzzy;
import omui.native.store;
import omui.theme;
import omui.theme.default_theme;
import omui.theme.compact;

namespace omui {  // names this file uses unqualified, from their owner
using mlibs::log::warn;
namespace cmdline = mcpplibs::cmdline;
}

namespace omui {

namespace {

// Twins of the application (D-149): the application again, headless and
// unseen, with its own runtime and player — previews, variants, the design's
// film and motion measured in them; what the person sees untouched.
class HeadlessTwin final : public player::Twin {
public:
    HeadlessTwin(const player::Manifest& manifest, bool design)
        : host_(HeadlessOptions{.level = HeadlessLevel::pixels, .size = manifest.size, .app = manifest.id + ".twin"}),
          shown_(host_.runtime(), manifest, {lang::js::typescript(), lang::js::javascript()}, {.design = design}) {
        (void)shown_.start();  // its problems are the application's own, said where it runs
        settle();
    }
    player::Player& player() override { return shown_; }
    Runtime& runtime() override { return host_.runtime(); }
    seconds time() override { return host_.runtime().now(); }
    void step(std::optional<seconds> at) override { (void)host_.step({}, at); }
    // Its scripts run on a thread of their own: waited for by time (a few frames
    // pass before QuickJS has begun), its frames stepped meanwhile.
    void settle() override {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!shown_.idle() && std::chrono::steady_clock::now() < deadline) {
            (void)host_.step();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        host_.settle();
    }
    std::expected<std::vector<std::uint8_t>, ViewFailure> png() override { return host_.png(); }
    std::expected<std::vector<std::uint8_t>, ViewFailure> framed_png(Rect frame, std::string_view fixed) override { return host_.framed_png(frame, fixed); }

private:
    Headless       host_;
    player::Player shown_;
};

player::TwinFn twins(const player::Manifest& manifest) {
    return [manifest](bool design) -> std::expected<std::unique_ptr<player::Twin>, std::string> { return std::make_unique<HeadlessTwin>(manifest, design); };
}


}  // namespace

// omui.config.ts beside the manifest (defineConfig, evaluated as data):
//   agent:  { development?: mode, release?: mode }   automatic, development, dual-user
//   record: { development?: level, release?: level } off, errors, actions, interactions, full
//   designer: whether the designer is linked — omui dev and omui build read it (the player runs what it was built with)
//   generation: { open?: { development?, release? }, surfaces?: { slot: doc | { doc, kind: "view" | "theme" } }, components?, verbs?,
//                 replay?: the design's replay (replay@1, design.history.export), played in place by generate.replay }
struct Config {
    std::optional<std::string> development_agent, release_agent;
    std::optional<RecordLevel> development_record, release_record;
    std::optional<generate::Openness> development_generation, release_generation;
    std::vector<generate::Surface>    surfaces;
    std::vector<std::string>       components, verbs;
    std::filesystem::path          replay;  // from the manifest's directory
};

std::expected<Config, std::string> read_config(const std::filesystem::path& base) {
    Config out;
    auto file = base / "omui.config.ts";
    std::error_code error;
    std::optional<Value> value;
    if (std::filesystem::is_regular_file(file, error)) {
        auto data = lang::ts::evaluate(file);
        if (!data) return std::unexpected(data.error().text());
        if (data->kind != "config") return std::unexpected(std::format("{}: its default export is defineConfig(…), not a {}", utf8(file), data->kind));
        auto parsed = parse_json(data->json);
        if (!parsed) return std::unexpected(std::format("{}: {}", utf8(file), parsed.error().text()));
        value = std::move(*parsed);
    } else if (file = base / "omui.config.json"; std::filesystem::is_regular_file(file, error)) {
        // Its snapshot, where the module is not evaluated (a page: the page's build evaluated it).
        std::ifstream in(file, std::ios::binary);
        auto parsed = parse_json(std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()));
        const Value* config = parsed ? parsed->get("config") : nullptr;
        if (!config || !config->map()) return std::unexpected(std::format("{}: a config's snapshot ({{\"$format\": \"config@1\", \"config\": {{…}}}})", utf8(file)));
        value = *config;
    } else {
        return out;
    }
    const auto text_at = [&](std::string_view group, std::string_view key) -> std::optional<std::string> {
        const Value* g = value->get(group);
        const Value* v = g ? g->get(key) : nullptr;
        return v && v->type() == Value::Type::text ? std::optional(std::string(v->as_string())) : std::nullopt;
    };
    for (const auto key : {"development", "release"}) {
        if (auto mode = text_at("agent", key)) {
            if (!parse_agent_mode(*mode)) return std::unexpected(std::format("{}: agent.{} is automatic, development or dual-user, not `{}`", utf8(file), key, *mode));
            (std::string_view(key) == "development" ? out.development_agent : out.release_agent) = *mode;
        }
        if (auto level = text_at("record", key)) {
            constexpr std::array<std::pair<std::string_view, RecordLevel>, 5> levels{{{"off", RecordLevel::off}, {"errors", RecordLevel::errors},
                                                                                       {"actions", RecordLevel::actions}, {"interactions", RecordLevel::interactions},
                                                                                       {"full", RecordLevel::full}}};
            const auto found = std::ranges::find(levels, *level, &std::pair<std::string_view, RecordLevel>::first);
            if (found == levels.end()) return std::unexpected(std::format("{}: record.{} is off, errors, actions, interactions or full, not `{}`", utf8(file), key, *level));
            (std::string_view(key) == "development" ? out.development_record : out.release_record) = found->second;
        }
    }
    if (const Value* generation = value->get("generation"); generation && generation->map()) {
        if (const Value* open = generation->get("open"); open && open->map())
            for (const auto& [key, level] : *open->map()) {
                const auto openness = generate::parse_openness(level.as_string());
                if (!openness) return std::unexpected(std::format("{}: generation.open.{} is off, data, limited or all, not `{}`", utf8(file), key, level.as_string()));
                (key == "development" ? out.development_generation : out.release_generation) = *openness;
            }
        if (const Value* surfaces = generation->get("surfaces"); surfaces && surfaces->map())
            for (const auto& [slot, said] : *surfaces->map()) {
                // A slot's doc, or { doc, kind }: "theme" is the application's theme (G8: a draft is an overlay of tokens).
                if (said.type() == Value::Type::text) {
                    out.surfaces.push_back({slot, std::string(said.as_string())});
                    continue;
                }
                const Value* doc = said.get("doc");
                const Value* kind = said.get("kind");
                const std::string_view kind_name = kind ? kind->as_string() : std::string_view("view");
                if (!said.map() || (kind_name != "view" && kind_name != "theme"))
                    return std::unexpected(std::format("{}: generation.surfaces.{} is what it is for, or {{ doc, kind: \"view\" | \"theme\" }}", utf8(file), slot));
                out.surfaces.push_back({slot, doc ? std::string(doc->as_string()) : std::string(),
                                        kind_name == "theme" ? generate::Surface::Kind::theme : generate::Surface::Kind::view});
            }
        if (const Value* replay = generation->get("replay")) {
            if (replay->type() != Value::Type::text || replay->as_string().empty())
                return std::unexpected(std::format("{}: generation.replay is the design's replay, a path from here (\"design/main.replay.xdef.json\")", utf8(file)));
            const std::string_view path = replay->as_string();
            out.replay = std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(path.data()), path.size()));
        }
        for (const auto& [list, into] : {std::pair{"components", &out.components}, std::pair{"verbs", &out.verbs}})
            if (const Value* names = generation->get(list); names && names->list())
                for (const Value& name : *names->list()) into->push_back(std::string(name.as_string()));
    }
    return out;
}

// A theme surface's drafts (G8): an overlay of tokens over the application's theme — its manifest's layers, else
// light — read and composed by omui.theme, every mistake at its pointer in the overlay.
std::function<std::expected<Theme, std::string>(std::string_view)> customized(std::vector<std::string> layers) {
    if (layers.empty()) layers = {"light"};
    return [layers = std::move(layers)](std::string_view overlay) -> std::expected<Theme, std::string> {
        auto base = theme::library().compose(layers);
        if (!base) return std::unexpected(base.error().text());
        if (overlay.empty()) return std::move(*base);
        auto layer = theme::read_overlay(overlay, "overlay");
        if (!layer) {
            std::string problems;
            for (const theme::Diagnostic& problem : layer.error()) problems += std::format("{}{}: {}{}", problems.empty() ? "" : "\n  ", problem.at, problem.message,
                                                                                            problem.hint.empty() ? "" : std::format(" ({})", problem.hint));
            return std::unexpected(problems);
        }
        auto composed = theme::overlaid(std::move(*base), *layer);
        if (!composed) return std::unexpected(composed.error().text());
        return std::move(*composed);
    };
}

// Code that makes a generated view document (generation's `limited` and `all`): a TypeScript
// data module, `export default defineView({…})`, evaluated as data modules are — no I/O, no
// real time, no randomness, a bounded heap and time — and imports by path only inside an empty
// directory.
generate::EvaluateFn generated_views(const std::filesystem::path& base) {
    return [scratch = base / ".omui" / "generated"](std::string_view language, std::string_view code) -> std::expected<std::string, std::string> {
        if (language != "ts" && language != "js") return std::unexpected(std::format("generated code is TypeScript (ts) here, not {}", language));
        std::error_code error;
        std::filesystem::create_directories(scratch, error);
        auto data = lang::ts::evaluate_source(code, utf8(scratch / "draft.view.xdef.ts"),
                                              {.root = scratch, .memory_bytes = 16 * 1024 * 1024, .time = std::chrono::milliseconds(500)});
        if (!data) return std::unexpected(data.error().text());
        if (data->kind != "view") return std::unexpected(std::format("the code's default export is defineView(…), not a {}", data->kind));
        return std::move(data->json);
    };
}

// Its package's version: the omui tool ships this program (omui-player) and runs an application of the same version
// through it (`--version` says it; E2).
constexpr std::string_view player_version = "0.1.0";

namespace {

// Where this player runs from: its package's directory — a self-contained package's launcher says it
// (MCPP_BUNDLE_DIR); a vendored package (as xlings installs a release) is the directory above the program's bin/
// that holds the package's mcpp-package.json — else its program's.
std::filesystem::path runtime_directory() {
    if (const auto bundle = mcpplibs::os::env::get("MCPP_BUNDLE_DIR"); bundle && !bundle->empty()) return mcpplibs::os::fs::from_utf8(*bundle);
    if (const auto image = mcpplibs::os::env::image()) {
        const std::filesystem::path directory = image->parent_path();
        std::error_code error;
        if (directory.filename() == "bin" && std::filesystem::exists(directory.parent_path() / "mcpp-package.json", error)) return directory.parent_path();
        return directory;
    }
    return std::filesystem::current_path();
}

}  // namespace

int player_main(int argc, char** argv) {
    // The player's switches beside the application's (RunOptions), on one
    // command line, parsed once (mcpplibs.cmdline, D-56).
    using mcpplibs::cmdline::App, mcpplibs::cmdline::Arg, mcpplibs::cmdline::Option;
    auto line = App("player")
        .version(player_version)
        .description("Runs a script-first openmui application from its mcpp.toml")
        .option(Option("manifest").takes_value().value_name("FILE").help("the application's mcpp.toml, or its directory (default: ./mcpp.toml)"))
        .option(Option("dev").help("development mode: the application is its own designer (D-149)"))
        .option(Option("types").takes_value().value_name("DIR").help("write the TypeScript declarations of what it registers, its API (api.json) and tsc's configuration into DIR, then end (omui check)"))
        .option(Option("design-film").takes_value().value_name("DIR").help("the design's history as a film: frames and a video description into DIR, then end (omui design film; needs the designer)"))
        .option(Option("film-branch").takes_value().value_name("NAME").help("design-film: whose commits (the one designed on)"))
        .option(Option("film-fps").value_type<int>().help("design-film: frames a second (30)"))
        .option(Option("film-seconds").value_type<double>().help("design-film: each step's time on screen (1.5)"))
        .option(Option("runtime").help("say what this player is as a runtime (E3b): its version, the openmui specification it implements, the capabilities it carries, where it runs from; then end"));
    auto command = command_line(std::move(line), argc, argv);
    if (!command) return command.error();
    const cmdline::ParsedArgs& parsed = command->args;
    RunOptions& options = command->options;
    if (parsed.is_flag_set("runtime")) {
        // What a launcher matches an application's declaration against (runtime@1): the specification's major and
        // minor version (its scripts' interface), the capabilities this build carries.
        Value::List carried;
        for (const std::string& capability : capabilities()) carried.emplace_back(capability);
        std::println("{}", Value(Value::Map{})
                               .set("$format", "runtime@1")
                               .set("runtime", Value{}
                                                   .set("version", std::string(player_version))
                                                   .set("openmui", std::format("{}.{}", abi::OMUI_SPEC_VERSION_MAJOR, abi::OMUI_SPEC_VERSION_MINOR))
                                                   .set("capabilities", Value(std::move(carried)))
                                                   .set("directory", mcpplibs::os::fs::utf8(runtime_directory())))
                               .json());
        return 0;
    }
    std::filesystem::path manifest = mcpplibs::os::fs::from_utf8(parsed.value("manifest").value_or("mcpp.toml"));
    if (std::filesystem::is_directory(manifest)) manifest /= "mcpp.toml";
    const bool design = parsed.is_flag_set("dev");
    std::optional<std::filesystem::path> types;
    if (auto dir = parsed.value("types")) types = mcpplibs::os::fs::from_utf8(*dir);
    auto m = read_manifest(manifest);
    if (!m) {
        std::println(std::cerr, "{}", m.error());
        return 2;
    }
    // How far agents may go, and how much the recorder keeps (D-144, D-148,
    // D-155): in development mode an agent has every control a developer's
    // own tools have — whether to ask the person first is its own judgement
    // and the person's instructions to it — and everything is recorded; in a
    // release, what the build profile says, and errors. omui.config.ts
    // (defineConfig) sets either; a switch the person gives wins over both.
    auto config = read_config(m->base);
    if (!config) {
        std::println(std::cerr, "{}", config.error());
        return 2;
    }
    const std::string mode = design ? config->development_agent.value_or("development") : config->release_agent.value_or("");
    if (!mode.empty() && !options.agent_mode) options.agent_mode = parse_agent_mode(mode);
    const auto record = design ? config->development_record.value_or(RecordLevel::full) : config->release_record;
    register_markdown_component();
    // The official native modules (an application's own are registered by
    // its main, before this): `import { fuzzy } from "openmui:native/fuzzy"`.
    if (!native::find("fuzzy"))
        if (auto r = natives::fuzzy::define(); !r) warn("the native module fuzzy: {}", r.error());
    // The official themes (light, dark, compact, reduced_motion); an
    // application's own come from its manifest (`themes`).
    for (auto add : {themes::add_default, themes::add_compact})
        if (auto r = add(); !r) warn("a theme package: {}", r.error()[0].text());
    if (types) {
        // What the application registered, declared for tsc (W1 step 5): its
        // components (its component documents too, so it starts, headless),
        // native modules and the define functions, a file a line. The
        // official components first, as run() registers them.
        register_base_components();
        register_window_components();
        Headless headless(HeadlessOptions{.size = m->size});
        player::Player declaring(headless.runtime(), *m, {lang::js::typescript(), lang::js::javascript()});
        if (auto r = declaring.start(); !r) std::print(std::cerr, "{}", r.error());
        auto files = declaring.declare(*types);
        if (!files) {
            std::println(std::cerr, "{}", files.error());
            return 1;
        }
        // Beside them, the same API as data (omui.agent:api): `omui describe --api`, llms.txt and
        // `omui check --portable` read it.
        const auto api = *types / "api.json";
        if (auto written = mcpplibs::os::fs::write_atomic(api, agent::api_of(headless.runtime()).json()); !written) {
            std::println(std::cerr, "cannot write {}: {}", utf8(api), written.error().message());
            return 1;
        }
        files->push_back(api);
        for (const auto& file : *files) std::println("{}", utf8(file));
        return 0;
    }
    if (auto film = parsed.value("design-film")) {
        // The design's history as a film (design.film, the designer's): the application started
        // headless in development mode, the verb asked, its answer printed.
        register_base_components();
        register_window_components();
        Headless headless(HeadlessOptions{.size = m->size, .app = m->id});
        player::Player designing(headless.runtime(), *m, {lang::js::typescript(), lang::js::javascript()}, {.design = true, .twin = twins(*m)});
        if (auto r = designing.start(); !r) {
            std::print(std::cerr, "{}", r.error());
            if (!headless.runtime().verbs().find("design.film")) return 2;  // no designer linked: nothing to film with
        }
        for (int frame = 0; frame < 600 && !designing.idle(); ++frame) (void)headless.step();
        Value args = Value{}.set("out", *film);
        if (auto branch = parsed.value("film-branch")) args.set("branch", *branch);
        if (auto fps = parsed.value_as<int>("film-fps")) args.set("fps", *fps);
        if (auto each = parsed.value_as<double>("film-seconds")) args.set("seconds", *each);
        Invocation call;
        call.args = std::move(args);
        call.actor = {"person", "developer", {}};
        const Envelope answer = headless.runtime().verbs().invoke("design.film", call);
        std::print("{}", answer.ndjson());
        return answer.exit_code().value_or(1) == 0 ? 0 : 1;
    }
    std::optional<player::Player> p;
    AppSpec app;
    app.id = m->id;
    app.title = m->title;
    app.size = m->size;
    app.effects = m->effects;
    // The application's effects written in TypeScript, evaluated as they load and reload (D-139).
    app.effect_data = [](const std::filesystem::path& file) -> std::expected<std::string, std::string> {
        auto data = lang::ts::evaluate(file);
        if (!data) return std::unexpected(data.error().text());
        return data->snapshot();
    };
    app.setup = [&](Runtime& rt) {
        if (record) rt.set_record_level(*record);
        p.emplace(rt, *m, std::vector<player::Language>{lang::js::typescript(), lang::js::javascript()},
                  player::PlayerOptions{.design = design, .twin = design ? twins(*m) : player::TwinFn{}});
        if (auto r = p->start(); !r) std::print(std::cerr, "{}", r.error());
        // A saved file shows within a frame in development mode (W2: save to screen ≤ 100 ms at p95; a
        // stat of the app's files a frame costs microseconds), within a tenth of a second otherwise.
        p->watch(design ? seconds(1.0 / 60) : seconds(0.1));
    };
    // Interface generated while it runs (D-144): where omui.config.ts lets it in, as open as it says
    // — everything in development, nothing in a release unless said.
    if (!config->surfaces.empty() || !config->replay.empty()) {
        const generate::Openness open = design ? config->development_generation.value_or(generate::Openness::all)
                                            : config->release_generation.value_or(generate::Openness::off);
        app.generation = generate::GenerationOptions{.openness = open,
                                                  .surfaces = config->surfaces,
                                                  .components = config->components,
                                                  .verbs = config->verbs,
                                                  .evaluate = generated_views(m->base),
                                                  .theme = customized(m->theme),
                                                  .directory = m->base / ".omui" / "generation"};
        // The design shown in place (G8): the replay it ships, each step's texts run as an edit runs; at the end, or
        // stopped, its own scripts again.
        if (!config->replay.empty()) {
            const std::filesystem::path file = m->base / config->replay;
            std::ifstream in(file, std::ios::binary);
            if (!in) std::println(std::cerr, "{}: generation.replay names no file here (design.history.export writes one)", utf8(file));
            app.generation->replay.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            app.generation->replay_file = utf8(config->replay);
            app.generation->show = [&p](const std::map<std::string, std::string>& texts) -> std::expected<void, std::string> {
                if (!p) return std::unexpected(std::string("the application's scripts are not running"));
                if (auto shown = p->show(texts); !shown) return std::unexpected(shown.error().message);
                return {};
            };
            app.generation->restore = [&p] {
                if (p) (void)p->discard_edits();
            };
        }
        // On the Web a page's files do not outlive it: the versions are kept in the page's storage (the host's store,
        // localStorage), so the customization is there again when the page is opened again.
        if constexpr (mcpplibs::os::is_emscripten) {
            const std::string prefix = std::format("omui.generation/{}/", m->id);
            app.generation->read_kept = [prefix](std::string_view name) { return omui::store().get(prefix + std::string(name)); };
            app.generation->write_kept = [prefix](std::string_view name, std::string_view text) { return omui::store().set(prefix + std::string(name), text); };
        }
    }
    app.ready = [&] { return p && p->idle(); };
    app.teardown = [&](Runtime&) { p.reset(); };  // the player goes before its runtime
    return run(std::move(app), options);
}

}  // namespace omui
