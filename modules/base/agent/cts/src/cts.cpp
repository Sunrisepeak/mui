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
    std::vector<std::string> next;           // an error record's ways out: each one's request, as JSON
    bool                     next_whole = true;  // … and each said why
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
            for (const auto& way : root["next"].items()) {
                out.next.push_back(way["request"].dump());
                if (way["why"].text().value_or("").empty() || !way["request"].is_object()) out.next_whole = false;
            }
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

// 9 it guides itself: what it is and the tasks to begin with are its catalog's, and run as they are written.
void guide_runs(const Probe& probe, Check& check) {
    const auto subject = probe.make();
    const mlibs::control::Guide guide = subject->guide();
    check.that(!guide.about.empty(), "it says what it is, in a line (the catalog's doc)");
    check.that(!guide.tasks.empty(), "it has tasks to begin with (the catalog's tasks)");
    {
        SubjectSession session({subject.get()});
        const Answered described = ask(session, std::format(R"({{"op":"describe","subject":"{}"}})", subject->name()));
        if (auto parsed = mlibs::json::Document::parse(described.data, "record")) {
            const auto data = parsed->root()["data"];
            check.that(data["about"].text() == guide.about, "`describe` says what it is");
            check.equal(data["tasks"].size(), guide.tasks.size(), "`describe` gives the tasks to begin with");
        }
    }
    std::size_t steps = 0;
    for (const mlibs::control::Task& task : guide.tasks) {
        check.that(!task.name.empty() && !task.summary.empty() && !task.steps.empty(), std::format("task `{}` says what it gets done and has the requests that do it", task.name));
        // Each task from the beginning: on a subject as it is made.
        const auto fresh = probe.make();
        SubjectSession session({fresh.get()});
        for (const std::string_view step : task.steps) {
            ++steps;
            if (!check.that(entry_of(*fresh, operation_of(step)) != nullptr, std::format("task `{}`: {} is a request of the catalog", task.name, step))) continue;
            const Answered answered = ask(session, step);
            check.that(answered.exit == 0, std::format("task `{}`: {} runs as written ({} {})", task.name, step, answered.code, answered.message));
        }
    }
    std::size_t examples = 0;
    for (const mlibs::control::CatalogEntry& entry : subject->operations()) {
        const bool needs = std::ranges::any_of(entry.parameters, [](const mlibs::control::Parameter& parameter) { return !parameter.optional; });
        if (needs) check.that(!entry.examples.empty(), std::format("`{}` needs arguments: it shows a request of it (an example, or a step of a task)", entry.operation.name));
        for (const std::string_view example : entry.examples) {
            ++examples;
            check.that(operation_of(example) == entry.operation.name, std::format("`{}`: its example is a request of it", entry.operation.name));
            const auto fresh = probe.make();
            SubjectSession session({fresh.get()});
            const Answered answered = ask(session, example);
            // Well formed, whatever it is sent to: it may name a node, a handle or a place this subject has not
            // (`not-found`, a precondition), and is never a request that could not be accepted anywhere.
            check.that(answered.code != openas::error_code::invalid_argument,
                       std::format("`{}`: {} is well formed ({} {})", entry.operation.name, example, answered.code, answered.message));
        }
    }
    check.evidence("tasks", std::to_string(guide.tasks.size()));
    check.evidence("steps", std::to_string(steps));
    check.evidence("examples", std::to_string(examples));
}

