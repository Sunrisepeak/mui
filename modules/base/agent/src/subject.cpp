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

std::string requests(std::span<const std::string_view> lines) {
    std::string out = "[";
    for (std::size_t index = 0; index < lines.size(); ++index) out += std::format("{}{}", index ? "," : "", lines[index]);
    return out + "]";
}

// Briefly: what it is and the tasks to begin with (its catalog's guide), then each operation's name, effect and a line.
std::string brief(const Subject& subject) {
    const mlibs::control::Guide guide = subject.guide();
    std::string out = "{";
    if (!guide.about.empty()) out += std::format("\"about\":{},", json_text(guide.about));
    if (!guide.tasks.empty()) {
        out += "\"tasks\":[";
        for (std::size_t index = 0; index < guide.tasks.size(); ++index) {
            const mlibs::control::Task& task = guide.tasks[index];
            out += std::format("{}{{\"name\":{},\"summary\":{},\"steps\":{}}}", index ? "," : "", json_text(task.name), json_text(task.summary), requests(task.steps));
        }
        out += "],";
    }
    out += "\"operations\":[";
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
    out += std::format("],\"result\":{},\"cost\":{},\"undo\":{},\"undoable\":{},\"cancellable\":{}", json_text(entry.result), json_text(cost_name(entry.cost)),
                       json_text(entry.undo), entry.undoable, entry.cancellable);
    if (!entry.examples.empty()) out += std::format(",\"examples\":{}", requests(entry.examples));
    return out + "}";
}

// ── the ways out of an error (`next`): requests to send as they are ──

std::string describing_subject(std::string_view subject) { return std::format("{{\"op\":\"describe\",\"subject\":{}}}", json_text(subject)); }
std::string describing(std::string_view operation) { return std::format("{{\"op\":\"describe\",\"operation\":{}}}", json_text(operation)); }
inline constexpr std::string_view who_is_here = R"({"op":"subjects"})";

std::string ways(std::initializer_list<std::string> each) {
    std::string out = "[";
    for (const std::string& way : each)
        if (!way.empty()) out += std::format("{}{}", out.size() > 1 ? "," : "", way);
    return out + "]";
}

// How many letters apart two names are (an edit each).
std::size_t apart(std::string_view one, std::string_view other) {
    std::vector<std::size_t> row(other.size() + 1);
    std::ranges::iota(row, std::size_t{0});
    for (std::size_t at = 1; at <= one.size(); ++at) {
        std::size_t corner = row[0];
        row[0] = at;
        for (std::size_t along = 1; along <= other.size(); ++along) {
            const std::size_t above = row[along];
            row[along] = std::min({row[along] + 1, row[along - 1] + 1, corner + (one[at - 1] == other[along - 1] ? 0u : 1u)});
            corner = above;
        }
    }
    return row[other.size()];
}

// For a name no subject has: the names nearest it — those it begins, or begins with; failing those, the ones
// the fewest letters off, two at the most (of equals, in the catalog's order; three at the most) — each
// described; then what its subject can do when it names one that is here, else who is here.
std::string nearest(std::span<Subject* const> subjects, std::string_view wanted) {
    struct Near {
        std::size_t      apart;
        std::string_view name;
    };
    std::vector<Near> near;
    const Subject* owner = nullptr;
    for (const Subject* subject : subjects) {
        if (wanted.starts_with(std::string(subject->name()) + ".")) owner = subject;
        for (const mlibs::control::CatalogEntry& entry : subject->operations()) {
            const std::string_view name = entry.operation.name;
            const std::size_t distance = name.starts_with(wanted) || wanted.starts_with(name) ? 0 : apart(wanted, name);
            if (distance <= 2) near.push_back({distance, name});
        }
    }
    std::ranges::stable_sort(near, {}, &Near::apart);
    if (!near.empty()) std::erase_if(near, [least = near.front().apart](const Near& each) { return each.apart != least; });
    std::string out = "[";
    for (std::size_t index = 0; index < std::min<std::size_t>(near.size(), 3); ++index)
        out += std::format("{}{}", index ? "," : "", way_out(std::format("`{}` is an operation here: what it takes", near[index].name), describing(near[index].name)));
    if (owner) out += std::format("{}{}", out.size() > 1 ? "," : "", way_out(std::format("what `{}` can do", owner->name()), describing_subject(owner->name())));
    else out += std::format("{}{}", out.size() > 1 ? "," : "", way_out("who is here", who_is_here));
    return out + "]";
}

// The ways out a subject's own failure carries when it names none: the operation whole, and an example of it.
std::string ways_of(const mlibs::control::CatalogEntry& entry) {
    return ways({way_out(std::format("what `{}` takes and answers", entry.operation.name), describing(entry.operation.name)),
                 entry.examples.empty() ? std::string() : way_out("a request of it that is well formed", entry.examples.front())});
}

