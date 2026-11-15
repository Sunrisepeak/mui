// kiln-play — Kiln without a window, played over lines: an agent with a pipe, a playtest, CI.
//
//   kiln-play run [--session FILE] [--tier human|assist] [--actor ACTOR]
//       requests on standard input, a JSON line each ({"op":"subjects"} says what is here); the records of
//       their answers on standard output. With --session, the game goes on from where the file's requests
//       left it: the game is a function of what it was asked, so the file is the game.
//
// Exit 0 done, 1 a request failed (said why), 2 a wrong command. The program is the runtime's
// (xengine.runtime.lines); what is Kiln's is the cavern it begins and what stands beside the game.
import std;
import mlibs.agent;
import xengine.runtime.lines;
import omui.showcase.kiln.headless;

int main(int argc, char** argv) {
    return xengine::runtime::play_over_lines<kiln::Play>(
        argc, argv,
        {.program = {.name = "kiln-play", .about = "Kiln without a window"},
         .begin = [](std::optional<std::uint64_t>) { return kiln::start(kiln::level::first()); },
         // The matter of the game's world beside the game, where the tier is shown it.
         .subjects = [](kiln::Standalone& game, std::shared_ptr<void>& kept) {
             auto causes = std::make_shared<kiln::Causes>(game);
             kept = causes;
             return kiln::subjects(game, causes.get());
         }});
}