// 10 every error carries a way out: requests to send as they are.
void errors_next(const Probe& probe, Check& check) {
    const auto subject = probe.make();
    SubjectSession session({subject.get()});
    const std::uint64_t before = subject->state_hash();
    // What `next` holds is sent as it is: a session's own request runs; any other is a request of the catalog.
    const auto leads = [&](const Answered& answered, std::string_view what) {
        if (!check.that(!answered.next.empty() && answered.next_whole, std::format("{}: the error carries its ways out (`next`), each with why", what))) return;
        for (const std::string& request : answered.next) {
            const std::string operation = operation_of(request);
            if (operation == "describe" || operation == "subjects")
                check.that(ask(session, request).exit == 0, std::format("{}: {} runs as given", what, request));
            else
                check.that(entry_of(*subject, operation) != nullptr, std::format("{}: {} is a request of the catalog", what, request));
        }
    };
    leads(ask(session, "{\"op\":"), "a line that is no request");
    leads(ask(session, R"({"op":"describe","subject":"nobody-here"})"), "a subject that is not here");
    std::size_t misspelt = 0, lacking = 0;
    for (const mlibs::control::CatalogEntry& entry : subject->operations()) {
        const std::string_view name = entry.operation.name;
        // A letter short of its name, when that is no other operation's.
        if (const std::string_view near = name.substr(0, name.size() - 1); !near.ends_with('.') && entry_of(*subject, near) == nullptr) {
            const Answered answered = ask(session, std::format(R"({{"op":"{}"}})", near));
            check.equal(answered.code, std::string(openas::error_code::not_found), std::format("`{}` is no operation", near));
            leads(answered, near);
            check.that(std::ranges::any_of(answered.next, [&](const std::string& request) { return request.contains(std::format("\"{}\"", name)); }),
                       std::format("`{}`: the ways out name `{}`", near, name));
            ++misspelt;
        }
        // Without what it needs.
        if (std::ranges::any_of(entry.parameters, [](const mlibs::control::Parameter& parameter) { return !parameter.optional; })) {
            const Answered answered = ask(session, std::format(R"({{"op":"{}"}})", name));
            check.equal(answered.code, std::string(openas::error_code::invalid_argument), std::format("`{}` without what it needs is `invalid-argument`", name));
            leads(answered, std::format("`{}` without what it needs", name));
            ++lacking;
        }
    }
    check.equal(hex(subject->state_hash()), hex(before), "none of them touched the state");
    check.evidence("misspelt", std::to_string(misspelt));
    check.evidence("lacking", std::to_string(lacking));
}

// 12 what it can do is found by intent: asked for in its own words, each operation and task is among the first.
void find_own(const Probe& probe, Check& check) {
    const auto subject = probe.make();
    SubjectSession session({subject.get()});
    const std::uint64_t before = subject->state_hash();
    // Where `name` stands in what a query finds (from 1; 0: not among them), and that each has requests.
    const auto place = [&](std::string_view query, std::string_view kind, std::string_view name) -> std::size_t {
        const Answered answered = ask(session, std::format(R"({{"op":"find","q":{},"limit":3}})", json_text(query)));
        auto parsed = mlibs::json::Document::parse(answered.data, "record");
        if (!check.that(answered.exit == 0 && parsed.has_value(), std::format("`find` answers ({})", query))) return 0;
        const auto found = parsed->root()["data"]["found"];
        std::size_t at = 0;
        for (std::size_t index = 0; index < found.size(); ++index) {
            check.that(found[index]["requests"].size() > 0 && !found[index]["summary"].text().value_or("").empty(), std::format("what `find` gives has a line and requests ({})", query));
            if (!at && found[index]["kind"].text() == kind && found[index]["name"].text() == name) at = index + 1;
        }
        return at;
    };
    std::size_t first = 0, asked = 0;
    for (const mlibs::control::CatalogEntry& entry : subject->operations()) {
        const std::size_t at = place(entry.summary, "operation", entry.operation.name);
        check.that(at != 0, std::format("`{}` is among the first three found by what it says it does", entry.operation.name));
        first += at == 1;
        ++asked;
    }
    for (const mlibs::control::Task& task : subject->guide().tasks) {
        const std::size_t at = place(task.summary, "task", task.name);
        check.that(at != 0, std::format("task `{}` is among the first three found by what it gets done", task.name));
        first += at == 1;
        ++asked;
    }
    // What its content names is found by name, and leads to a request of the catalog.
    const std::vector<Term> terms = subject->terms();
    for (std::size_t index = 0; index < std::min<std::size_t>(terms.size(), 8); ++index) {
        check.that(place(terms[index].name, terms[index].kind, terms[index].name) != 0, std::format("{} `{}` is found by its name", terms[index].kind, terms[index].name));
        check.that(entry_of(*subject, operation_of(terms[index].request)) != nullptr, std::format("{} `{}` leads to a request of the catalog", terms[index].kind, terms[index].name));
    }
    // Nothing found says so, and where to look.
    const Answered nothing = ask(session, R"({"op":"find","q":"zzqxv wwkpj"})");
    if (auto parsed = mlibs::json::Document::parse(nothing.data, "record"); check.that(nothing.exit == 0 && parsed.has_value(), "words nothing here has are answered, not failed")) {
        const auto data = parsed->root()["data"];
        check.that(data["found"].size() == 0 && data["next"].size() > 0, "nothing found, and where to look instead (`next`)");
        for (const auto& way : data["next"].items()) check.that(ask(session, way["request"].dump()).exit == 0, std::format("{} runs as given", way["request"].dump()));
    }
    const Answered lacking = ask(session, R"({"op":"find"})");
    check.that(lacking.code == openas::error_code::invalid_argument && !lacking.next.empty(), "`find` without what is wanted is `invalid-argument`, with a way out");
    check.equal(hex(subject->state_hash()), hex(before), "finding touched nothing");
    check.evidence("asked", std::to_string(asked));
    check.evidence("first", std::to_string(first));
    check.evidence("terms", std::to_string(terms.size()));
}

