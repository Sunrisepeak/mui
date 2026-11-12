module mlibs.agent;

import std;
import openas;
import mlibs.control;

namespace mlibs::agent {

namespace {

std::string described(const Subject& subject) {
    const Capabilities can = subject.capabilities();
    return std::format("{{\"name\":{},\"interface\":{},\"capabilities\":{{\"observation\":{},\"budget\":{},\"control\":{},\"time\":{},\"causes\":{},"
                       "\"provenance\":{},\"preview\":{},\"history\":{}}},\"hash\":\"{:016x}\"}}",
                       json_text(subject.name()), json_text(subject.interface_name()), can.observation, can.budget, can.control, can.time, can.causes,
                       can.provenance, can.preview, can.history, subject.state_hash());
}

constexpr std::string_view cost_name(mlibs::control::Cost cost) noexcept {
    switch (cost) {
        case mlibs::control::Cost::instant: return "instant";
        case mlibs::control::Cost::frame: return "frame";
        case mlibs::control::Cost::bounded: return "bounded";
        case mlibs::control::Cost::heavy: return "heavy";
    }
    return "?";
}

// Briefly: a name, its effect and a line each.
std::string brief(const Subject& subject) {
    std::string out = "{\"operations\":[";
    bool first = true;
    for (const mlibs::control::CatalogEntry& entry : subject.operations()) {
        if (!first) out += ',';
        first = false;
        out += std::format("{{\"name\":{},\"effect\":{},\"summary\":{}}}", json_text(entry.operation.name), json_text(openas::name(entry.operation.effect)),
                           json_text(entry.summary));
    }
    out += "]}";
    return out;
}

// One operation whole: its parameters, what it answers, its cost, how it is undone.
std::string whole(const mlibs::control::CatalogEntry& entry) {
    std::string out = std::format("{{\"name\":{},\"effect\":{},\"summary\":{},\"parameters\":[", json_text(entry.operation.name),
                                  json_text(openas::name(entry.operation.effect)), json_text(entry.summary));
    bool first = true;
    for (const mlibs::control::Parameter& parameter : entry.parameters) {
        if (!first) out += ',';
        first = false;
        out += std::format("{{\"name\":{},\"type\":{},\"doc\":{},\"optional\":{}}}", json_text(parameter.name), json_text(parameter.type), json_text(parameter.doc),
                           parameter.optional);
    }
    out += std::format("],\"result\":{},\"cost\":{},\"undo\":{},\"undoable\":{},\"cancellable\":{}}}", json_text(entry.result), json_text(cost_name(entry.cost)),
                       json_text(entry.undo), entry.undoable, entry.cancellable);
    return out;
}

}  // namespace

SubjectSession::SubjectSession(std::vector<Subject*> subjects, SessionOptions options)
    : subjects_(std::move(subjects)), options_(std::move(options)) {}

void SubjectSession::poll() {
    for (Subject* subject : subjects_) subject->poll();
}

void SubjectSession::request(std::string_view line, const Reply& reply) {
    auto parsed = parse_request(line);
    if (!parsed) {
        Answer answer(reply, {});
        answer.fail(openas::error_code::invalid_argument, parsed.error());
        return;
    }
    const Request& request = *parsed;
    Answer answer(reply, request.re);

    if (request.op == "subjects") {
        std::string out = "{\"subjects\":[";
        for (std::size_t index = 0; index < subjects_.size(); ++index) out += (index ? "," : "") + described(*subjects_[index]);
        out += "]}";
        answer.data(out);
        answer.result();
        return;
    }
    // `describe`: what one of ours can do, in tiers — a subject briefly, one operation whole, the whole listing.
    if (request.op == "describe") {
        const auto operation = request["operation"].text();
        const auto named = request["subject"].text();
        Subject* candidate = nullptr;
        for (Subject* each : subjects_) {
            if (named ? each->name() == *named : (operation ? operation->starts_with(std::string(each->name()) + ".") : subjects_.size() == 1)) candidate = each;
            if (candidate) break;
        }
        if (!candidate) {
            if (named) return answer.fail(openas::error_code::not_found, std::format("no subject here is named `{}` (ask `subjects`)", *named));
            if (operation) return answer.fail(openas::error_code::not_found, std::format("no subject here has an operation `{}` (ask `subjects`)", *operation));
            return answer.fail(openas::error_code::invalid_argument, "`describe` says which: a `subject`, or an `operation` by its whole name");
        }
        if (operation) {
            const auto entries = candidate->operations();
            const auto entry = std::ranges::find_if(entries, [&](const auto& listed) { return listed.operation.name == *operation; });
            if (entry == entries.end()) return answer.fail(openas::error_code::not_found, std::format("`{}` has no operation `{}`", candidate->name(), *operation));
            answer.data(whole(*entry));
        } else if (request["detail"].text() == "full" && !candidate->listing().empty()) {
            answer.data(candidate->listing());
        } else {
            answer.data(brief(*candidate));
        }
        answer.result();
        return;
    }
    // An operation, by its whole name in a subject's catalog.
    Subject* subject = nullptr;
    const mlibs::control::CatalogEntry* found = nullptr;
    for (Subject* candidate : subjects_) {
        for (const mlibs::control::CatalogEntry& entry : candidate->operations())
            if (entry.operation.name == request.op) {
                subject = candidate;
                found = &entry;
            }
        if (found) break;
    }
    if (!found) {
        answer.fail(openas::error_code::not_found, std::format("no subject here has an operation `{}` (ask `subjects`, then `describe` one)", request.op));
        return;
    }
    // Authority first: nothing reaches a subject undecided.
    if (options_.access) {
        // An operation whose effect is what it runs (an action of a node, a verb: `delegated`) declares the most
        // it can do; what it does this time is decided where it runs, by the same access. Here it is decided as
        // an operation that may be asked at all.
        const openas::Effect effect = found->delegated ? openas::Effect::read : found->operation.effect;
        const openas::Decision decision = options_.access->decide(request.op, effect, found->operation.person, options_.actor, options_.session.get(), request.op);
        if (decision == openas::Decision::refuse) {
            answer.fail(openas::error_code::refused, std::format("`{}` is not allowed here", request.op));
            return;
        }
        if (decision == openas::Decision::ask && request.answer != "yes") {
            answer.fail(openas::error_code::needs_answer, std::format("`{}` ({}) asks its person first: send it again with \"answer\":\"yes\" once they agree",
                                                                      request.op, openas::name(found->operation.effect)),
                        openas::exit_code::needs_answer);
            return;
        }
    }
    // Who asks is the session's to say: its owner when it has one, never the line's own claim.
    Request decided = request;
    decided.actor = options_.session ? options_.session->attribute(options_.actor) : options_.actor;
    subject->operate(decided.op, decided, answer);
    if (!answer.finished() && !answer.deferred()) answer.result();
}

}  // namespace mlibs::agent
