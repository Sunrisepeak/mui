// A subject's history as a store of its own (mlibs.agent:history): states as commits of every source's text,
// on branches, with tags, addressed by content — with no interface and no application. And two texts' merge by
// lines, and the book of claims.
import std;
import mlibs.agent;
import mlibs.json;
import mlibs.testing;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
using namespace mlibs::testing;
namespace agent = mlibs::agent;

namespace {

using Texts = std::map<std::string, std::string>;

std::filesystem::path fresh(std::string_view name) {
    const auto directory = scratch_dir(name) / "history";
    std::error_code error;
    std::filesystem::remove_all(directory, error);
    return directory;
}

}  // namespace

UTest cases{"agent.history", [] {
    "commits by content: the same texts are no commit; a commit's texts come back; ids, prefixes and `~n` name commits"_test = [] {
        agent::History history(fresh("agent-history-commits"));
        expect(eq(history.head_branch(), std::string("main")) && !history.resolve("HEAD")) << "before the first commit: main, and nothing to name";
        const Texts one{{"materials/wood.json", "{\"ignition\":570}"}, {"scene.json", "[]"}};
        const auto first = history.commit(one, "wood", "agent:a", "0011", std::nullopt, R"({"added":["wood"]})");
        expect(fatal(first.has_value())) << (first ? "" : first.error());
        expect(eq(first->branch, std::string("main")) && first->parents.empty() && eq(first->state, std::string("0011")) && first->time > 0);
        const auto again = history.commit(one, "the same", "agent:a", "0011");
        expect(!again && again.error().contains("nothing changed")) << "the same texts are no commit";
        Texts two = one;
        two["materials/wood.json"] = "{\"ignition\":600}";
        const auto second = history.commit(two, "harder to light", "person", "0022");
        expect(fatal(second.has_value()));
        expect(second->parents == std::vector<std::string>{first->id}) << "on the branch's last";
        expect(second->files.at("scene.json") == first->files.at("scene.json") && second->files.at("materials/wood.json") != first->files.at("materials/wood.json"))
            << "a text that did not change is the same object";
        // Read back: what was kept, as it was said.
        const auto read = history.commit_of(first->id);
        expect(fatal(read.has_value()));
        expect(eq(read->message, std::string("wood")) && eq(read->actor, std::string("agent:a")) && eq(read->state, std::string("0011")) && eq(read->changes, std::string(R"({"added":["wood"]})")));
        expect(history.texts_of(first->id) == std::expected<Texts, std::string>(one) && history.texts_of("HEAD") == std::expected<Texts, std::string>(two));
        // Names: HEAD, the branch, an id, a prefix only one has, `~n` along first parents.
        expect(history.resolve("HEAD") == std::expected<std::string, std::string>(second->id) && history.resolve("main~1") == std::expected<std::string, std::string>(first->id));
        expect(history.resolve(first->id.substr(0, 8)) == std::expected<std::string, std::string>(first->id));
        expect(!history.resolve("main~2") && history.resolve("main~2").error().contains("past the first commit"));
        expect(!history.resolve("nothing") && !history.commit_of("0000000000000000"));
        const auto log = history.log("main");
        expect(eq(log.size(), std::size_t{2}) && eq(log[0].id, second->id) && eq(log[1].id, first->id)) << "newest first";
        // Another store over the same directory reads the same: nothing is held in memory.
        const agent::History reopened(history.directory());
        expect(reopened.resolve("HEAD") == std::expected<std::string, std::string>(second->id));
    };

    "branches and tags: a branch at a commit, HEAD switched, a commit on a branch that is not HEAD's, the nearest commit two reach"_test = [] {
        agent::History history(fresh("agent-history-branches"));
        const auto base = history.commit({{"a", "1"}}, "base", "person", "");
        expect(fatal(base.has_value()));
        expect(history.branch("try/hotter") == std::expected<std::string, std::string>(base->id));
        expect(!history.branch("try/hotter") && !history.branch("HEAD") && !history.branch("no good name")) << "there already; not a name";
        const auto on_main = history.commit({{"a", "2"}}, "main goes on", "person", "");
        expect(fatal(on_main.has_value()));
        expect(history.switch_to("try/hotter") == std::expected<std::string, std::string>(base->id) && eq(history.head_branch(), std::string("try/hotter")));
        const auto on_branch = history.commit({{"a", "1"}, {"b", "x"}}, "the branch goes on", "agent:b", "");
        expect(fatal(on_branch.has_value()));
        expect(on_branch->parents == std::vector<std::string>{base->id} && eq(on_branch->branch, std::string("try/hotter")));
        expect(history.common_ancestor("main", "try/hotter") == std::optional<std::string>(base->id));
        // A commit on a branch that is not HEAD's, made where it is not there: HEAD stays.
        const auto aside = history.commit_on("variant-bold", {{"a", "bold"}}, "a variant", "agent:b", "");
        expect(fatal(aside.has_value()));
        expect(eq(history.head_branch(), std::string("try/hotter")) && aside->parents == std::vector<std::string>{on_branch->id});
        expect(history.tag("v1", "main") == std::expected<std::string, std::string>(on_main->id) && !history.tag("v1"));
        expect(history.resolve("v1") == std::expected<std::string, std::string>(on_main->id));
        const auto branches = history.branches();
        expect(eq(branches.size(), std::size_t{3}) && branches.at("main") == on_main->id && branches.at("try/hotter") == on_branch->id && branches.at("variant-bold") == aside->id);
        expect(eq(history.tags().size(), std::size_t{1}));
        expect(!history.switch_to("nowhere"));
        // A merge's commit has both parents.
        const auto merged = history.commit({{"a", "2"}, {"b", "x"}}, "merge main", "person", "", std::vector<std::string>{on_branch->id, on_main->id});
        expect(fatal(merged.has_value()));
        expect(eq(merged->parents.size(), std::size_t{2}) && history.common_ancestor("try/hotter", "main") == std::optional<std::string>(on_main->id));
    };

    "pictures by content, one a commit; names a branch or tag may have"_test = [] {
        agent::History history(fresh("agent-history-pictures"));
        const auto commit = history.commit({{"a", "1"}}, "one", "person", "");
        expect(fatal(commit.has_value()));
        const std::array<std::uint8_t, 5> png{0x89, 'P', 'N', 'G', 0};
        const auto kept = history.keep_picture(png), again = history.keep_picture(png);
        expect(fatal(kept.has_value() && again.has_value()));
        expect(*kept == *again && std::filesystem::file_size(*kept) == 5u) << "the same picture is kept once";
        expect(!history.thumbnail(commit->id));
        expect(history.set_thumbnail(commit->id, *kept).has_value() && history.thumbnail(commit->id) == std::optional(*kept));
        expect(history.set_thumbnail("another", *kept).has_value() && history.thumbnail(commit->id) == std::optional(*kept)) << "one more does not lose the first";
        expect(agent::valid_ref_name("main") && agent::valid_ref_name("try/hotter-2.1") && !agent::valid_ref_name("HEAD") && !agent::valid_ref_name("a b") && !agent::valid_ref_name("a~1") &&
               !agent::valid_ref_name("/a") && !agent::valid_ref_name("a//b") && !agent::valid_ref_name("0123456789abcdef"));
    };

    "a merge of two texts by lines: each side's change, the same change once, overlapping changes a conflict"_test = [] {
        const std::string base = "a\nb\nc\nd";
        const auto both = agent::merge_text(base, "A\nb\nc\nd", "a\nb\nc\nD");
        expect(!both.conflict && both.text == "A\nb\nc\nD") << both.text;
        const auto same = agent::merge_text(base, "a\nB\nc\nd", "a\nB\nc\nd");
        expect(!same.conflict && same.text == "a\nB\nc\nd");
        const auto inserted = agent::merge_text(base, "a\nb\nx\nc\nd", "a\nb\nc\nd\ny");
        expect(!inserted.conflict && inserted.text == "a\nb\nx\nc\nd\ny") << inserted.text;
        expect(agent::merge_text(base, "a\nX\nc\nd", "a\nY\nc\nd").conflict);
    };
}};

