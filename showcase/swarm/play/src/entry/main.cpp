// swarm-play — Swarm without a window, played over lines: an agent with a pipe, a playtest, CI.
//
//   swarm-play run [--session FILE] [--tier human|assist] [--actor ACTOR] [--seed N]
//       requests on standard input, a JSON line each ({"op":"subjects"} says what is here); the records of
//       their answers on standard output. With --session, the game goes on from where the file's requests
//       left it: the game is a function of what it was asked, so the file is the game.
//
// Exit 0 done, 1 a request failed (said why), 2 a wrong command. The program is the runtime's
// (xengine.runtime.lines; as the session that made Swarm left it, this file was a copy of Kiln's with the names
// changed — the copy is what moved the program into the runtime).
import std;
import xengine.runtime.lines;
import omui.showcase.swarm.headless;

int main(int argc, char** argv) {
    return xengine::runtime::play_over_lines<swarm::Play>(
        argc, argv,
        {.program = {.name = "swarm-play", .about = "Swarm without a window", .seed = "which game: where the waves after the first come from (1 when none)"},
         .begin = [](std::optional<std::uint64_t> seed) -> std::expected<swarm::State, std::string> { return swarm::start(seed.value_or(1)); }});
}
