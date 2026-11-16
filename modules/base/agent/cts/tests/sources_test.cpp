// The design adapter's contract (mlibs.agent.sources@1) and the workspace that holds several domains' sources under
// one history: sources that keep it pass; ones that change on their own texts or take what does not read fail by
// name; a workspace of two keeps it itself, a refusal in one domain leaves the other as it was, and one history
// commits, edits and goes back across both.
import std;
import mlibs.agent.cts;
import mlibs.json;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace agent = mlibs::agent;
namespace contract = mlibs::agent::cts;
using mlibs::cts::Status;

namespace {

// Texts kept as they are; a text beginning with '!' does not read. Its state: the texts themselves.
class Texts final : public agent::Sources {
public:
    enum class Fault { none, changes_own, takes_anything };
    explicit Texts(std::map<std::string, std::string> first, Fault fault = Fault::none) : texts_(std::move(first)), fault_(fault) {}
    std::map<std::string, std::string> texts() override { return texts_; }
    std::expected<std::string, std::string> apply(const std::map<std::string, std::string>& texts, Reason) override {
        if (fault_ != Fault::takes_anything)
            for (const auto& [path, text] : texts)
                if (text.starts_with('!')) return std::unexpected(std::format("{} does not read", path));
        texts_ = texts;
        if (fault_ == Fault::changes_own) texts_["touched"] = std::to_string(++touched_);
        return std::string(R"({"applied":true})");
    }
    std::string state() override {
        std::string out;
        for (const auto& [path, text] : texts_) out += std::format("{}={};", path, text);
        return out;
    }

private:
    std::map<std::string, std::string> texts_;
    Fault fault_;
    int touched_ = 0;
};

contract::SourcesProbe texts_probe(Texts::Fault fault = Texts::Fault::none) {
    return {.make = [fault] { return std::make_unique<Texts>(std::map<std::string, std::string>{{"main.ts", "export const a = 1;"}}, fault); },
            .change = {{"main.ts", "export const a = 2;"}},
            .refused = {{"main.ts", "! not a program"}}};
}

// Two domains in one workspace: an interface and an animation.
struct Two final : agent::Sources {
    Texts ui{{{"main.ts", "column()"}}};
    Texts animation{{{"rise.animation.omui.json", R"({"duration":1})"}}};
    agent::Workspace workspace;
    Two() {
        (void)workspace.mount("ui", ui);
        (void)workspace.mount("animation", animation);
    }
    std::map<std::string, std::string> texts() override { return workspace.texts(); }
    std::expected<std::string, std::string> apply(const std::map<std::string, std::string>& texts, Reason reason) override { return workspace.apply(texts, reason); }
    std::string state() override { return workspace.state(); }
};

}  // namespace

static UTest cases{"agent.sources", [] {
    contract::declare_sources(texts_probe(), {.subject = "texts"});
    contract::declare_sources({.make = [] { return std::make_unique<Two>(); },
                               .change = {{"ui/main.ts", "column(text())"}, {"animation/rise.animation.omui.json", R"({"duration":2})"}},
                               .refused = {{"animation/rise.animation.omui.json", "! half a document"}}},
                              {.subject = "workspace"});

    "sources that change on their own, or take what does not read, fail the contract by name"_test = [] {
        const auto changing = mlibs::cts::tally([] { contract::declare_sources(texts_probe(Texts::Fault::changes_own), {.subject = "changing"}); });
        expect(changing.find("sources.apply.own") && changing.find("sources.apply.own")->status == Status::failed) << "its own texts are a change";
        const auto taking = mlibs::cts::tally([] { contract::declare_sources(texts_probe(Texts::Fault::takes_anything), {.subject = "taking"}); });
        expect(taking.find("sources.apply.refused") && taking.find("sources.apply.refused")->status == Status::failed) << "what does not read was taken";
        expect(taking.find("sources.apply.change") && taking.find("sources.apply.change")->status == Status::passed);
    };

    "a workspace: every domain under its name; a refusal in one leaves the other as it was; a path under none refused"_test = [] {
        Two two;
        const auto texts = two.texts();
        expect(texts.contains("ui/main.ts") && texts.contains("animation/rise.animation.omui.json") && eq(texts.size(), std::size_t{2}));
        expect(two.state().starts_with("ui=") && two.state().contains(";animation="));
        auto edited = texts;
        edited["ui/main.ts"] = "column(text())";
        edited["animation/rise.animation.omui.json"] = "! half a document";
        const auto refused = two.apply(edited, agent::Sources::Reason::edit);
        expect(!refused && refused.error().starts_with("animation: ")) << (refused ? *refused : refused.error());
        expect(two.ui.texts().at("main.ts") == "column()") << "the interface's edit was put back";
        auto stray = texts;
        stray["sound/hit.wav"] = "…";
        expect(!two.apply(stray, agent::Sources::Reason::edit)) << "no domain is mounted as sound";
        agent::Workspace again;
        expect(again.mount("ui", two.ui).has_value() && !again.mount("ui", two.animation).has_value() && !again.mount("a/b", two.animation).has_value());
    };

    "one history over both domains: a commit holds both, an edit to each, and going back puts both back"_test = [] {
        Two two;
        const auto directory = temp_dir() / "workspace-history";  // mtest's, this case's own
        std::error_code error;
        std::filesystem::remove_all(directory, error);
        agent::History history(directory);
        agent::HistoryOperations operations(history, two);
        const auto parameters = [](std::string_view json) { return mlibs::json::Document::parse(json, "request").value(); };
        auto first = parameters(R"({"message":"the first design"})");
        expect(operations.run("commit", first.root(), "person:ana").has_value());
        // An agent edits both: the interface and its animation, then commits them as one.
        auto both = two.texts();
        both["ui/main.ts"] = "column(text(), button())";
        both["animation/rise.animation.omui.json"] = R"({"duration":0.4})";
        expect(two.apply(both, agent::Sources::Reason::edit).has_value());
        auto second = parameters(R"({"message":"a button that rises faster"})");
        const auto committed = operations.run("commit", second.root(), "agent:designer");
        expect(committed.has_value() && committed->contains("ui/main.ts") && committed->contains("animation/rise.animation.omui.json")) << (committed ? *committed : committed.error());
        // Reverted as one step: both domains as the first design had them.
        auto back = parameters(R"({"to":"HEAD~1"})");
        const auto reverted = operations.run("revert", back.root(), "person:ana");
        expect(reverted.has_value()) << (reverted ? *reverted : reverted.error());
        expect(two.ui.texts().at("main.ts") == "column()" && two.animation.texts().at("rise.animation.omui.json") == R"({"duration":1})") << two.state();
    };
}};