UTest claims{"agent.claims", [] {
    "claims stand together unless they meet: the same part, a property against the part, a subtree against what is inside it"_test = [] {
        agent::Claims book;
        const auto wood = book.claim({.part = "materials/wood", .intent = "making it harder to light"}, "agent:a", 10);
        expect(fatal(wood.has_value()));
        expect(eq(wood->actor, std::string("agent:a")) && eq(wood->since, 10.0) && eq(wood->until, 40.0)) << "thirty seconds unless said";
        // Another part, and a property of another part: both stand.
        expect(book.claim({.part = "materials/water", .property = "boiling"}, "agent:b", 11).has_value());
        expect(book.claim({.part = "materials/water", .property = "density"}, "agent:c", 11).has_value()) << "two properties of one part";
        // What meets: the part held, a property of it, a subtree around it — each answered with the claim in the way.
        const auto same = book.claim({.part = "materials/wood"}, "agent:b", 12);
        expect(!same && eq(same.error().actor, std::string("agent:a")) && eq(same.error().intent, std::string("making it harder to light")));
        expect(!book.claim({.part = "materials/wood", .property = "ignition"}, "agent:b", 12)) << "a property of a part held whole";
        expect(!book.claim({.part = "materials", .subtree = true}, "agent:b", 12)) << "a subtree around a part held";
        expect(!book.claim({.part = "materials/water"}, "agent:a", 12)) << "the part whose property another holds";
        expect(!book.claim({.part = "materials/water", .property = "boiling"}, "agent:c", 12)) << "the same property";
        expect(book.claim({.part = "materials/wooden"}, "agent:b", 12).has_value()) << "a name that begins the same is another part";
        // A subtree holds what is inside it.
        expect(book.claim({.part = "laws", .subtree = true}, "agent:b", 13).has_value());
        expect(!book.claim({.part = "laws/burns"}, "agent:a", 13) && !book.claim({.part = "laws/burns", .property = "when"}, "agent:a", 13));
        expect(eq(book.held(13).size(), std::size_t{5}));
        expect(book.over("laws/burns", "agent:a", 13).has_value() && !book.over("laws/burns", "agent:b", 13)) << "held over it by another; not by itself";
        expect(book.over("materials/wood", "agent:b", 13).has_value() && !book.over("appearance", "agent:b", 13));
        expect(agent::within("main/a/b", "main/a") && agent::within("main/a", "main/a") && !agent::within("main/ab", "main/a") && !agent::within("main", "main/a"));
    };

    "a claim is a lease: renewed by asking again, ended by its time or by letting go — and a person's word lets go of anyone's"_test = [] {
        agent::Claims book;
        expect(book.claim({.part = "scene", .intent = "first", .seconds = 5}, "agent:a", 0).has_value());
        const auto renewed = book.claim({.part = "scene", .intent = "still at it", .seconds = 5}, "agent:a", 4);
        expect(fatal(renewed.has_value()));
        expect(eq(renewed->since, 0.0) && eq(renewed->until, 9.0) && eq(renewed->intent, std::string("still at it")) && eq(book.held(4).size(), std::size_t{1}));
        expect(!book.claim({.part = "scene"}, "agent:b", 8)) << "held until its time";
        expect(book.held(9).empty() && !book.over("scene", "agent:b", 9)) << "ended: not held";
        expect(book.claim({.part = "scene"}, "agent:b", 9).has_value()) << "and another has it";
        // The seconds asked are kept within 1 … 600.
        expect(eq(book.claim({.part = "x", .seconds = 0}, "agent:a", 10)->until, 11.0) && eq(book.claim({.part = "y", .seconds = 9000}, "agent:a", 10)->until, 610.0));
        // Letting go: one's own; another's only by a person's word.
        expect(!book.release("scene", "", "agent:a") && book.release("scene", "", "agent:b") && !book.release("scene", "", "agent:b"));
        expect(book.over("y", "agent:b", 10).has_value() && book.release("y", "", "someone", true) && !book.over("y", "agent:b", 10));
        book.clear();
        expect(book.empty() && book.held(10).empty());
    };
}};

