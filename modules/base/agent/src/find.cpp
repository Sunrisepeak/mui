module mlibs.agent;

import std;
import mlibs.control;

namespace mlibs::agent {

namespace {

// Words that say nothing of what is wanted.
constexpr std::array<std::string_view, 44> said_everywhere{"a",   "an",  "the",  "to",   "of",   "in",    "on",   "at",   "it",   "its",  "is",
                                                            "are", "be",  "and",  "or",   "i",    "me",    "my",   "we",   "you",  "that", "this",
                                                            "what", "how", "can",  "do",   "does", "with",  "for",  "from", "by",   "as",   "want",
                                                            "need", "make", "some", "there", "here", "which", "would", "should", "something", "thing", "have"};

// A word as it is looked up: lowercase, without the endings that only say how many or when (places, placed,
// placing, place → plac). Crude on purpose: the same rule on both sides, no table of a language.
std::string stem(std::string word) {
    const auto ends = [&](std::string_view ending) { return word.size() > ending.size() + 2 && word.ends_with(ending); };
    if (ends("s") && !word.ends_with("ss")) word.pop_back();
    if (ends("ing")) word.resize(word.size() - 3);
    else if (ends("ed")) word.resize(word.size() - 2);
    if (ends("e")) word.pop_back();
    return word;
}

// The words of a text, each once: runs of letters and digits, lowercase, without those said everywhere.
std::vector<std::string> words(std::string_view text) {
    std::vector<std::string> out;
    std::string word;
    const auto close = [&] {
        if (!word.empty() && !std::ranges::contains(said_everywhere, std::string_view(word))) {
            std::string stemmed = stem(std::move(word));
            if (!std::ranges::contains(out, stemmed)) out.push_back(std::move(stemmed));
        }
        word.clear();
    };
    for (const char raw : text) {
        const auto ch = static_cast<unsigned char>(raw);
        if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) word += raw;
        else if (ch >= 'A' && ch <= 'Z') word += static_cast<char>(ch - 'A' + 'a');
        else if (ch >= 0x80) word += raw;  // other scripts: kept as written
        else close();
    }
    close();
    return out;
}

// What can be found: its words by where they stand (its name weighs most, then what it says, then the rest).
struct Indexed {
    Found                    found;
    std::vector<std::string> name, text, more;
};

}  // namespace

std::vector<Found> find(std::span<Subject* const> subjects, std::string_view query, std::size_t limit) {
    std::vector<Indexed> all;
    for (const Subject* subject : subjects) {
        const std::string owner(subject->name());
        for (const mlibs::control::CatalogEntry& entry : subject->operations()) {
            std::string more;
            for (const mlibs::control::Parameter& parameter : entry.parameters) more += std::format("{} {} ", parameter.name, parameter.doc);
            more += entry.result;
            more += ' ';
            more += entry.keywords;
            const std::string name(entry.operation.name);
            all.push_back({{.kind = "operation",
                            .subject = owner,
                            .name = name,
                            .text = std::string(entry.summary),
                            .requests = entry.examples.empty() ? std::vector<std::string>{std::format("{{\"op\":\"describe\",\"operation\":{}}}", json_text(name))}
                                                               : std::vector<std::string>{std::string(entry.examples.front())}},
                           words(name), words(entry.summary), words(more)});
        }
        for (const mlibs::control::Task& task : subject->guide().tasks)
            all.push_back({{.kind = "task", .subject = owner, .name = std::string(task.name), .text = std::string(task.summary), .requests = {task.steps.begin(), task.steps.end()}},
                           words(task.name), words(task.summary), {}});
        for (Term& term : subject->terms()) {
            std::vector<std::string> name = words(term.name), text = words(term.text);
            all.push_back({{.kind = std::move(term.kind), .subject = owner, .name = std::move(term.name), .text = std::move(term.text), .requests = {std::move(term.request)}},
                           std::move(name), std::move(text), {}});
        }
    }
    // A word found in few weighs more than one found in many: whole numbers, the same on every machine.
    const std::vector<std::string> wanted = words(query);
    std::vector<std::uint64_t> rarity(wanted.size(), 0);
    for (std::size_t index = 0; index < wanted.size(); ++index) {
        std::size_t holders = 0;
        for (const Indexed& each : all)
            if (std::ranges::contains(each.name, wanted[index]) || std::ranges::contains(each.text, wanted[index]) || std::ranges::contains(each.more, wanted[index])) ++holders;
        if (holders) rarity[index] = 1024u * all.size() / holders;
    }
    // What has more of the words wanted answers more of the intent: the sum, times how many it has.
    for (Indexed& each : all) {
        std::uint64_t has = 0;
        for (std::size_t index = 0; index < wanted.size(); ++index) {
            const std::uint64_t where =
                std::ranges::contains(each.name, wanted[index]) ? 4u : std::ranges::contains(each.text, wanted[index]) ? 2u : std::ranges::contains(each.more, wanted[index]) ? 1u : 0u;
            each.found.score += where * rarity[index];
            has += where != 0;
        }
        each.found.score *= has;
    }
    std::erase_if(all, [](const Indexed& each) { return each.found.score == 0; });
    // Of equals, the first as the subjects list them.
    std::ranges::stable_sort(all, std::ranges::greater{}, [](const Indexed& each) { return each.found.score; });
    std::vector<Found> out;
    for (std::size_t index = 0; index < std::min(all.size(), limit); ++index) out.push_back(std::move(all[index].found));
    return out;
}

}  // namespace mlibs::agent
