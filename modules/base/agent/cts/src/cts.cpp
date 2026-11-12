module mlibs.agent.cts;

import std;
import openas;
import mlibs.control;
import mlibs.json;

namespace mlibs::agent::cts {

namespace {

using mlibs::cts::Check;
using mlibs::cts::Level;

// What a session answered to one request.
struct Answered {
    std::vector<std::string> lines;      // every record, as sent
    std::string              data;       // the first data record's line
    std::size_t              data_bytes = 0;  // … its `data`, compact
    bool                     data_empty = true;
    std::string              code, message;  // an error record's
    std::int64_t             exit = -1;      // the result's; -1: no result came
    bool                     records = true; // every line was a record (an object with a kind)
    std::vector<std::string> echoed;         // each record's `re`, as JSON ("" when it has none)
};

Answered ask(Session& session, std::string_view line) {
    Answered out;
    session.request(line, [&](std::string_view record) { out.lines.emplace_back(record); });
    for (const std::string& text : out.lines) {
        auto parsed = mlibs::json::Document::parse(text, "record");
        if (!parsed || !parsed->root().is_object() || !parsed->root()["kind"].is_string()) {
            out.records = false;
            continue;
        }
        const auto root = parsed->root();
        const std::string kind = root["kind"].text().value_or("");
        out.echoed.push_back(root["re"].dump());
        if (kind == "data" && out.data.empty()) {
            out.data = text;
            const auto data = root["data"];
            out.data_bytes = data.dump().size();
            out.data_empty = !data || data.is_null() || ((data.is_object() || data.is_array()) && data.size() == 0) || data.text() == "";
        }
        if (kind == "error") {
            out.code = root["code"].text().value_or("");
            out.message = root["text"].text().value_or("");
        }
        if (kind == "result") out.exit = root["exitCode"].integer().value_or(-1);
    }
    return out;
}

// The operation a request names ("" when it is no request).
std::string operation_of(std::string_view line) {
    auto parsed = mlibs::json::Document::parse(line, "request");
    return parsed ? parsed->root()["op"].text().value_or("") : std::string{};
}

const mlibs::control::CatalogEntry* entry_of(const Subject& subject, std::string_view operation) {
    for (const mlibs::control::CatalogEntry& entry : subject.operations())
        if (entry.operation.name == operation) return &entry;
    return nullptr;
}

// A request the probe gives for a capability its subject says it has: said so when it gives none.
bool shown(Check& check, std::string_view request, std::string_view what, const Subject& subject, std::source_location at = std::source_location::current()) {
    if (request.empty()) return check.that(false, std::format("the subject says it can, and its probe names no request that shows it: {}", what), at);
    const std::string operation = operation_of(request);
    return check.that(entry_of(subject, operation) != nullptr, std::format("{}: `{}` is not in the subject's catalog", what, operation), at);
}

std::string hex(std::uint64_t hash) { return std::format("{:016x}", hash); }

void catalog_complete(const Probe& probe, Check& check) {
    const auto subject = probe.make();
    const std::string name(subject->name());
    check.that(!name.empty() && name.find('.') == std::string::npos, "a subject has a name, one word of an address");
    check.that(subject->interface_name().find('@') != std::string_view::npos, "the interface its operations speak says its version (`name@version`)");
    const auto operations = subject->operations();
    check.that(!operations.empty(), "what it can do is its catalog: it has operations");
    std::set<std::string_view> names;
    for (const mlibs::control::CatalogEntry& entry : operations) {
        const std::string_view operation = entry.operation.name;
        check.that(operation.starts_with(name + "."), std::format("`{}` is addressed under its subject (`{}.…`)", operation, name));
        check.that(names.insert(operation).second, std::format("`{}` is listed once", operation));
        check.that(!entry.summary.empty(), std::format("`{}` says what it does", operation));
        if (entry.operation.effect == openas::Effect::read) check.that(entry.undo.empty(), std::format("`{}` reads: there is nothing to undo", operation));
        if (entry.operation.effect == openas::Effect::reversible)
            check.that(!entry.undo.empty(), std::format("`{}` can be taken back: it says how", operation));
        for (const mlibs::control::Parameter& parameter : entry.parameters)
            check.that(!parameter.name.empty() && !parameter.type.empty(), std::format("`{}`: a parameter has a name and a type", operation));
    }

    // The same, as an agent finds it out.
    SubjectSession session({subject.get()});
    const Answered who = ask(session, R"({"op":"subjects"})");
    check.that(who.exit == 0 && who.data.contains(std::format("\"name\":\"{}\"", name)), "`subjects` names it");
    const Answered described = ask(session, std::format(R"({{"op":"describe","subject":"{}"}})", name));
    check.that(described.exit == 0, "`describe` answers");
    if (auto parsed = mlibs::json::Document::parse(described.data, "record")) {
        const auto listed = parsed->root()["data"]["operations"];
        check.equal(listed.size(), operations.size(), "`describe` lists the catalog");
        for (std::size_t index = 0; index < std::min(listed.size(), operations.size()); ++index) {
            check.that(listed[index]["name"].text() == operations[index].operation.name, std::format("`describe` names `{}`", operations[index].operation.name));
            check.that(listed[index]["effect"].text() == openas::name(operations[index].operation.effect),
                       std::format("`describe` gives `{}` its declared effect", operations[index].operation.name));
        }
    }
    for (const mlibs::control::CatalogEntry& entry : operations) {
        const Answered one = ask(session, std::format(R"({{"op":"describe","operation":"{}"}})", entry.operation.name));
        check.that(one.exit == 0 && one.data.contains("\"parameters\""), std::format("`{}` is described whole when asked for", entry.operation.name));
    }
    if (const std::string_view listing = subject->listing(); !listing.empty()) {
        check.that(mlibs::json::Document::parse(listing, "listing").has_value(), "the catalog's listing is JSON");
        for (const mlibs::control::CatalogEntry& entry : operations)
            check.that(listing.contains(std::format("\"{}\"", entry.operation.name)), std::format("the listing has `{}`", entry.operation.name));
    }
    check.evidence("operations", std::to_string(operations.size()));
}

void catalog_stable(const Probe& probe, Check& check) {
    const auto first = probe.make(), second = probe.make();
    check.that(first->state_hash() != 0, "a subject has a state: its hash is not 0");
    check.equal(hex(first->state_hash()), hex(second->state_hash()), "made twice, the same state");
    SubjectSession one({first.get()}), two({second.get()});
    const std::string describe = std::format(R"({{"op":"describe","subject":"{}"}})", first->name());
    for (const std::string_view request : {std::string_view{R"({"op":"subjects"})"}, std::string_view{describe}}) {
        const Answered here = ask(one, request), there = ask(two, request);
        check.that(here.lines == there.lines, std::format("{}: the same answer from each, byte for byte", request));
        check.that(ask(one, request).lines == here.lines, std::format("{}: asked again, the same answer", request));
    }
    check.evidence("state", hex(first->state_hash()));
}

void read_pure(const Probe& probe, Check& check) {
    const auto subject = probe.make();
    if (!shown(check, probe.read, "a request that reads", *subject)) return;
    const auto* entry = entry_of(*subject, operation_of(probe.read));
    check.that(entry->operation.effect == openas::Effect::read, std::format("`{}` is declared a read", entry->operation.name));
    SubjectSession session({subject.get()});
    const std::uint64_t before = subject->state_hash();
    const Answered first = ask(session, probe.read), second = ask(session, probe.read);
    check.that(first.exit == 0 && !first.data_empty, "a reading answers its data");
    check.equal(hex(subject->state_hash()), hex(before), "a reading leaves the state as it was");
    check.that(first.lines == second.lines, "read twice with nothing between: the same answer");
}

void preview_untouched(const Probe& probe, Check& check) {
    const auto subject = probe.make();
    if (!shown(check, probe.preview, "a request that previews", *subject)) return;
    SubjectSession session({subject.get()});
    const std::uint64_t before = subject->state_hash();
    const Answered previewed = ask(session, probe.preview);
    check.that(previewed.exit == 0 && !previewed.data_empty, "a preview answers what would be");
    check.equal(hex(subject->state_hash()), hex(before), "after a preview the state is as it was");
}

void undo_returns(const Probe& probe, Check& check) {
    const auto subject = probe.make();
    if (!shown(check, probe.change, "a request that changes it", *subject) || !shown(check, probe.undo, "a request that takes the change back", *subject)) return;
    SubjectSession session({subject.get()});
    const std::uint64_t before = subject->state_hash();
    check.that(ask(session, probe.change).exit == 0, "the change runs");
    check.that(subject->state_hash() != before, "the change changed the state");
    check.that(ask(session, probe.undo).exit == 0, "taking it back runs");
    check.equal(hex(subject->state_hash()), hex(before), "taken back, the state is the one before");
}

void time_replays(const Probe& probe, Check& check) {
    const auto first = probe.make(), second = probe.make();
    if (!check.that(!probe.script.empty(), "the subject says it has time, and its probe gives no requests that play it on")) return;
    SubjectSession one({first.get()}), two({second.get()});
    const std::uint64_t before = first->state_hash();
    for (const std::string& request : probe.script) {
        if (!shown(check, request, "a request of the script", *first)) return;
        const Answered here = ask(one, request), there = ask(two, request);
        check.that(here.exit == 0, std::format("{} runs", request));
        check.that(here.lines == there.lines, std::format("{}: the same answer on each, byte for byte", request));
        check.equal(hex(first->state_hash()), hex(second->state_hash()), std::format("after {}: the same state", request));
    }
    check.that(first->state_hash() != before, "the requests played it on");
    check.evidence("state", hex(first->state_hash()));
}

void source_chain(const Probe& probe, Check& check) {
    const auto subject = probe.make();
    if (!shown(check, probe.source, "a request from something it put out to what made it", *subject)) return;
    SubjectSession session({subject.get()});
    const Answered traced = ask(session, probe.source);
    check.that(traced.exit == 0 && !traced.data_empty, "what it put out is traced to what made it: the chain is not empty");
}

inline constexpr std::string_view actor_id = "contract-cts-actor";

void actor_recorded(const Probe& probe, Check& check) {
    const auto subject = probe.make();
    if (!shown(check, probe.change, "a request that changes it", *subject) || !shown(check, probe.explain, "a request that explains the change", *subject)) return;
    SubjectSession session({subject.get()}, {.actor = {openas::Kind::agent, std::string(actor_id), {}}});
    check.that(ask(session, probe.change).exit == 0, "the change runs");
    const Answered explained = ask(session, probe.explain);
    check.that(explained.exit == 0 && explained.data.contains(actor_id), "the record of the change names who made it: the session's actor");
}

void authority_first(const Probe& probe, Check& check) {
    const auto subject = probe.make();
    const std::string_view request = subject->capabilities().control ? std::string_view{probe.change} : std::string_view{probe.read};
    if (!shown(check, request, "a request to refuse", *subject)) return;
    // A user who allows nothing of this subject.
    mlibs::control::Policy nothing;
    nothing.user.operations = std::vector<std::string>{std::format("{}.nothing-is-allowed", subject->name())};
    SubjectSession refusing({subject.get()}, {.access = std::make_shared<mlibs::control::Access>(nothing)});
    const std::uint64_t before = subject->state_hash();
    const Answered refused = ask(refusing, request);
    check.equal(refused.code, std::string(openas::error_code::refused), "what the policy refuses is answered `refused`");
    check.that(refused.exit != 0, "a refusal is not a success");
    check.that(refused.data.empty(), "a refused request answers no data");
    check.equal(hex(subject->state_hash()), hex(before), "a refused request did not run");
    // The same request with nobody refusing: it was the policy's doing.
    SubjectSession open({subject.get()});
    check.that(ask(open, request).exit == 0, "the same request, allowed, runs");
}

void observe_budget(const Probe& probe, Check& check) {
    const auto subject = probe.make();
    if (!shown(check, probe.budgeted, "a reading asked within a budget", *subject)) return;
    if (!check.that(probe.budget > 0, "the probe says the budget it asks within")) return;
    SubjectSession session({subject.get()});
    const Answered seen = ask(session, probe.budgeted);
    check.that(seen.exit == 0 && !seen.data_empty, "a reading within a budget still answers");
    check.that(seen.data_bytes <= probe.budget, std::format("the answer is within the budget: {} bytes of {}", seen.data_bytes, probe.budget));
    check.evidence("bytes", std::format("{} of {}", seen.data_bytes, probe.budget));
}

void errors_structured(const Probe& probe, Check& check) {
    const auto subject = probe.make();
    SubjectSession session({subject.get()});
    const std::uint64_t before = subject->state_hash();
    const Answered unknown = ask(session, std::format(R"({{"op":"{}.no-such-operation","re":41}})", subject->name()));
    check.equal(unknown.code, std::string(openas::error_code::not_found), "an operation it has not is `not-found`");
    check.that(!unknown.message.empty() && unknown.exit > 0, "an error says what is wrong, and its result is not a success");
    check.that(unknown.records && std::ranges::all_of(unknown.echoed, [](const std::string& re) { return re == "41"; }), "every record echoes the request's `re`");
    const Answered garbled = ask(session, "{\"op\":");
    check.equal(garbled.code, std::string(openas::error_code::invalid_argument), "a line that is no request is `invalid-argument`");
    check.that(garbled.records && garbled.exit > 0, "and is still answered in records, ending in a result");
    check.equal(hex(subject->state_hash()), hex(before), "neither touched the state");
}

void causes_explained(const Probe& probe, Check& check) {
    const auto subject = probe.make();
    if (!shown(check, probe.change, "a request that changes it", *subject) || !shown(check, probe.explain, "a request that explains the change", *subject)) return;
    SubjectSession session({subject.get()});
    check.that(ask(session, probe.change).exit == 0, "the change runs");
    const std::uint64_t after = subject->state_hash();
    const Answered explained = ask(session, probe.explain);
    check.that(explained.exit == 0 && !explained.data_empty, "a change on record is explained: its chain of causes is not empty");
    check.equal(hex(subject->state_hash()), hex(after), "explaining leaves the state as it was");
}

mlibs::cts::Suite<const Probe> build() {
    const std::string checked(contract);
    mlibs::cts::Suite<const Probe> out{"mlibs.agent.contract", "1"};
    // 1 one source: what it can do is its catalog; what can be named has a stable address.
    out.add({"agent.catalog.complete", checked, Level::semantic, {}, catalog_complete});
    out.add({"agent.catalog.stable", checked, Level::exact, {}, catalog_stable});
    // 2 transactional: a preview leaves the state; what is taken back is back.
    out.add({"agent.preview.untouched", checked, Level::exact, {"preview"}, preview_untouched});
    out.add({"agent.undo.returns", checked, Level::exact, {"control", "undo"}, undo_returns});
    // 3 deterministic: time is its own; the same requests are the same bits.
    out.add({"agent.time.replays", checked, Level::exact, {"time"}, time_replays});
    // 4 what it puts out is traced to what made it.
    out.add({"agent.source.chain", checked, Level::semantic, {"provenance"}, source_chain});
    // 5 who acted is in the model.
    out.add({"agent.actor.recorded", checked, Level::semantic, {"control", "causes"}, actor_recorded});
    // 6 authority at the core: nothing runs undecided.
    out.add({"agent.authority.first", checked, Level::exact, {}, authority_first});
    // 7 observation made for an agent: a reading is pure, and within the budget asked.
    out.add({"agent.read.pure", checked, Level::exact, {"observation"}, read_pure});
    out.add({"agent.observe.budget", checked, Level::semantic, {"observation", "budget"}, observe_budget});
    // 8 the working details: errors with a code, requests answered in records, a change explained.
    out.add({"agent.errors.structured", checked, Level::semantic, {}, errors_structured});
    out.add({"agent.causes.explained", checked, Level::semantic, {"control", "causes"}, causes_explained});
    return out;
}

}  // namespace

bool has(const Capabilities& capabilities, std::string_view capability) noexcept {
    if (capability == "observation") return capabilities.observation;
    if (capability == "budget") return capabilities.budget;
    if (capability == "control") return capabilities.control;
    if (capability == "time") return capabilities.time;
    if (capability == "causes") return capabilities.causes;
    if (capability == "provenance") return capabilities.provenance;
    if (capability == "preview") return capabilities.preview;
    if (capability == "history") return capabilities.history;
    // What it changed is taken back by its history (undo) or by its time (a seek to before it).
    if (capability == "undo") return capabilities.history || capabilities.time;
    return false;
}

const mlibs::cts::Suite<const Probe>& suite() {
    static const mlibs::cts::Suite<const Probe> all = build();
    return all;
}

mlibs::cts::Report run(const Probe& probe, mlibs::cts::Environment environment, const mlibs::cts::Policy& policy) {
    // What it cannot do is what it says it cannot: its own capabilities, asked once.
    const Capabilities capabilities = probe.make()->capabilities();
    if (environment.subject.empty()) environment.subject = std::string(probe.make()->name());
    return suite().run(probe, [&](std::string_view capability) { return has(capabilities, capability); }, policy, std::move(environment));
}

}  // namespace mlibs::agent::cts
