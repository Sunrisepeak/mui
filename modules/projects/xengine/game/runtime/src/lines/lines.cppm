// xengine.runtime.lines — a game as a program played over lines (the package's `lines` feature).
//
// What every game's program was writing for itself (Kiln's and Swarm's were the same file but for a name):
// `<name> run [--session FILE] [--tier human|assist] [--actor ACTOR] [--seed N]` — requests on standard input, a
// JSON line each, the records of their answers on standard output; with --session the game goes on from where
// the file's requests left it (mlibs.agent's `converse`: the game is a function of what it was asked, so the file
// is the game). Exit 0 done, 1 a request failed (said why), 2 a wrong command.
//
//   int main(int argc, char** argv) {
//       return xengine::runtime::play_over_lines<kiln::Play>(argc, argv, {
//           .program = {.name = "kiln-play", .about = "Kiln without a window"},
//           .begin = [](std::optional<std::uint64_t>) { return kiln::start(kiln::level::first()); }});
//   }
//
// A feature and not the package's own: a command line and standard input are the native systems', and the
// runtime also builds for the Web.
export module xengine.runtime.lines;

import std;
import mlibs.agent;
export import xengine.runtime;

export namespace xengine::runtime {

struct Program {
    std::string_view name;   // the program's: "kiln-play"
    std::string_view about;  // the game, in a few words: "Kiln without a window"
    std::string_view seed;   // what --seed chooses ("which game: where the waves come from"); empty: no --seed
};

// What the command line said.
struct Asked {
    Options                      options;   // the tier asked for; an hour of steps kept to seek within
    mlibs::agent::SessionOptions asked_by;  // who asks
    std::filesystem::path        session;   // the requests so far (empty: every run begins the game again)
    std::optional<std::uint64_t> seed;
};

// The command line read and `play` called for `run`; the exit code. `play` answers how the conversation went,
// or why the game could not begin.
int over_lines(int argc, char** argv, const Program& program, const std::function<std::expected<mlibs::agent::Conversed, std::string>(Asked&)>& play);

template <Playable G>
struct Lines {
    Program program;
    // A new game (`seed` as --seed gave it, where the program takes one), or why there is none.
    std::function<std::expected<typename G::State, std::string>(std::optional<std::uint64_t> seed)> begin;
    // The session's subjects, where the game is not alone: the game and what stands beside it (the matter of
    // its world, its causes). What they need alive goes in `kept`: it is gone before the game is.
    std::function<std::vector<mlibs::agent::Subject*>(Runtime<G>& game, std::shared_ptr<void>& kept)> subjects;
};

template <Playable G>
int play_over_lines(int argc, char** argv, const Lines<G>& lines) {
    return over_lines(argc, argv, lines.program, [&](Asked& asked) -> std::expected<mlibs::agent::Conversed, std::string> {
        auto begun = lines.begin(asked.seed);
        if (!begun) return std::unexpected(begun.error());
        Runtime<G> game(std::move(*begun), asked.options);
        std::shared_ptr<void> kept;
        std::vector<mlibs::agent::Subject*> subjects = lines.subjects ? lines.subjects(game, kept) : std::vector<mlibs::agent::Subject*>{&game};
        mlibs::agent::SubjectSession session(std::move(subjects), std::move(asked.asked_by));
        return mlibs::agent::converse(session, std::cin, std::cout, asked.session);
    });
}

}  // namespace xengine::runtime
