// swarm-play — Swarm without a window, played over lines: an agent with a pipe, a playtest, CI.
//
//   swarm-play run [--session FILE] [--tier human|assist] [--actor ACTOR] [--seed N]
//       requests on standard input, a JSON line each ({"op":"subjects"} says what is here); the records of
//       their answers on standard output. With --session, the game goes on from where the file's requests
//       left it: the game is a function of what it was asked, so the file is the game.
//
// Exit 0 done, 1 a request failed (said why), 2 a wrong command.
import std;
import openas;
import mlibs.agent;
import omui.showcase.swarm.headless;
import mcpplibs.os;
import mcpplibs.cmdline;

namespace {

namespace cmdline = mcpplibs::cmdline;
using mlibs::agent::json_text;

constexpr std::string_view usage = "usage: swarm-play run [--session FILE] [--tier human|assist] [--actor ACTOR] [--seed N]";

int wrong(std::string_view text) {
    std::println("{{\"ok\":false,\"error\":{},\"usage\":{}}}", json_text(text), json_text(usage));
    return 2;
}
int failed(std::string_view text) {
    std::println("{{\"ok\":false,\"error\":{}}}", json_text(text));
    return 1;
}

int run(const cmdline::ParsedArgs& args) {
    xengine::runtime::Options options{.keep = 3600};
    if (const auto tier = args.value("tier")) {
        if (*tier == "assist") options.tier = {.name = "assist", .reaction_steps = 0, .actions_per_second = 0, .full_view = true};
        else if (*tier != "human") return wrong("--tier is human (as a person plays) or assist (shown everything, no limit)");
    }
    mlibs::agent::SessionOptions asked_by;
    if (const auto actor = args.value("actor")) {
        auto said = openas::parse(*actor);
        if (!said) return wrong("--actor is an actor as openas writes one: agent:name, person:name");
        asked_by.actor = std::move(*said);
    }
    std::uint64_t seed = 1;
    if (const auto given = args.value("seed")) {
        const std::string_view text = *given;
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), seed);
        if (error != std::errc() || end != text.data() + text.size()) return wrong("--seed is a whole number, 0 or more");
    }
    swarm::Standalone game(swarm::start(seed), options);
    mlibs::agent::SubjectSession session({&game}, std::move(asked_by));
    const auto kept = args.value("session");
    const auto conversed = mlibs::agent::converse(session, std::cin, std::cout, kept ? mcpplibs::os::fs::from_utf8(*kept) : std::filesystem::path());
    if (!conversed) return failed(conversed.error());
    return conversed->done ? 0 : 1;
}

cmdline::App command_line() {
    using cmdline::App, cmdline::Option;
    return App("swarm-play")
        .description("Swarm without a window, for agents, playtests and CI: JSON lines in, records out, exit 0 done / 1 failed / 2 a wrong command")
        .subcommand(App("run")
                        .description("requests, a JSON line each, answered in records; a new game, or one gone on with from a session's file")
                        .option(Option("session").takes_value().value_name("FILE").help("the requests so far: played again first, the new ones added"))
                        .option(Option("tier").takes_value().value_name("TIER").help("human (default: what a person is shown, a person's reactions) or assist"))
                        .option(Option("actor").takes_value().value_name("ACTOR").help("who asks: agent:name, person:name (an agent with no name when none)"))
                        .option(Option("seed").takes_value().value_name("N").help("which game: where the waves after the first come from (1 when none)")));
}

}  // namespace

int main(int argc, char** argv) {
    const cmdline::App app = command_line();
    auto parsed = app.parse(argc, argv);
    if (!parsed) return parsed.error().is_error() ? wrong(parsed.error().message) : 0;
    if (!parsed->has_subcommand()) return wrong("a command: run");
    return run(parsed->subcommand()->get());
}
