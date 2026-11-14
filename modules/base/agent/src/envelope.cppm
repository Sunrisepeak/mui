// mlibs.agent:envelope — a request and its answer as records (openas §6).
//
// A request is one JSON object a line: `op` names what is asked, `re` (any
// JSON value) is echoed in every record of its answer, `answer` is what a
// person answered when the operation was asked for before. An answer is
// records, one a line — `data`, `notice`, `error` — and last a `result` with
// the exit code. A subject writes what its data means; the framing is here,
// the same for every subject.
//
// An error says what is wrong and carries the ways out of it (D-186): `next`,
// an array of {"why": …, "request": {…}} — each request one that can be sent
// as it is. A subject that knows the way out gives it; one that gives none
// still answers with the session's (the operation described, an example of it).
// A result that is a success carries what follows it, when its catalog says so:
// `then`, in the same form (after a change, the reading that shows what it did).
export module mlibs.agent:envelope;

import std;
import openas;
import mlibs.json;
import :endpoint;

export namespace mlibs::agent {

// A JSON string literal.
[[nodiscard]] std::string json_text(std::string_view text);
// One way out of an error: why it helps, and a request (a JSON object) to send as it is.
[[nodiscard]] std::string way_out(std::string_view why, std::string_view request);

struct Request {
    std::string op;
    std::string re;      // its `re` as JSON text; empty: it gave none
    std::string answer;  // a person's answer to the question this operation asks ("yes"); empty: none
    // Who asks, as the session knows them — never what the line claims. A subject records it with what it changes.
    openas::Actor actor{openas::Kind::agent, {}, {}};
    std::shared_ptr<const mlibs::json::Document> document;
    // The request's own members, to read its arguments (an absent one is a node that is false).
    [[nodiscard]] mlibs::json::Node operator[](std::string_view key) const { return document->root()[key]; }
};

// The request a line holds, or why it is none.
[[nodiscard]] std::expected<Request, std::string> parse_request(std::string_view line);

class Answer {
public:
    Answer(Reply reply, std::string re);

    // A `data` record carrying one JSON value (its text).
    void data(std::string_view json);
    void notice(std::string_view text);
    // `next`: the ways out, a JSON array of {"why", "request"} (none given: the session's, see otherwise).
    void error(std::string_view code, std::string_view text, std::string_view next = {});
    // The last record; once.
    void result(int exit_code = openas::exit_code::done);
    // An error and the result that ends the answer.
    void fail(std::string_view code, std::string_view text, int exit_code = openas::exit_code::failed, std::string_view next = {});
    // The ways out an error of this answer carries when it gives none itself (a JSON array, as `next`).
    void otherwise(std::string next) { otherwise_ = std::move(next); }
    // What follows when it is done: given with a result that is a success (a JSON array, as `next`: `then`).
    void then(std::string follows) { then_ = std::move(follows); }
    [[nodiscard]] bool finished() const noexcept { return finished_; }

    // A record as a subject that already speaks records gives it: its kind and its members (JSON, without
    // the braces: `"data":{…}`); a `result` ends the answer.
    void record(std::string_view kind, std::string_view members);
    // The answer comes later — a condition waited for, frames to be shown: the subject keeps the copy this
    // returns and ends it then (at its owner's safe point); the session does not end this one.
    [[nodiscard]] Answer later() {
        deferred_ = true;
        return *this;
    }
    [[nodiscard]] bool deferred() const noexcept { return deferred_; }

private:
    Reply       reply_;
    std::string re_;
    std::string otherwise_;
    std::string then_;
    bool        finished_ = false;
    bool        deferred_ = false;
};

}  // namespace mlibs::agent
