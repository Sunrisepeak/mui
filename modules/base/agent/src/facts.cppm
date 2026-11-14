// mlibs.agent:facts — what was asked of a process's subjects, as facts (D-186,
// norm 11): every operation that changes something and every request that
// failed, each one record with a number — who asked, of which subject, what,
// how it ended and why, recorded when its answer ends (a subject may end one
// at a later safe point: it is on record then, under the number it was given). A log line, a trace and a diagnostic are readings of
// such a record, not other records: the line `mlibs.log` keeps carries the
// same number (`fact`, its book's: a process with several books tells them
// by who asked), and so does the result the one who asked was answered with. An agent reads them as they are, by what they are about:
//
//   → {"op":"log"}                                   the latest, the oldest first
//   → {"op":"log","since":12,"failed":true}          those after a number that failed
//   → {"op":"log","subject":"matter","limit":5}      a subject's latest
//
// A book keeps the latest (256 by default); numbers go on from 1 and are not
// used again. Recording is a cold path: a request is one, never a cell's or a
// pixel's step.
export module mlibs.agent:facts;

import std;
import openas;

export namespace mlibs::agent {

struct Fact {
    std::uint64_t  id = 0;       // 1, 2, … in its book
    std::string    subject;      // "" when the request named none that is here
    std::string    operation;
    openas::Effect effect = openas::Effect::read;
    openas::Actor  actor{openas::Kind::agent, {}, {}};
    std::string    outcome;      // "done", or the error's code
    std::string    text;         // what the error said (empty when done)
    std::string    request;      // the request as it came: one JSON object
    std::string    at;           // where the subject's own record has the change (Subject::recorded): a JSON object, or empty
};

// A fact as a line a person reads: "agent:ada matter.place → done", "agent matter.heat → invalid-argument: …".
[[nodiscard]] std::string line_of(const Fact& fact);
// … and as JSON, what `log` answers.
[[nodiscard]] std::string json_of(const Fact& fact);

class Facts {
public:
    explicit Facts(std::size_t capacity = 256) : capacity_(capacity) {}
    // A number taken for a fact before it is whole: given with the result, then recorded under it.
    [[nodiscard]] std::uint64_t reserve() noexcept { return ++last_; }
    // Kept — under the number it has, or the next when it has none — and written to the log (category
    // `agent.<subject>`, at info) with its number.
    std::uint64_t record(Fact fact);
    struct Asked {
        std::uint64_t    since = 0;      // those after this number
        std::string_view subject;        // of this subject alone ("" for any)
        bool             failed = false; // only those that did not end `done`
        std::size_t      limit = 50;     // the latest so many
    };
    [[nodiscard]] std::vector<Fact> read(const Asked& asked) const;
    [[nodiscard]] std::uint64_t last() const noexcept { return last_; }

private:
    std::size_t      capacity_;
    std::uint64_t    last_ = 0;
    std::deque<Fact> kept_;
};

}  // namespace mlibs::agent
