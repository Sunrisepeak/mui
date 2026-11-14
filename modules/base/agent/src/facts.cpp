module mlibs.agent;

import std;
import openas;
import mlibs.log.api;

namespace mlibs::agent {

namespace {
std::string actor_text(const openas::Actor& actor) { return actor.name.empty() ? std::string(openas::name(actor.kind)) : std::format("{}:{}", openas::name(actor.kind), actor.name); }
bool ended_well(const Fact& fact) { return fact.outcome == "done"; }
}  // namespace

std::string line_of(const Fact& fact) {
    return std::format("{} {} → {}{}{}", actor_text(fact.actor), fact.operation, fact.outcome, fact.text.empty() ? "" : ": ", fact.text);
}

std::string json_of(const Fact& fact) {
    std::string out = std::format("{{\"fact\":{},\"subject\":{},\"operation\":{},\"effect\":{},\"by\":{{\"kind\":{},\"name\":{}}},\"outcome\":{}", fact.id, json_text(fact.subject),
                                  json_text(fact.operation), json_text(openas::name(fact.effect)), json_text(openas::name(fact.actor.kind)), json_text(fact.actor.name),
                                  json_text(fact.outcome));
    if (!fact.text.empty()) out += std::format(",\"text\":{}", json_text(fact.text));
    return out + std::format(",\"request\":{}}}", fact.request.empty() ? std::string_view("null") : std::string_view(fact.request));
}

std::uint64_t Facts::record(Fact fact) {
    if (fact.id == 0) fact.id = ++last_;
    const std::uint64_t id = fact.id;
    const std::string category = fact.subject.empty() ? std::string("agent") : std::format("agent.{}", fact.subject);
    // A request that failed is an agent's ordinary traffic, not the program's trouble: info, as one that was done.
    mlibs::log::log(mlibs::log::Level::info, category, line_of(fact), std::source_location::current(), fact.id);
    if (capacity_ == 0) return id;
    if (kept_.size() >= capacity_) kept_.pop_front();
    // In the order of their numbers: one answered later is recorded after those that came after it.
    auto at = kept_.end();
    while (at != kept_.begin() && std::prev(at)->id > id) --at;
    kept_.insert(at, std::move(fact));
    return id;
}

std::vector<Fact> Facts::read(const Asked& asked) const {
    std::vector<Fact> out;
    for (auto fact = kept_.rbegin(); fact != kept_.rend() && out.size() < asked.limit && fact->id > asked.since; ++fact) {
        if (!asked.subject.empty() && fact->subject != asked.subject) continue;
        if (asked.failed && ended_well(*fact)) continue;
        out.push_back(*fact);
    }
    std::ranges::reverse(out);
    return out;
}

}  // namespace mlibs::agent
