// hanim's design adapter (hanim.studio:sources) keeps the design adapter's contract (mlibs.agent.sources@1): a
// directory of animation documents read the same twice, its own texts no change, an edit to the timing seen in
// what runs and taken back, a document that does not read refused with nothing written.
import std;
import mlibs.agent.cts;
import hanim.studio;
import mcpplibs.mtest;
import mcpplibs.os;

using namespace mcpplibs::mtest;
namespace contract = mlibs::agent::cts;

namespace {

std::filesystem::path here() { return std::filesystem::path(std::source_location::current().file_name()).parent_path(); }

std::string bounce() { return mcpplibs::os::fs::read(here().parent_path() / "examples" / "bounce.animation.omui.json").value_or(""); }

// A directory of its own with the example in it, the same first state each time.
std::unique_ptr<mlibs::agent::Sources> made() {
    static int made_so_far = 0;
    const auto directory = temp_dir() / std::format("documents-{}", ++made_so_far);
    std::error_code error;
    std::filesystem::remove_all(directory, error);
    std::filesystem::create_directories(directory, error);
    (void)mcpplibs::os::fs::write_atomic(directory / "bounce.animation.omui.json", bounce());
    return std::make_unique<hanim::studio::DocumentSources>(directory);
}

std::string slower() {
    std::string text = bounce();
    const auto at = text.find(R"("duration": 1.2)");
    if (at != std::string::npos) text.replace(at, 15, R"("duration": 2.4)");
    return text;
}

}  // namespace

static UTest cases{"hanim.studio.sources", [] {
    contract::declare_sources({.make = made,
                               .change = {{"bounce.animation.omui.json", slower()}},
                               .refused = {{"bounce.animation.omui.json", R"({"version": 1, "objects": [{"id": "ball", "shape": {"circle": "wide"}}]})"}}},
                              {.subject = "hanim.studio.sources"});

    "a new document is written, one no longer among them removed; one that does not read names its place"_test = [] {
        auto sources = made();
        auto texts = sources->texts();
        expect(eq(texts.size(), std::size_t{1}));
        texts["drop.animation.omui.json"] = bounce();
        texts.erase("bounce.animation.omui.json");
        const auto applied = sources->apply(texts, mlibs::agent::Sources::Reason::edit);
        expect(applied && applied->contains("drop.animation.omui.json") && applied->contains(R"("removed":["bounce.animation.omui.json"])")) << (applied ? *applied : applied.error());
        expect(sources->texts().size() == 1u && sources->texts().contains("drop.animation.omui.json"));
        const auto stray = sources->apply({{"notes.txt", "hello"}}, mlibs::agent::Sources::Reason::edit);
        expect(!stray && stray.error().contains("no animation document")) << "only the directory's animation documents";
    };
}};
