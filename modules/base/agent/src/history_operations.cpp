// The history's operations over a subject's sources (mlibs.agent:history; D-151, D-160).
module mlibs.agent;

import std;
import mlibs.json;
import openas;

namespace mlibs::agent {

namespace {

using Texts = std::map<std::string, std::string>;

// A JSON object with more members after its own.
std::string with(std::string object, std::string_view members) {
    while (!object.empty() && (object.back() == ' ' || object.back() == '\n')) object.pop_back();
    if (object.size() < 2 || object.front() != '{' || object.back() != '}') object = std::format("{{\"changes\":{}}}", object.empty() ? std::string("null") : object);
    object.pop_back();
    return std::format("{}{}{}}}", object, object.size() > 1 ? "," : "", members);
}

std::string text_of(mlibs::json::Node parameters, std::string_view key, std::string_view otherwise = {}) { return parameters[key].text().value_or(std::string(otherwise)); }

}  // namespace

LineChange changed_lines(std::string_view before, std::string_view after) {
    const auto lines = [](std::string_view text) {
        std::vector<std::string_view> out;
        for (const auto line : std::views::split(text, '\n')) out.emplace_back(line.begin(), line.end());
        return out;
    };
    const auto old_lines = lines(before), new_lines = lines(after);
    std::size_t head = 0, tail = 0;
    while (head < old_lines.size() && head < new_lines.size() && old_lines[head] == new_lines[head]) ++head;
    while (tail + head < old_lines.size() && tail + head < new_lines.size() && old_lines[old_lines.size() - 1 - tail] == new_lines[new_lines.size() - 1 - tail]) ++tail;
    const auto joined = [](std::span<const std::string_view> part) {
        std::string out;
        for (const auto line : part) out += std::format("{}{}", out.empty() ? "" : "\n", line);
        return out;
    };
    return {head + 1, joined(std::span(old_lines).subspan(head, old_lines.size() - head - tail)), joined(std::span(new_lines).subspan(head, new_lines.size() - head - tail))};
}

std::string commit_text(const Commit& commit, const Commit* against) {
    std::string out = std::format("{{\"id\":{},\"message\":{},\"actor\":{},\"branch\":{},\"time\":{},\"state\":{},\"parents\":[", json_text(commit.id), json_text(commit.message),
                                  json_text(commit.actor), json_text(commit.branch), commit.time, json_text(commit.state));
    for (std::size_t index = 0; index < commit.parents.size(); ++index) out += (index ? "," : "") + json_text(commit.parents[index]);
    out += against ? "],\"changed\":[" : "],\"files\":[";
    bool first = true;
    for (const auto& [path, object] : commit.files) {
        if (against)
            if (const auto was = against->files.find(path); was != against->files.end() && was->second == object) continue;
        out += (first ? "" : ",") + json_text(path), first = false;
    }
    if (against)
        for (const auto& [path, object] : against->files)
            if (!commit.files.contains(path)) out += (first ? "" : ",") + json_text(path), first = false;
    return out + "]}";
}

std::string HistoryOperations::said(const Commit& commit) const {
    if (!by_change) return commit_text(commit);
    static const Commit nothing;
    const auto parent = commit.parents.empty() ? std::expected<Commit, std::string>(nothing) : history_.commit_of(commit.parents.front());
    return commit_text(commit, parent ? &*parent : &nothing);
}

std::string HistoryOperations::shown_now() const {
    if (!shown_commit.empty()) return shown_commit;
    const auto head = history_.resolve("HEAD");
    return head ? *head : std::string();
}

std::expected<std::string, std::string> HistoryOperations::run(std::string_view operation, mlibs::json::Node parameters, std::string_view actor) {
    const auto refuse = [this](std::string why, std::string_view as = openas::error_code::invalid_argument) {
        refused_as = as;
        return std::unexpected(std::move(why));
    };
    const std::string_view not_found = openas::error_code::not_found, failed = openas::error_code::failed, theirs_to_say = {};
    // What is shown, as texts: the commit seen, else the branch's last.
    const auto shown_texts = [&] { return history_.texts_of(shown_now().empty() ? std::string("HEAD") : shown_now()); };
    const auto optional_at = [&](std::optional<std::string>& kept) -> std::optional<std::string_view> {
        kept = parameters["at"].text();
        return kept ? std::optional<std::string_view>(*kept) : std::nullopt;
    };

    if (operation == "history") {
        const std::string branch = text_of(parameters, "branch", history_.head_branch());
        const auto limit = static_cast<std::size_t>(std::max<double>(0, parameters["limit"].number().value_or(50)));
        std::string out = std::format("{{\"head\":{},\"branches\":{{", json_text(history_.head_branch()));
        bool first = true;
        for (const auto& [name, id] : history_.branches()) out += std::format("{}{}:{}", first ? "" : ",", json_text(name), json_text(id)), first = false;
        out += "},\"tags\":{";
        first = true;
        for (const auto& [name, id] : history_.tags()) out += std::format("{}{}:{}", first ? "" : ",", json_text(name), json_text(id)), first = false;
        out += "},\"log\":[";
        first = true;
        for (const Commit& commit : history_.log(branch, limit)) out += (first ? "" : ",") + said(commit), first = false;
        const auto shown = shown_texts();
        return out + std::format("],\"uncommitted\":{}}}", !shown || *shown != sources_.texts());
    }
    if (operation == "commit") {
        const std::string message = text_of(parameters, "message");
        if (message.empty()) return refuse("a commit says what it is: `message`");
        auto commit = history_.commit(sources_.texts(), message, std::string(actor), sources_.state());
        if (!commit) return refuse(commit.error(), failed);
        return said(*commit);
    }
    if (operation == "branch") {
        const std::string name = text_of(parameters, "name");
        std::optional<std::string> at;
        auto made = history_.branch(name, optional_at(at));
        if (!made) return refuse(made.error());
        return std::format("{{\"branch\":{},\"at\":{}}}", json_text(name), json_text(*made));
    }
    if (operation == "switch") {
        const std::string to = text_of(parameters, "to");
        // Changes not committed: what runs is not the commit shown (the branch's last, or one seen).
        const auto shown = shown_texts();
        if (shown && *shown != sources_.texts() && !parameters["force"].boolean().value_or(false))
            return refuse(std::format("the design has changes not committed: {} them, or switch with force (they are dropped)", names_.commit), failed);
        const auto branches = history_.branches();
        auto at = history_.resolve(to);
        if (!at) return refuse(std::format("{} ({} lists the branches and tags)", at.error(), names_.list), not_found);
        auto texts = history_.texts_of(*at);
        if (!texts) return refuse(texts.error(), failed);
        auto applied = sources_.apply(*texts, Sources::Reason::switch_to);
        if (!applied) return refuse(applied.error(), theirs_to_say);
        if (!branches.contains(to)) {  // a commit, seen: the branch stays; a commit keeps it there, a branch made of it is one
            shown_commit = *at;
            return with(std::move(*applied), std::format("\"branch\":{},\"at\":{},\"detached\":true", json_text(history_.head_branch()), json_text(*at)));
        }
        shown_commit.clear();
        (void)history_.switch_to(to);
        return with(std::move(*applied), std::format("\"branch\":{},\"at\":{}", json_text(to), json_text(branches.at(to))));
    }
    if (operation == "revert") {
        auto id = history_.resolve(text_of(parameters, "to"));
        if (!id) return refuse(std::format("{} ({} lists the branches and tags)", id.error(), names_.list), not_found);
        auto texts = history_.texts_of(*id);
        if (!texts) return refuse(texts.error(), failed);
        auto applied = sources_.apply(*texts, Sources::Reason::undo);
        if (!applied) return refuse(applied.error(), theirs_to_say);
        auto commit = history_.commit(sources_.texts(), std::format("back to {}", id->substr(0, 8)), std::string(actor), sources_.state());
        shown_commit.clear();
        return commit ? with(std::move(*applied), std::format("\"commit\":{}", said(*commit))) : *applied;
    }
    if (operation == "diff") {
        const std::string from = text_of(parameters, "from", "HEAD");
        auto before = history_.texts_of(from);
        if (!before) return refuse(before.error(), not_found);
        const auto to = parameters["to"].text();
        Texts after;
        if (to) {
            auto texts = history_.texts_of(*to);
            if (!texts) return refuse(texts.error(), not_found);
            after = std::move(*texts);
        } else {
            after = sources_.texts();  // what runs now
        }
        std::set<std::string> paths;
        for (const auto& [path, text] : *before) paths.insert(path);
        for (const auto& [path, text] : after) paths.insert(path);
        std::string out = std::format("{{\"from\":{},\"to\":{},\"files\":[", json_text(from), json_text(to.value_or("running")));
        bool first = true;
        for (const auto& path : paths) {
            const std::string old_text = before->contains(path) ? before->at(path) : std::string();
            const std::string new_text = after.contains(path) ? after.at(path) : std::string();
            if (old_text == new_text) continue;
            const LineChange change = changed_lines(old_text, new_text);
            out += std::format("{}{{\"line\":{},\"before\":{},\"after\":{},\"path\":{}}}", first ? "" : ",", change.line, json_text(change.before), json_text(change.after), json_text(path));
            first = false;
        }
        return out + "]}";
    }
    if (operation == "merge") {
        const std::string from = text_of(parameters, "from");
        auto theirs_id = history_.resolve(from);
        auto ours_id = history_.resolve("HEAD");
        if (!theirs_id) return refuse(std::format("{} ({} lists the branches and tags)", theirs_id.error(), names_.list), not_found);
        if (!ours_id) return refuse("nothing committed on this branch to merge into", failed);
        const auto base_id = history_.common_ancestor("HEAD", *theirs_id);
        auto ours = history_.texts_of(*ours_id), theirs = history_.texts_of(*theirs_id);
        if (!ours || !theirs) return refuse(!ours ? ours.error() : theirs.error(), failed);
        Texts base;
        if (base_id)
            if (auto texts = history_.texts_of(*base_id)) base = std::move(*texts);
        Texts merged;
        std::string conflicts;
        std::set<std::string> paths;
        for (const auto& [path, text] : *ours) paths.insert(path);
        for (const auto& [path, text] : *theirs) paths.insert(path);
        for (const auto& path : paths) {
            const std::string base_text = base.contains(path) ? base.at(path) : std::string();
            // A source a side has not is one it removed (or never had): its text there is none.
            const bool in_ours = ours->contains(path), in_theirs = theirs->contains(path);
            const std::string our_text = in_ours ? ours->at(path) : std::string();
            const std::string their_text = in_theirs ? theirs->at(path) : std::string();
            const TextMerge result = merge_text(base_text, our_text, their_text);
            if (result.conflict) conflicts += (conflicts.empty() ? "" : ",") + json_text(path);
            if (!result.text.empty() || (in_ours && in_theirs)) merged.emplace(path, result.text);
        }
        if (!conflicts.empty())
            return refuse(std::format("both branches changed the same lines of [{}]: nothing merged — {} one side to agree, commit, and merge again", conflicts, names_.edit), "conflict");
        auto applied = sources_.apply(merged, Sources::Reason::edit);
        if (!applied) return refuse(applied.error(), theirs_to_say);
        auto commit = history_.commit(sources_.texts(), std::format("merge {}", from), std::string(actor), sources_.state(), std::vector<std::string>{*ours_id, *theirs_id});
        shown_commit.clear();
        return commit ? with(std::move(*applied), std::format("\"commit\":{}", said(*commit))) : *applied;
    }
    if (operation == "tag") {
        const std::string name = text_of(parameters, "name");
        std::optional<std::string> at;
        auto tagged = history_.tag(name, optional_at(at));
        if (!tagged) return refuse(tagged.error());
        return std::format("{{\"tag\":{},\"at\":{}}}", json_text(name), json_text(*tagged));
    }
    return refuse(std::format("the history has no operation `{}`", operation));
}

}  // namespace mlibs::agent
