// mlibs.agent:replay — how a subject was designed, as data a release plays (G8, D-253; replay@1, this package's
// kind): a branch of its history, step by step — each step every source's text as it was then, what the step did
// (its caption), who did it and its time on screen. The owner of a subject plays it in place (an interface: each
// step's texts run in place of its scripts', presented as an edit is, under its caption); what a source is stays
// the owner's, as in the history.
//
//     {"$format": "replay@1",
//      "replay": {"doc": "…", "subject": "org.openmui.subscription", "branch": "main",
//                 "steps": [{"caption": "1/9 · …", "actor": "agent:designer", "seconds": 1.5,
//                            "node": "main/plans", "files": {"app/main.ts": "3f2a…"}}],
//                 "texts": {"3f2a…": "…"}}}
//
// A text is written once, by content; a step names its sources' texts by key.
export module mlibs.agent:replay;

import std;

export namespace mlibs::agent {

struct ReplayStep {
    std::string caption;                        // what the step did, in words
    std::string actor;                          // who did it: "agent:designer", "person:developer"
    double      seconds = 1.5;                  // its time on screen
    std::map<std::string, std::string> texts;  // a source's path → its text then
    std::string node;                           // what it built or changed, where its maker is shown ("" when not known)
};

struct Replay {
    std::string             doc;      // what it is, in a sentence
    std::string             subject;  // whose design: an application's id
    std::string             branch;   // the history's branch it was taken from
    std::vector<ReplayStep> steps;    // oldest first
};

inline constexpr std::string_view replay_format = "replay@1";

// replay@1's text read, every step's texts resolved; the first mistake at its line, column and JSON pointer in
// `file`, with the fix where there is one.
[[nodiscard]] std::expected<Replay, std::string> read_replay(std::string_view text, std::string_view file = "replay");
// Its text: indented, each text kept once (by its content's hash), the same replay always the same bytes.
[[nodiscard]] std::string write_replay(const Replay& replay);

}  // namespace mlibs::agent
