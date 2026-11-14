module xengine.runtime.lines;

import std;
import openas;
import mlibs.agent;
import mcpplibs.os;
import mcpplibs.cmdline;

namespace xengine::runtime {

namespace {

namespace cmdline = mcpplibs::cmdline;
using mlibs::agent::json_text;

std::string usage(const Program& program) {
    return std::format("usage: {} run [--session FILE] [--tier human|assist] [--actor ACTOR]{}", program.name, program.seed.empty() ? "" : " [--seed N]");
}
int wrong(const Program& program, std::string_view text) {
    std::println("{{\"ok\":false,\"error\":{},\"usage\":{}}}", json_text(text), json_text(usage(program)));
    return 2;
}
int failed(std::string_view text) {
    std::println("{{\"ok\":false,\"error\":{}}}", json_text(text));
    return 1;
}

cmdline::App command_line(const Program& program) {
    using cmdline::App, cmdline::Option;
    App run = App("run")
                  .description("requests, a JSON line each, answered in records; a new game, or one gone on with from a session's file")
                  .option(Option("session").takes_value().value_name("FILE").help("the requests so far: played again first, the new ones added"))
                  .option(Option("tier").takes_value().value_name("TIER").help("human (default: what a person is shown, a person's reactions) or assist"))
                  .option(Option("actor").takes_value().value_name("ACTOR").help("who asks: agent:name, person:name (an agent with no name when none)"));
    if (!program.seed.empty()) run = std::move(run).option(Option("seed").takes_value().value_name("N").help(program.seed));
    return App(program.name)
        .description(std::format("{}, for agents, playtests and CI: JSON lines in, records out, exit 0 done / 1 failed / 2 a wrong command", program.about))
        .subcommand(std::move(run));
}

}  // namespace

int over_lines(int argc, char** argv, const Program& program, const std::function<std::expected<mlibs::agent::Conversed, std::string>(Asked&)>& play) {
    const cmdline::App app = command_line(program);
    auto parsed = app.parse(argc, argv);
    if (!parsed) return parsed.error().is_error() ? wrong(program, parsed.error().message) : 0;
    if (!parsed->has_subcommand()) return wrong(program, "a command: run");
    const cmdline::ParsedArgs& args = parsed->subcommand()->get();

    Asked asked{.options = {.keep = 3600}};
    if (const auto tier = args.value("tier")) {
        if (*tier == "assist") asked.options.tier = {.name = "assist", .reaction_steps = 0, .actions_per_second = 0, .full_view = true};
        else if (*tier != "human") return wrong(program, "--tier is human (as a person plays) or assist (shown everything, no limit)");
    }
    if (const auto actor = args.value("actor")) {
        auto said = openas::parse(*actor);
        if (!said) return wrong(program, "--actor is an actor as openas writes one: agent:name, person:name");
        asked.asked_by.actor = std::move(*said);
    }
    if (!program.seed.empty()) {
        if (const auto given = args.value("seed")) {
            const std::string_view text = *given;
            std::uint64_t seed = 0;
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), seed);
            if (error != std::errc() || end != text.data() + text.size()) return wrong(program, "--seed is a whole number, 0 or more");
            asked.seed = seed;
        }
    }
    if (const auto kept = args.value("session")) asked.session = mcpplibs::os::fs::from_utf8(*kept);
    const auto conversed = play(asked);
    if (!conversed) return failed(conversed.error());
    return conversed->done ? 0 : 1;
}

}  // namespace xengine::runtime
