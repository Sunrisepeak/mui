// An application's surfaces outside its view, drawn (W7, D-140): a file
// chosen in the interface's own picker, notices that tell and can be pressed,
// a menu bar above the application — each operated as a person (and an agent)
// operates the rest of the tree, with nothing of the system's needed.
import std;
import omui;
import mlibs.testing;
import mcpplibs.mtest;
import mcpplibs.os;

using namespace omui;
using namespace mcpplibs::mtest;

namespace {

struct Shown {
    Headless h{HeadlessOptions{.level = HeadlessLevel::pixels, .size = {640, 480}}};
    std::vector<std::string> invoked;

    Shown() {
        register_base_components();
        for (const std::string_view verb : {"notes.open", "notes.save", "notes.quit", "notes.later"})
            (void)h.runtime().verbs().add({std::string(verb), "", Effect::reversible, "", {}}, [this, verb](const Value&) -> VerbResult {
                invoked.emplace_back(verb);
                return Value{};
            });
        h.mount("main", column({.id = "page", .padding = 8}, {text({.id = "body", .text = "Notes"})}));
        h.settle();
    }

    bool press(std::string_view path) {
        Invocation person;
        person.actor = {"person", "tester", {}};
        const Envelope done = semantic::Tree(h.runtime()).act(path, "activate", person);
        h.settle();
        if (done.exit_code().value_or(-1) != 0) std::println("press {}: {}", path, done.ndjson());
        return done.exit_code().value_or(-1) == 0;
    }
    bool there(std::string_view path) { return static_cast<bool>(h.runtime().find(path)); }
    std::string name_of(std::string_view path) {
        const auto node = h.tree().node(path);
        return node ? node->name : std::string();
    }
};

}  // namespace

UTest cases{"sdk.system", [] {
    "a file chosen in the drawn picker: only what the filters let through, into a folder and up again, chosen, or cancelled"_test = [] {
        const auto dir = mlibs::testing::scratch_dir("system-files");
        std::filesystem::create_directories(dir / "trip");
        for (const auto* name : {"a.png", "b.txt", "trip/c.png"}) std::ofstream(dir / name) << "x";
        Shown shown;
        System system(shown.h.runtime(), {.files = Surfacing::drawn});
        std::optional<std::vector<std::filesystem::path>> answer;
        system.choose_files({.title = "Open a picture", .filters = {{"Pictures", "png"}}, .start = dir},
                            [&](std::vector<std::filesystem::path> chosen) { answer = std::move(chosen); });
        shown.h.settle();
        expect(shown.there("system-files/panel/title") && shown.name_of("system-files/panel/title") == "Open a picture");
        // The folder first, then the picture; the text file left out.
        expect(eq(shown.name_of("system-files/panel/entries/list/entry-0"), std::string("trip/")) &&
               eq(shown.name_of("system-files/panel/entries/list/entry-1"), std::string("a.png")) && !shown.there("system-files/panel/entries/list/entry-2"))
            << shown.name_of("system-files/panel/entries/list/entry-2");
        expect(shown.press("system-files/panel/entries/list/entry-0")) << "into the folder";
        expect(eq(shown.name_of("system-files/panel/entries/list/entry-0"), std::string("c.png")));
        expect(shown.press("system-files/panel/where/up")) << "and up again";
        expect(shown.press("system-files/panel/actions/accept"));
        expect(!answer && shown.name_of("system-files/panel/problem") == "nothing chosen yet") << "nothing chosen: said, not answered";
        expect(shown.press("system-files/panel/entries/list/entry-1") && shown.press("system-files/panel/actions/accept"));
        expect(answer && answer->size() == 1 && answer->front().filename() == "a.png") << (answer ? answer->size() : 99);
        expect(!shown.there("system-files/panel")) << "gone once chosen";

        answer.reset();
        system.choose_files({.kind = FileChoice::Kind::save, .start = dir, .name = "notes.md"}, [&](std::vector<std::filesystem::path> chosen) { answer = std::move(chosen); });
        shown.h.settle();
        expect(shown.press("system-files/panel/actions/accept"));
        expect(answer && answer->size() == 1 && answer->front() == dir / "notes.md") << "saved as the name offered";

        answer.reset();
        system.choose_files({.start = dir}, [&](std::vector<std::filesystem::path> chosen) { answer = std::move(chosen); });
        shown.h.settle();
        expect(shown.press("system-files/panel/actions/cancel"));
        expect(answer && answer->empty()) << "cancelled: nothing chosen";
    };

    "notices: told, pressed (their verb runs), gone after their time"_test = [] {
        Shown shown;
        System system(shown.h.runtime(), {.notifications = Surfacing::drawn, .notice_shown = seconds(30)});
        system.notify({.title = "Saved", .body = "notes.md"});
        system.notify({.title = "A reminder", .verb = "notes.later"});
        shown.h.settle(seconds(0.5));
        expect(shown.there("system-notices/notice-1") && shown.there("system-notices/notice-2"));
        expect(shown.press("system-notices/notice-2"));
        expect(eq(shown.invoked.size(), std::size_t{1}) && shown.invoked.front() == "notes.later") << "pressed: its verb";
        expect(!shown.there("system-notices/notice-2") && shown.there("system-notices/notice-1"));
        (void)shown.h.advance(seconds(31));
        expect(!shown.there("system-notices/notice-1")) << "gone after its time";
    };

    "a drawn menu bar above the application: a group opened, an entry chosen runs its verb; the application below it"_test = [] {
        Shown shown;
        System system(shown.h.runtime());
        const float top_before = shown.h.runtime().screen_bounds(shown.h.runtime().find("main")).y;
        system.set_menu_bar({{"File", {{"Open", "notes.open"}, {"Save", "notes.save"}}}, {"App", {{"Quit", "notes.quit"}}}});
        shown.h.settle();
        const float top_after = shown.h.runtime().screen_bounds(shown.h.runtime().find("main")).y;
        expect(top_after >= top_before + 32) << std::format("the application moved down below the bar: {} then {}", top_before, top_after);
        expect(shown.press("system-menu/bar/group-0"));
        expect(shown.there("system-menu/open/entry-1") && shown.name_of("system-menu/open/entry-1") == "Save");
        expect(shown.press("system-menu/open/entry-1"));
        expect(eq(shown.invoked.size(), std::size_t{1}) && shown.invoked.front() == "notes.save");
        expect(!shown.there("system-menu/open")) << "closed once chosen";
        expect(!system.set_tray({{"Open", "notes.open"}}, "Notes")) << "no tray without a system's (headless)";
    };
}};
