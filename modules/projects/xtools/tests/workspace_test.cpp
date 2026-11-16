// RC3.10's bar (plan §4.5, D-204): one workspace designs an interface and an animation together, headless. The
// interface's adapter (omui.designer's view documents) and hanim's (animation documents) are mounted side by side in
// mlibs.agent's Workspace under one history: the workspace keeps the adapter's contract itself; an agent's edit to
// both is one commit; a branch and a switch move both; a revert puts both back; a refusal in one domain leaves the
// other's files as they were.
import std;
import mlibs.agent.cts;
import mlibs.json;
import omui.designer;
import omui.ui.base;
import hanim.studio;
import mcpplibs.mtest;
import mcpplibs.os;

using namespace mcpplibs::mtest;
namespace agent = mlibs::agent;
namespace contract = mlibs::agent::cts;

namespace {

std::filesystem::path projects() {  // modules/projects/xtools/tests → modules/projects
    return std::filesystem::path(std::source_location::current().file_name()).parent_path().parent_path().parent_path();
}

constexpr std::string_view card = R"({"omui": "0.1", "view": {"root": {"type": "column", "id": "card", "children": [
  {"type": "text", "id": "title", "props": {"text": "Ready"}}]}}})";

std::string bounce() { return mcpplibs::os::fs::read(projects() / "hanim" / "studio" / "examples" / "bounce.animation.xdef.json").value_or(""); }

std::string slower(std::string text) {
    if (const auto at = text.find(R"("duration": 1.2)"); at != std::string::npos) text.replace(at, 15, R"("duration": 2.4)");
    return text;
}

// A design of two domains on disk: its directories, each domain's adapter, the workspace over them.
struct Design final : agent::Sources {
    std::filesystem::path         ui_dir, animation_dir;
    omui::designer::ViewSources   ui;
    hanim::studio::DocumentSources animation;
    agent::Workspace              workspace;
    explicit Design(const std::filesystem::path& root)
        : ui_dir(root / "ui"), animation_dir(root / "animation"), ui(ui_dir), animation(animation_dir) {
        std::error_code error;
        std::filesystem::remove_all(root, error);
        std::filesystem::create_directories(ui_dir, error);
        std::filesystem::create_directories(animation_dir, error);
        (void)mcpplibs::os::fs::write_atomic(ui_dir / "card.view.xdef.json", card);
        (void)mcpplibs::os::fs::write_atomic(animation_dir / "bounce.animation.xdef.json", bounce());
        (void)workspace.mount("ui", ui);
        (void)workspace.mount("animation", animation);
    }
    std::map<std::string, std::string> texts() override { return workspace.texts(); }
    std::expected<std::string, std::string> apply(const std::map<std::string, std::string>& texts, Reason reason) override { return workspace.apply(texts, reason); }
    std::string state() override { return workspace.state(); }
};

std::string on_disk(const std::filesystem::path& file) { return mcpplibs::os::fs::read(file).value_or("(none)"); }

mlibs::json::Document request(std::string_view json) { return mlibs::json::Document::parse(json, "request").value(); }

}  // namespace

static UTest cases{"xtools.workspace", [] {
    omui::register_base_components();
    // The composition keeps the adapter's contract: an edit to both domains, and one domain's refusal.
    contract::declare_sources(
        {.make = [] {
             static int made_so_far = 0;
             return std::make_unique<Design>(temp_dir() / std::format("design-{}", ++made_so_far));
         },
         .change = {{"ui/card.view.xdef.json", std::string(card).replace(std::string(card).find("Ready"), 5, "Go")}, {"animation/bounce.animation.xdef.json", slower(bounce())}},
         .refused = {{"animation/bounce.animation.xdef.json", R"({"version": 1, "objects": [{"id": "ball", "shape": {"circle": "wide"}}]})"}}},
        {.subject = "xtools.workspace"});

    "an interface and its animation designed together: one commit, a branch, a switch, a revert — both domains each time"_test = [] {
        Design design(temp_dir() / "design");
        agent::History history(temp_dir() / "history");
        agent::HistoryOperations operations(history, design);
        auto first = request(R"({"message":"a card and a bounce"})");
        expect(operations.run("commit", first.root(), "person:ana").has_value());
        auto branch = request(R"({"name":"livelier"})");
        expect(operations.run("branch", branch.root(), "agent:designer").has_value());
        auto onto = request(R"({"to":"livelier"})");
        expect(operations.run("switch", onto.root(), "agent:designer").has_value());

        // The agent's edit: the card's title and the ball's drop, as one change.
        auto texts = design.texts();
        texts["ui/card.view.xdef.json"] = std::string(card).replace(std::string(card).find("Ready"), 5, "Go!");
        texts["animation/bounce.animation.xdef.json"] = slower(bounce());
        const auto applied = design.apply(texts, agent::Sources::Reason::edit);
        expect(applied && applied->contains(R"("ui":)") && applied->contains(R"("animation":)")) << (applied ? *applied : applied.error());
        auto second = request(R"({"message":"Go! and a slower drop"})");
        const auto committed = operations.run("commit", second.root(), "agent:designer");
        expect(committed && committed->contains("ui/card.view.xdef.json") && committed->contains("animation/bounce.animation.xdef.json"))
            << (committed ? *committed : committed.error());
        const std::string livelier = design.state();

        // Back to the first design: a switch puts both domains' files back.
        auto back = request(R"({"to":"main"})");
        const auto switched = operations.run("switch", back.root(), "person:ana");
        expect(switched.has_value()) << (switched ? *switched : switched.error());
        expect(on_disk(design.ui_dir / "card.view.xdef.json").contains("Ready") && on_disk(design.animation_dir / "bounce.animation.xdef.json") == bounce());
        // And forward again: the same state, both domains.
        expect(operations.run("switch", onto.root(), "person:ana").has_value() && eq(design.state(), livelier));

        // One step back on the branch: a revert, both domains as the first design had them.
        auto revert = request(R"({"to":"HEAD~1"})");
        expect(operations.run("revert", revert.root(), "person:ana").has_value());
        expect(on_disk(design.ui_dir / "card.view.xdef.json").contains("Ready") && on_disk(design.animation_dir / "bounce.animation.xdef.json") == bounce());
    };

    "a refusal in one domain leaves the other's files as they were"_test = [] {
        Design design(temp_dir() / "refused");
        auto texts = design.texts();
        texts["ui/card.view.xdef.json"] = std::string(card).replace(std::string(card).find("Ready"), 5, "Changed");
        texts["animation/bounce.animation.xdef.json"] = R"({"version": 1, "objects": [{"id": "ball", "shape": {"circle": "wide"}}]})";
        const auto refused = design.apply(texts, agent::Sources::Reason::edit);
        expect(!refused && refused.error().starts_with("animation: ")) << (refused ? *refused : refused.error());
        expect(on_disk(design.ui_dir / "card.view.xdef.json") == card) << "the card is as it was";
    };
}};