struct Entry {
    std::string_view              id;
    Level                         level;
    std::vector<std::string_view> needs;
    void (*body)(const Probe&, Check&);
};

// The eight properties and the norms over them (D-186: 9 it guides itself, 10 every error carries a way out, 12 found by intent), as cases.
const std::vector<Entry>& entries() {
    static const std::vector<Entry> all{
        // 1 one source: what it can do is its catalog; what can be named has a stable address.
        {"agent.catalog.complete", Level::semantic, {}, catalog_complete},
        {"agent.catalog.stable", Level::exact, {}, catalog_stable},
        // 2 transactional: a preview leaves the state; what is taken back is back.
        {"agent.preview.untouched", Level::exact, {"preview"}, preview_untouched},
        {"agent.undo.returns", Level::exact, {"control", "undo"}, undo_returns},
        // 3 deterministic: time is its own; the same requests are the same bits.
        {"agent.time.replays", Level::exact, {"time"}, time_replays},
        // 4 what it puts out is traced to what made it.
        {"agent.source.chain", Level::semantic, {"provenance"}, source_chain},
        // 5 who acted is in the model.
        {"agent.actor.recorded", Level::semantic, {"control", "causes"}, actor_recorded},
        // 6 authority at the core: nothing runs undecided.
        {"agent.authority.first", Level::exact, {}, authority_first},
        // 7 observation made for an agent: a reading is pure, and within the budget asked.
        {"agent.read.pure", Level::exact, {"observation"}, read_pure},
        {"agent.observe.budget", Level::semantic, {"observation", "budget"}, observe_budget},
        // 8 the working details: errors with a code, requests answered in records, a change explained.
        {"agent.errors.structured", Level::semantic, {}, errors_structured},
        {"agent.causes.explained", Level::semantic, {"control", "causes"}, causes_explained},
        // 9 it guides itself (D-186): what it is and the tasks to begin with, each running as written.
        {"agent.guide.runs", Level::semantic, {}, guide_runs},
        // 10 every error carries a way out.
        {"agent.errors.next", Level::semantic, {}, errors_next},
        // 12 what it can do is found by intent.
        {"agent.find.own", Level::semantic, {}, find_own},
    };
    return all;
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

std::vector<std::string> unsupported(const Capabilities& capabilities) {
    std::vector<std::string> out;
    for (const Entry& entry : entries())
        if (std::ranges::any_of(entry.needs, [&](std::string_view need) { return !has(capabilities, need); })) out.emplace_back(entry.id);
    return out;
}

std::size_t case_count() noexcept { return entries().size(); }

void declare(const Probe& probe, mlibs::cts::Against who, std::source_location at) {
    // What it cannot do is what it says it cannot: its own capabilities, asked once.
    const Capabilities capabilities = probe.make()->capabilities();
    if (who.subject.empty()) who.subject = std::string(probe.make()->name());
    if (!who.has) who.has = [capabilities](std::string_view capability) { return has(capabilities, capability); };
    mlibs::cts::against(
        std::move(who),
        [&probe, at] {
            for (const Entry& entry : entries())
                mlibs::cts::declare({.id = std::string(entry.id), .contract = std::string(contract), .level = entry.level,
                                     .needs = {entry.needs.begin(), entry.needs.end()},
                                     .body = [&probe, body = entry.body](Check& check) { body(probe, check); }},
                                    at);
        },
        at);
}

}  // namespace mlibs::agent::cts