namespace {

// A design that is nothing but its texts: what is put in is what runs.
class Notes final : public agent::Sources {
public:
    Texts now;
    std::vector<Reason> reasons;  // why texts were put in, each time
    Texts texts() override { return now; }
    std::expected<std::string, std::string> apply(const Texts& texts, Reason reason) override {
        if (texts.contains("refuse")) return std::unexpected(std::string("these texts do not run"));
        std::string changed;
        for (const auto& [path, text] : texts)
            if (!now.contains(path) || now.at(path) != text) changed += std::format("{}{}", changed.empty() ? "" : ",", agent::json_text(path));
        now = texts;
        reasons.push_back(reason);
        return std::format("{{\"changed\":[{}]}}", changed);
    }
    std::string state() override { return std::format("{}", now.size()); }
};

struct Asked {
    std::optional<mlibs::json::Document> data;
    std::string                          error;
    [[nodiscard]] mlibs::json::Node operator[](std::string_view key) const { return data->root()[key]; }
};
Asked ask(agent::HistoryOperations& operations, std::string_view operation, std::string_view parameters = "{}", std::string_view actor = "agent:a") {
    const auto parsed = mlibs::json::Document::parse(parameters, "parameters");
    expect(fatal(parsed.has_value())) << parameters;
    Asked out;
    auto answer = operations.run(operation, parsed->root(), actor);
    if (!answer) {
        out.error = answer.error();
        return out;
    }
    auto document = mlibs::json::Document::parse(*answer, "answer");
    expect(fatal(document.has_value())) << *answer;
    out.data.emplace(std::move(*document));
    return out;
}

}  // namespace