// The request again, with a person's answer.
std::string answered(const Request& request) {
    std::string out = "{";
    for (const auto& [key, value] : request.document->root().members())
        if (key != "answer") out += std::format("{}:{},", json_text(key), value.dump());
    return out + "\"answer\":\"yes\"}";
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
        answer.fail(openas::error_code::invalid_argument, parsed.error(), openas::exit_code::failed,
                    ways({way_out("a request as it is written; begin with who is here", who_is_here)}));
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
    // `find`: what answers an intent, across the subjects.
    if (request.op == "find") {
        const auto query = request["q"].text();
        if (!query || query->empty())
            return answer.fail(openas::error_code::invalid_argument, "`find` says what is wanted, in words: `q`", openas::exit_code::failed,
                               ways({way_out("an intent in a few words", R"({"op":"find","q":"what can I read here"})"), way_out("who is here", who_is_here)}));
        const auto limit = static_cast<std::size_t>(std::clamp<std::int64_t>(request["limit"].integer().value_or(5), 1, 20));
        std::string out = "{\"found\":[";
        bool first = true;
        for (const Found& found : find(subjects_, *query, limit)) {
            std::string sent = "[";
            for (std::size_t index = 0; index < found.requests.size(); ++index) sent += std::format("{}{}", index ? "," : "", found.requests[index]);
            out += std::format("{}{{\"kind\":{},\"subject\":{},\"name\":{},\"summary\":{},\"requests\":{}]}}", first ? "" : ",", json_text(found.kind), json_text(found.subject),
                               json_text(found.name), json_text(found.text), sent);
            first = false;
        }
        out += "]";
        // Nothing found is answered with where to look: each subject, described.
        if (first) {
            out += ",\"next\":[";
            for (std::size_t index = 0; index < subjects_.size(); ++index)
                out += std::format("{}{}", index ? "," : "", way_out(std::format("what `{}` can do, and the tasks to begin with", subjects_[index]->name()), describing_subject(subjects_[index]->name())));
            out += "]";
        }
        answer.data(out + "}");
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
            const std::string here = ways({way_out("who is here", who_is_here)});
            if (named) return answer.fail(openas::error_code::not_found, std::format("no subject here is named `{}` (ask `subjects`)", *named), openas::exit_code::failed, here);
            if (operation)
                return answer.fail(openas::error_code::not_found, std::format("no subject here has an operation `{}` (ask `subjects`)", *operation),
                                   openas::exit_code::failed, nearest(subjects_, *operation));
            return answer.fail(openas::error_code::invalid_argument, "`describe` says which: a `subject`, or an `operation` by its whole name", openas::exit_code::failed, here);
        }
        if (operation) {
            const auto entries = candidate->operations();
            const auto entry = std::ranges::find_if(entries, [&](const auto& listed) { return listed.operation.name == *operation; });
            if (entry == entries.end())
                return answer.fail(openas::error_code::not_found, std::format("`{}` has no operation `{}`", candidate->name(), *operation), openas::exit_code::failed,
                                   nearest(subjects_, *operation));
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
        answer.fail(openas::error_code::not_found, std::format("no subject here has an operation `{}` (ask `subjects`, then `describe` one)", request.op),
                    openas::exit_code::failed, nearest(subjects_, request.op));
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
            // What is declared is described whatever is allowed: an agent sees what else there is (a reading, a preview).
            answer.fail(openas::error_code::refused, std::format("`{}` is not allowed here", request.op), openas::exit_code::failed,
                        ways({way_out(std::format("what else `{}` can do: a reading or a preview may be allowed", subject->name()), describing_subject(subject->name()))}));
            return;
        }
        if (decision == openas::Decision::ask && request.answer != "yes") {
            answer.fail(openas::error_code::needs_answer, std::format("`{}` ({}) asks its person first: send it again with \"answer\":\"yes\" once they agree",
                                                                      request.op, openas::name(found->operation.effect)),
                        openas::exit_code::needs_answer, ways({way_out("the same request, once its person agrees", answered(request))}));
            return;
        }
    }
    // What it needs is in its catalog: a request without it is answered here, with what would be accepted.
    for (const mlibs::control::Parameter& parameter : found->parameters)
        if (!parameter.optional && !request[parameter.name]) {
            answer.fail(openas::error_code::invalid_argument,
                        std::format("`{}` needs `{}` ({}{}{})", request.op, parameter.name, parameter.type, parameter.doc.empty() ? "" : ": ", parameter.doc),
                        openas::exit_code::failed, ways_of(*found));
            return;
        }
    // A failure the subject answers without a way out still carries the operation's.
    answer.otherwise(ways_of(*found));
    // Who asks is the session's to say: its owner when it has one, never the line's own claim.
    Request decided = request;
    decided.actor = options_.session ? options_.session->attribute(options_.actor) : options_.actor;
    subject->operate(decided.op, decided, answer);
    if (!answer.finished() && !answer.deferred()) answer.result();
}

}  // namespace mlibs::agent
