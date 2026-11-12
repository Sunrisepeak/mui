module omui.agent;

import std;
import openas;
import mlibs.agent;
import mlibs.hash;
import mlibs.json;

namespace omui::agent {

InterfaceSubject::InterfaceSubject(Runtime& runtime, Protocol& protocol, InterfaceOptions options)
    : runtime_(runtime), protocol_(protocol), options_(options), tree_(runtime) {}

std::uint64_t InterfaceSubject::state_hash() const { return mlibs::hash::fnv1a(tree_.state().json()); }

void InterfaceSubject::operate(std::string_view operation, const mlibs::agent::Request& request, mlibs::agent::Answer& answer) {
    // The protocol's own request: the operation without its subject, who asks as the session says (never as
    // the line claimed), no `re` (the kernel's answer echoes it).
    auto asked = parse_json(request.document->root().dump());
    if (!asked || !asked->map()) return answer.fail(openas::error_code::invalid_argument, "a request is a JSON object");
    Value line;
    line.set("op", std::string(operation.substr(name().size() + 1)));
    for (const auto& [key, value] : *asked->map())
        if (key != "op" && key != "re" && key != "actor") line.set(key, value);
    line.set("actor", openas::format(request.actor));

    // Its records, as they come — now, or later for what waits (a condition, frames shown): this copy of the
    // answer ends then, and the session does not end the original.
    auto later = std::make_shared<mlibs::agent::Answer>(answer.later());
    protocol_.request(line.json(), [later](std::string_view record) {
        auto parsed = mlibs::json::Document::parse(record, "record");
        if (!parsed || !parsed->root().is_object()) return;
        std::string kind, members;
        for (const auto& [key, value] : parsed->root().members()) {
            if (key == "kind") {
                kind = value.text().value_or("");
                continue;
            }
            if (key == "re") continue;
            members += std::format("{}{}:{}", members.empty() ? "" : ",", mlibs::agent::json_text(key), value.dump());
        }
        later->record(kind, members);
    });
}

ProcessSession::ProcessSession(Runtime& runtime, ProtocolOptions options)
    : runtime_(runtime),
      default_actor_(options.default_actor),
      protocol_(runtime, options),
      interface_(runtime, protocol_, {.traces_pixels = static_cast<bool>(options.pixel_owner)}) {}

void ProcessSession::request(std::string_view line, const mlibs::agent::Reply& reply) {
    // The kernel's requests: `subjects`, an operation by its whole name (a dot), `describe` a subject or an
    // operation. Everything else — what is no request too — is the protocol's, answered as it always was.
    bool kernels = false;
    if (auto parsed = mlibs::json::Document::parse(line, "request"); parsed && parsed->root().is_object()) {
        const std::string op = parsed->root()["op"].text().value_or("");
        kernels = op == "subjects" || op.contains('.') || (op == "describe" && (parsed->root()["subject"] || parsed->root()["operation"]));
    }
    if (!kernels) return protocol_.request(line, reply);

    std::vector<mlibs::agent::Subject*> subjects{&interface_};
    for (mlibs::agent::Subject* held : runtime_.subjects()) subjects.push_back(held);
    const auto session = protocol_.session();
    mlibs::agent::SessionOptions options;
    options.access = runtime_.agent_access().control_binding();
    if (session) options.session = session->control_session();
    if (const auto actor = Actor::parse(default_actor_)) options.actor = to_openas(*actor);
    mlibs::agent::SubjectSession(std::move(subjects), std::move(options)).request(line, reply);
}

}  // namespace omui::agent