UTest operations{"agent.history.operations", [] {
    "the history's operations over any sources: commit, branch, switch, diff, merge by lines, tag, revert — each answered as data, each refusal with its reason"_test = [] {
        agent::History history(fresh("agent-history-operations"));
        Notes notes;
        notes.now = {{"wood", "ignition 570\nfuel 400\nheat 80"}, {"scene", "a strip of wood"}};
        agent::HistoryOperations operations(history, notes, {.list = "lab.history", .commit = "lab.commit", .edit = "lab.edit"});

        expect(ask(operations, "commit").error.contains("`message`")) << "a commit says what it is";
        const Asked first = ask(operations, "commit", R"({"message":"wood as it is"})");
        expect(fatal(first.data.has_value())) << first.error;
        expect(first["actor"].text() == "agent:a" && first["branch"].text() == "main" && first["state"].text() == "2" && first["files"].size() == 2u && first["parents"].size() == 0u);
        expect(ask(operations, "commit", R"({"message":"again"})").error.contains("nothing changed"));
        const Asked listed = ask(operations, "history");
        expect(listed["head"].text() == "main" && listed["log"].size() == 1u && listed["uncommitted"].boolean() == false && listed["branches"]["main"].text() == first["id"].text());

        // A branch, the design changed on it, committed by another.
        expect(ask(operations, "branch", R"({"name":"hotter"})")["at"].text() == first["id"].text());
        expect(ask(operations, "switch", R"({"to":"hotter"})")["branch"].text() == "hotter" && notes.reasons.back() == agent::Sources::Reason::switch_to);
        notes.now["wood"] = "ignition 570\nfuel 400\nheat 120";
        expect(ask(operations, "history")["uncommitted"].boolean() == true);
        const Asked what = ask(operations, "diff");
        expect(what["to"].text() == "running" && what["files"].size() == 1u && what["files"][0]["path"].text() == "wood" && what["files"][0]["line"].integer() == 3 &&
               what["files"][0]["before"].text() == "heat 80" && what["files"][0]["after"].text() == "heat 120");
        expect(ask(operations, "switch", R"({"to":"main"})").error.contains("lab.commit them")) << "changes not committed are not dropped unasked, and the answer names the subject's own operation";
        const Asked hot = ask(operations, "commit", R"({"message":"hotter"})", "agent:b");
        expect(hot["actor"].text() == "agent:b" && hot["parents"][0].text() == first["id"].text());

        // Main goes on with another line; the two merge by lines.
        const Asked back = ask(operations, "switch", R"({"to":"main"})");
        expect(back["branch"].text() == "main" && back["changed"][0].text() == "wood" && notes.now.at("wood").ends_with("heat 80")) << "what the sources said changed, and where the design is now";
        notes.now["wood"] = "ignition 600\nfuel 400\nheat 80";
        expect(ask(operations, "commit", R"({"message":"harder to light"})").data.has_value());
        const Asked merged = ask(operations, "merge", R"({"from":"hotter"})", "person");
        expect(fatal(merged.data.has_value())) << merged.error;
        expect(notes.now.at("wood") == "ignition 600\nfuel 400\nheat 120" && merged["commit"]["parents"].size() == 2u && merged["commit"]["message"].text() == "merge hotter" &&
               merged["commit"]["actor"].text() == "person" && notes.reasons.back() == agent::Sources::Reason::edit);
        // The same lines changed on both: nothing merged, and why.
        expect(ask(operations, "branch", R"({"name":"other"})").data.has_value());
        notes.now["scene"] = "a pile of wood";
        expect(ask(operations, "commit", R"({"message":"a pile"})").data.has_value());
        expect(ask(operations, "switch", R"({"to":"other"})").data.has_value());
        notes.now["scene"] = "a raft of wood";
        expect(ask(operations, "commit", R"({"message":"a raft"})").data.has_value());
        const Texts before_merge = notes.now;
        const Asked conflict = ask(operations, "merge", R"({"from":"main"})");
        expect(conflict.error.contains("[\"scene\"]") && conflict.error.contains("lab.edit one side") && notes.now == before_merge) << conflict.error;

        // A tag; a commit seen that is no branch's last; a revert is a new commit.
        expect(ask(operations, "tag", R"({"name":"v1","at":"main"})")["tag"].text() == "v1" && ask(operations, "tag", R"({"name":"v1"})").error.contains("there already"));
        const Asked seen = ask(operations, "switch", std::format(R"({{"to":"{}"}})", first["id"].text().value_or("")));
        expect(seen["detached"].boolean() == true && seen["branch"].text() == "other" && operations.shown_now() == first["id"].text() && notes.now.at("scene") == "a strip of wood");
        expect(ask(operations, "history")["uncommitted"].boolean() == false) << "what runs is the commit seen";
        const Asked reverted = ask(operations, "revert", R"({"to":"v1"})");
        expect(reverted["commit"]["message"].text().value_or("").starts_with("back to ") && notes.now.at("scene") == "a pile of wood" && notes.reasons.back() == agent::Sources::Reason::undo &&
               operations.shown_commit.empty());
        // What is not there, and what the sources refuse.
        expect(ask(operations, "switch", R"({"to":"nowhere"})").error.contains("lab.history lists the branches and tags") && ask(operations, "revert", R"({"to":"nowhere"})").error.contains("no branch, tag or commit"));
        expect(ask(operations, "diff", R"({"from":"nowhere"})").error.contains("nowhere") && ask(operations, "frobnicate").error.contains("no operation"));
        expect(ask(operations, "history", R"({"limit":2})")["log"].size() == 2u);
    };
}};
