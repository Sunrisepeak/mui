// The interface's design adapter for a workspace (omui.designer:view_sources) keeps the design adapter's contract
// (mlibs.agent.sources@1): a directory of view documents read the same twice, its own texts no change, an edit seen
// in what runs and taken back, a document naming a component there is not refused at its place, nothing written.
import std;
import mlibs.agent.cts;
import omui.designer;
import omui.ui.base;
import mcpplibs.mtest;
import mcpplibs.os;

using namespace mcpplibs::mtest;
namespace contract = mlibs::agent::cts;

namespace {

constexpr std::string_view hello = R"({"omui": "0.1", "view": {"root": {"type": "text", "id": "greeting", "props": {"text": "hello"}}}})";

std::unique_ptr<mlibs::agent::Sources> made() {
    static int made_so_far = 0;
    const auto directory = temp_dir() / std::format("views-{}", ++made_so_far);
    std::error_code error;
    std::filesystem::remove_all(directory, error);
    std::filesystem::create_directories(directory, error);
    (void)mcpplibs::os::fs::write_atomic(directory / "hello.view.omui.json", hello);
    return std::make_unique<omui::designer::ViewSources>(directory);
}

}  // namespace

static UTest cases{"designer.view_sources", [] {
    omui::register_base_components();
    contract::declare_sources({.make = made,
                               .change = {{"hello.view.omui.json", R"({"omui": "0.1", "view": {"root": {"type": "text", "id": "greeting", "props": {"text": "hello there"}}}})"}},
                               .refused = {{"hello.view.omui.json", R"({"omui": "0.1", "view": {"root": {"type": "buton", "id": "greeting"}}})"}}},
                              {.subject = "omui.designer.view_sources"});

    "a refusal says where: the document, its line and column, the component it names"_test = [] {
        auto sources = made();
        const auto refused = sources->apply({{"hello.view.omui.json", "{\"omui\": \"0.1\",\n \"view\": {\"root\": {\"type\": \"buton\"}}}"}}, mlibs::agent::Sources::Reason::edit);
        expect(!refused && refused.error().contains("hello.view.omui.json:2:") && refused.error().contains("buton")) << (refused ? *refused : refused.error());
        expect(sources->texts().at("hello.view.omui.json") == hello) << "nothing written";
    };
}};
