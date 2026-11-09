module omui.entry.player;

import std;
import tomlplusplus;
import mcpplibs.os;
import mcpplibs.cmdline;
import omui;
import omui.player;
import omui.ui.markdown;
import omui.lang.js;
import omui.native;
import omui.native.fuzzy;
import omui.theme;
import omui.theme.default_theme;
import omui.theme.compact;

namespace omui {  // names this file uses unqualified, from their owner
using mlibs::log::warn;
namespace cmdline = mcpplibs::cmdline;
}

namespace omui {

namespace {

// `ui/*.view.omui.json`: the files of a directory whose names match (`*` in
// the last component only), sorted; a path without `*` as it is.
std::vector<std::filesystem::path> expand(const std::filesystem::path& base, std::string_view pattern) {
    std::vector<std::filesystem::path> out;
    const std::filesystem::path p = std::filesystem::path(std::u8string(pattern.begin(), pattern.end()));
    const std::string name = utf8(p.filename());
    const auto star = name.find('*');
    if (star == std::string::npos) return {p};
    const std::string head = name.substr(0, star), tail = name.substr(star + 1);
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(base / p.parent_path(), ec)) {
        const std::string n = utf8(e.path().filename());
        if (n.size() >= head.size() + tail.size() && n.starts_with(head) && n.ends_with(tail)) out.push_back(p.parent_path() / e.path().filename());
    }
    std::ranges::sort(out);
    return out;
}

}  // namespace

std::expected<player::Manifest, std::string> read_manifest(const std::filesystem::path& file) {
    toml::table root;
    try {
        root = toml::parse_file(utf8(file));
    } catch (const toml::parse_error& e) {
        return std::unexpected(std::format("{}:{}:{}: {}", utf8(file), e.source().begin.line, e.source().begin.column, e.description()));
    }
    player::Manifest m;
    m.base = file.parent_path();
    const toml::table* meta = root["package"]["metadata"]["openmui"].as_table();
    if (!meta) return std::unexpected(std::format("{}: no [package.metadata.openmui]", utf8(file)));
    const toml::table& t = *meta;
    if (auto id = t["id"].value<std::string>()) m.id = *id;
    if (auto title = t["title"].value<std::string>()) m.title = *title;
    else if (auto name = root["package"]["name"].value<std::string>()) m.title = *name;
    if (auto size = t["size"].value<std::string>()) {  // "480x320"
        const std::string_view v = *size;
        const auto x = v.find('x');
        float w = 0, h = 0;
        if (x != std::string_view::npos && std::from_chars(v.data(), v.data() + x, w).ec == std::errc{} &&
            std::from_chars(v.data() + x + 1, v.data() + v.size(), h).ec == std::errc{} && w > 0 && h > 0)
            m.size = {w, h};
        else
            return std::unexpected(std::format("{}: size is \"<width>x<height>\", not \"{}\"", utf8(file), v));
    }
    auto paths = [&](std::string_view key, std::vector<std::filesystem::path>& out) {
        if (const toml::array* a = t[key].as_array())
            for (const auto& v : *a)
                if (auto s = v.value<std::string>())
                    for (auto& p : expand(m.base, *s)) out.push_back(std::move(p));
    };
    paths("components", m.components);
    paths("views", m.views);
    paths("themes", m.themes);
    // effects = "fx": the application's own effects, a directory.
    if (auto e = t["effects"].value<std::string>()) m.effects = m.base / std::filesystem::path(std::u8string(e->begin(), e->end()));
    // theme = ["dark", "compact"]: the composition to start with.
    if (const toml::array* a = t["theme"].as_array())
        for (const auto& v : *a)
            if (auto s = v.value<std::string>()) m.theme.push_back(*s);
    // [package.metadata.openmui.scripts]: name = "logic/main.ts", or
    // name = { lang = "ts", entry = "logic/main.ts" }; run in the names' order.
    if (const toml::table* scripts = t["scripts"].as_table())
        for (const auto& [name, v] : *scripts) {
            std::optional<std::string> lang, entry = v.value<std::string>();
            if (const toml::table* x = v.as_table()) {
                lang = (*x)["lang"].value<std::string>();
                entry = (*x)["entry"].value<std::string>();
            }
            if (!entry) return std::unexpected(std::format("{}: the script `{}` names its `entry`", utf8(file), name.str()));
            m.scripts.push_back({lang.value_or(""), std::filesystem::path(std::u8string(entry->begin(), entry->end()))});
        }
    return m;
}

// omui.config.ts beside the manifest (defineConfig, evaluated as data):
//   agent:  { development?: mode, release?: mode }   automatic, development, dual-user
//   record: { development?: level, release?: level } off, errors, actions, interactions, full
struct Config {
    std::optional<std::string> development_agent, release_agent;
    std::optional<RecordLevel> development_record, release_record;
};

std::expected<Config, std::string> read_config(const std::filesystem::path& base) {
    Config out;
    const auto file = base / "omui.config.ts";
    std::error_code error;
    if (!std::filesystem::is_regular_file(file, error)) return out;
    auto data = lang::ts::evaluate(file);
    if (!data) return std::unexpected(data.error());
    if (data->kind != "config") return std::unexpected(std::format("{}: its default export is defineConfig(…), not a {}", utf8(file), data->kind));
    const auto value = parse_json(data->json);
    if (!value) return std::unexpected(std::format("{}: {}", utf8(file), value.error().text()));
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
    return out;
}

int player_main(int argc, char** argv) {
    // The player's switches beside the application's (RunOptions), on one
    // command line, parsed once (mcpplibs.cmdline, D-56).
    using mcpplibs::cmdline::App, mcpplibs::cmdline::Arg, mcpplibs::cmdline::Option;
    auto line = App("player")
        .description("Runs a script-first openmui application from its mcpp.toml")
        .option(Option("manifest").takes_value().value_name("FILE").help("the application's mcpp.toml, or its directory (default: ./mcpp.toml)"))
        .option(Option("dev").help("development mode: the application is its own designer (D-149)"))
        .option(Option("types").takes_value().value_name("DIR").help("write the TypeScript declarations of what it registers, its API (api.json) and tsc's configuration into DIR, then end (omui check)"));
    auto command = command_line(std::move(line), argc, argv);
    if (!command) return command.error();
    const cmdline::ParsedArgs& parsed = command->args;
    RunOptions& options = command->options;
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
    std::optional<player::Player> p;
    AppSpec app;
    app.id = m->id;
    app.title = m->title;
    app.size = m->size;
    app.effects = m->effects;
    app.setup = [&](Runtime& rt) {
        if (record) rt.set_record_level(*record);
        p.emplace(rt, *m, std::vector<player::Language>{lang::js::typescript(), lang::js::javascript()},
                  player::PlayerOptions{.design = design});
        if (auto r = p->start(); !r) std::print(std::cerr, "{}", r.error());
        // A saved file shows within a frame in development mode (W2: save to screen ≤ 100 ms at p95; a
        // stat of the app's files a frame costs microseconds), within a tenth of a second otherwise.
        p->watch(design ? seconds(1.0 / 60) : seconds(0.1));
    };
    app.ready = [&] { return p && p->idle(); };
    app.teardown = [&](Runtime&) { p.reset(); };  // the player goes before its runtime
    return run(std::move(app), options);
}

}  // namespace omui
