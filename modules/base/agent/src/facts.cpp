module mlibs.agent;

import std;
import openas;
import mlibs.json;
import mlibs.log.api;
import mlibs.trace.api;

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
    if (!fact.at.empty()) out += std::format(",\"at\":{}", fact.at);
    return out + std::format(",\"request\":{}}}", fact.request.empty() ? std::string_view("null") : std::string_view(fact.request));
}

std::shared_ptr<Facts> Facts::kept_in(std::filesystem::path file, std::size_t capacity) {
    auto book = std::make_shared<Facts>(capacity);
    book->file_ = std::move(file);
    std::ifstream in(book->file_, std::ios::binary);
    for (std::string line; std::getline(in, line);) {
        auto parsed = mlibs::json::Document::parse(line, "fact");
        if (!parsed || !parsed->root().is_object()) continue;
        const auto root = parsed->root();
        const auto number = root["fact"].integer();
        if (!number || *number <= 0) continue;
        Fact fact{.id = static_cast<std::uint64_t>(*number),
                  .subject = root["subject"].text().value_or(""),
                  .operation = root["operation"].text().value_or(""),
                  .effect = openas::parse_effect(root["effect"].text().value_or("")).value_or(openas::Effect::read),
                  .actor = openas::parse(std::format("{}:{}", root["by"]["kind"].text().value_or("agent"), root["by"]["name"].text().value_or("")))
                               .value_or(openas::Actor{openas::Kind::agent, root["by"]["name"].text().value_or(""), {}}),
                  .outcome = root["outcome"].text().value_or(""),
                  .text = root["text"].text().value_or(""),
                  .request = root["request"].is_null() ? std::string() : root["request"].dump(),
                  .at = root["at"] ? root["at"].dump() : std::string()};
        book->last_ = std::max(book->last_, fact.id);
        if (capacity == 0) continue;
        if (book->kept_.size() >= capacity) book->kept_.pop_front();
        book->kept_.push_back(std::move(fact));
    }
    return book;
}

std::uint64_t Facts::record(Fact fact) {
    if (fact.id == 0) fact.id = ++last_;
    const std::uint64_t id = fact.id;
    const std::string category = fact.subject.empty() ? std::string("agent") : std::format("agent.{}", fact.subject);
    // A request that failed is an agent's ordinary traffic, not the program's trouble: info, as one that was done.
    mlibs::log::log(mlibs::log::Level::info, category, line_of(fact), std::source_location::current(), fact.id);
    // … and, while a trace is being taken, an instant in it with the same number.
    if (mlibs::trace::enabled()) mlibs::trace::fact(mlibs::trace::intern(fact.operation), id, "agent");
    if (!file_.empty()) {
        std::error_code ignored;
        std::filesystem::create_directories(file_.parent_path(), ignored);
        if (std::ofstream out(file_, std::ios::binary | std::ios::app); out) out << json_of(fact) << '\n';
    }
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
