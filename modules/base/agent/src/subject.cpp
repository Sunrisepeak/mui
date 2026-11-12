module mlibs.agent;

import std;
import openas;
import mlibs.control;

namespace mlibs::agent {

namespace {

std::string described(const Subject& subject) {
    const Capabilities can = subject.capabilities();
    return std::format("{{\"name\":{},\"interface\":{},\"capabilities\":{{\"observation\":{},\"control\":{},\"time\":{},\"causes\":{},\"preview\":{},"
                       "\"history\":{}}},\"hash\":\"{:016x}\"}}",
                       json_text(subject.name()), json_text(subject.interface_name()), can.observation, can.control, can.time, can.causes,
                       can.preview, can.history, subject.state_hash());
}

std::string catalog(const Subject& subject) {
    std::string out = "{\"operations\":[";
    bool first = true;
    for (const Operation& operation : subject.operations()) {
        if (!first) out += ',';
        first = false;
        out += std::format("{{\"name\":{},\"effect\":{},\"doc\":{}}}", json_text(std::format("{}.{}", subject.name(), operation.name)),
                           json_text(openas::name(operation.effect)), json_text(operation.doc));
    }
    out += "]}";
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
    // "<subject>.<operation>": the subject is the longest of ours the name begins with.
    Subject* subject = nullptr;
    std::string_view operation_name;
    for (Subject* candidate : subjects_) {
        const std::string_view name = candidate->name();
        if (request.op.size() > name.size() + 1 && request.op.starts_with(name) && request.op[name.size()] == '.' &&
            (!subject || name.size() > subject->name().size())) {
            subject = candidate;
            operation_name = std::string_view(request.op).substr(name.size() + 1);
        }
    }
    if (!subject) {
        answer.fail(openas::error_code::not_found, std::format("no subject here answers `{}` (ask `subjects`)", request.op));
        return;
    }
    if (operation_name == "describe") {
        answer.data(catalog(*subject));
        answer.result();
        return;
    }
    const auto operations = subject->operations();
    const auto found = std::ranges::find(operations, operation_name, &Operation::name);
    if (found == operations.end()) {
        answer.fail(openas::error_code::not_found, std::format("`{}` has no operation `{}` (ask `{}.describe`)", subject->name(), operation_name, subject->name()));
        return;
    }
    // Authority first: nothing reaches a subject undecided.
    if (options_.access) {
        const openas::Decision decision =
            options_.access->decide(request.op, found->effect, false, options_.actor, options_.session.get(), request.op);
        if (decision == openas::Decision::refuse) {
            answer.fail(openas::error_code::refused, std::format("`{}` is not allowed here", request.op));
            return;
        }
        if (decision == openas::Decision::ask && request.answer != "yes") {
            answer.fail(openas::error_code::needs_answer, std::format("`{}` ({}) asks its person first: send it again with \"answer\":\"yes\" once they agree",
                                                                      request.op, openas::name(found->effect)),
                        openas::exit_code::needs_answer);
            return;
        }
    }
    subject->operate(operation_name, request, answer);
    if (!answer.finished()) answer.result();
}

}  // namespace mlibs::agent
