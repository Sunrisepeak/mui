// The player: an application's components, views and scripts started from a
// manifest; files that change on disk reloaded in place, state kept; errors
// kept with their places while the old view stays.
import std;
import omui.core;
import omui.view;
import omui.abi;
import omui.player;
import omui.semantic;
import omui.ui.base;
import omui.host.headless;
import mlibs.image;
import mlibs.testing;
import mcpplibs.mtest;

using namespace mlibs::testing;
using namespace mcpplibs::mtest;
using namespace omui;

namespace {

void write(const std::filesystem::path& p, std::string_view text) {
    std::filesystem::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary | std::ios::trunc) << text;
    // A modification time no file system can confuse with the last one.
    static int bump = 0;
    std::filesystem::last_write_time(p, std::filesystem::file_time_type::clock::now() + std::chrono::seconds(++bump));
}

// A language for the test: a "script" is a view document, mounted at "side"
// through the C ABI, as a binding's mount would.
class Stub final : public player::Engine {
public:
    explicit Stub(abi::OmuiRuntime rt) : rt_(rt) {}
    std::future<player::Status> run(std::string code, std::string) override { return mount(code); }
    std::future<player::Status> reload(std::string code, std::string) override { return mount(code); }
    player::Status reload_now(std::string code, std::string) override { return mount(code).get(); }
    [[nodiscard]] bool idle() const override { return true; }

private:
    std::future<player::Status> mount(const std::string& code) {
        std::promise<player::Status> p;
        if (abi::omuiMount(rt_, {"side", 4}, {code.data(), code.size()}) != abi::OMUI_SUCCESS) p.set_value(std::unexpected(std::string("refused")));
        else p.set_value({});
        return p.get_future();
    }
    abi::OmuiRuntime rt_;
};

// A language whose script imports one module by path (logic/part.stub): it
// says so, and each run mounts that module's text (its edit, or the file's).
struct ModuleLog {
    std::vector<std::string> changes;  // module_changed: "<file>=<text or (file)>"
    int                      runs = 0;
};

class ModuleStub final : public player::Engine {
public:
    ModuleStub(abi::OmuiRuntime rt, std::filesystem::path part, ModuleLog& log) : rt_(rt), part_(std::move(part)), log_(log) {}
    std::future<player::Status> run(std::string, std::string) override { return ready(mount()); }
    std::future<player::Status> reload(std::string, std::string) override { return ready(mount()); }
    player::Status reload_now(std::string, std::string) override { return mount(); }
    [[nodiscard]] bool idle() const override { return true; }
    void module_changed(const std::filesystem::path& file, std::optional<std::string> text) override {
        log_.changes.push_back(std::format("{}={}", file.filename().generic_string(), text ? *text : std::string("(file)")));
        edit_ = std::move(text);
    }
    [[nodiscard]] std::vector<std::filesystem::path> modules() const override { return {part_}; }

private:
    static std::future<player::Status> ready(player::Status s) {
        std::promise<player::Status> p;
        p.set_value(std::move(s));
        return p.get_future();
    }
    player::Status mount() {
        ++log_.runs;
        std::ifstream in(part_, std::ios::binary);
        const std::string code = edit_ ? *edit_ : std::string{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
        if (abi::omuiMount(rt_, {"side", 4}, {code.data(), code.size()}) != abi::OMUI_SUCCESS) return std::unexpected(std::string("refused"));
        return {};
    }
    abi::OmuiRuntime             rt_;
    std::filesystem::path        part_;
    ModuleLog&                   log_;
    std::optional<std::string>   edit_;
};

player::Language stub() {
    return {"stub", {".stub"}, [](abi::OmuiRuntime rt) -> std::unique_ptr<player::Engine> { return std::make_unique<Stub>(rt); }, {}};
}

constexpr std::string_view counter = R"j({"omui": "0.1", "view": {"signals": {"count": {"type": "int"}},
  "root": {"type": "column", "children": [
    {"type": "tally_tag", "id": "badge", "props": {"label": "clicks"}},
    {"type": "text", "id": "n", "props": {"text": {"expr": "format('{} clicks', count)"}}},
    {"type": "button", "id": "inc", "props": {"text": "+1"}, "on": {"activated": [{"set": "count", "expr": "count + 1"}]}}]}}})j";

constexpr std::string_view badge_doc = R"j({"omui": "0.1", "component": {"name": "tally_tag",
  "interface": {"props": {"label": {"type": "string"}}},
  "root": {"type": "text", "id": "t", "props": {"text": {"expr": "upper(label)"}}}}})j";

std::string file_text(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::string name_of(Headless& h, std::string_view id) {
    auto n = h.tree().node(id);
    return n ? n->name : std::string("<none>");
}

}  // namespace

UTest cases{"player", [] {
    register_base_components();

    "a manifest's components, views and scripts start; saved files reload in place"_test = [] {
        const auto dir = scratch_dir("player");
        write(dir / "ui" / "main.view.omui.json", counter);
        write(dir / "ui" / "badge.view.omui.json", badge_doc);
        write(dir / "logic" / "side.stub", R"j({"omui": "0.1", "view": {"root": {"type": "text", "id": "hello", "props": {"text": "from a script"}}}})j");
        player::Manifest m;
        m.id = "org.openmui.player-test";
        m.base = dir;
        m.components = {"ui/badge.view.omui.json"};
        m.views = {"ui/main.view.omui.json"};
        m.scripts = {{"stub", "logic/side.stub"}};
        Headless h(HeadlessOptions{.size = {320, 240}});
        player::Player p(h.runtime(), m, {stub()});
        auto started = p.start();
        expect(started.has_value()) << (started ? "" : started.error());
        h.settle();
        expect(p.idle());
        expect(eq(name_of(h, "main/badge/t"), std::string("CLICKS")));
        expect(eq(name_of(h, "side"), std::string("from a script")));
        // (The script's view is mounted over the main one: an agent's action reaches the button.)
        Invocation call;
        call.actor = {"agent", "test", {}};
        const auto e = h.tree().act("main/inc", "activate", call);
        expect(eq(e.exit_code().value_or(-1), 0));
        h.settle();
        expect(eq(name_of(h, "main/n"), std::string("1 clicks")));

        // A view saved: in place, its state kept.
        std::string next(counter);
        next.replace(next.find("{} clicks"), 9, "{} taps");
        write(dir / "ui" / "main.view.omui.json", next);
        expect(p.poll() == std::vector<std::string>{"ui/main.view.omui.json"});
        h.settle();
        expect(eq(name_of(h, "main/n"), std::string("1 taps")));
        // A component saved: its instances rebuilt.
        std::string loud(badge_doc);
        loud.replace(loud.find("upper(label)"), 12, "format('<{}>', label)");
        write(dir / "ui" / "badge.view.omui.json", loud);
        expect(p.poll() == std::vector<std::string>{"ui/badge.view.omui.json"});
        h.settle();
        expect(eq(name_of(h, "main/badge/t"), std::string("<clicks>")));
        // A script saved: run again.
        write(dir / "logic" / "side.stub", R"j({"omui": "0.1", "view": {"root": {"type": "text", "props": {"text": "again"}}}})j");
        expect(p.poll() == std::vector<std::string>{"logic/side.stub"});
        h.settle();
        expect(eq(name_of(h, "side"), std::string("again")));
        // A view saved with a mistake: nothing changes, the problem says where.
        write(dir / "ui" / "main.view.omui.json", R"j({"omui": "0.1", "view": {"root": {"type": "colum"}}})j");
        expect(p.poll().empty());
        expect(!p.problems().empty() && p.problems().back().find("ui/main.view.omui.json:1:43: error: no component is named `colum`") != std::string::npos) << (p.problems().empty() ? "" : p.problems().back());
        h.settle();
        expect(eq(name_of(h, "main/n"), std::string("1 taps")));
    };

    "watch reloads on the runtime's timers"_test = [] {
        const auto dir = scratch_dir("player-watch");
        write(dir / "ui" / "main.view.omui.json", R"j({"omui": "0.1", "view": {"root": {"type": "text", "props": {"text": "one"}}}})j");
        player::Manifest m;
        m.base = dir;
        m.views = {"ui/main.view.omui.json"};
        Headless h(HeadlessOptions{.size = {320, 240}});
        player::Player p(h.runtime(), m, {});
        expect(p.start().has_value());
        p.watch(seconds(0.1));
        h.settle(seconds(0.05));
        expect(eq(name_of(h, "main"), std::string("one")));
        write(dir / "ui" / "main.view.omui.json", R"j({"omui": "0.1", "view": {"root": {"type": "text", "props": {"text": "two"}}}})j");
        (void)h.advance(seconds(0.2));
        expect(eq(name_of(h, "main"), std::string("two")));
    };

    "what cannot start is a problem, not a crash"_test = [] {
        const auto dir = scratch_dir("player-problems");
        player::Manifest m;
        m.base = dir;
        m.views = {"ui/missing.view.omui.json"};
        m.scripts = {{"cobol", "logic/main.cbl"}};
        Headless h(HeadlessOptions{.size = {320, 240}});
        player::Player p(h.runtime(), m, {stub()});
        auto r = p.start();
        expect(!r);
        expect(r.error().find("ui/missing.view.omui.json: cannot be read") != std::string::npos) << r.error();
        expect(r.error().find("logic/main.cbl: no language `cobol`") != std::string::npos) << r.error();
    };

    "out of development mode there are no design verbs"_test = [] {
        const auto dir = scratch_dir("player-no-design");
        write(dir / "logic" / "side.stub", R"j({"omui": "0.1", "view": {"root": {"type": "text", "props": {"text": "x"}}}})j");
        player::Manifest m;
        m.id = "org.openmui.player-no-design";
        m.base = dir;
        m.scripts = {{"stub", "logic/side.stub"}};
        Headless h(HeadlessOptions{.size = {320, 240}});
        player::Player p(h.runtime(), m, {stub()});
        expect(p.start().has_value());
        expect(h.runtime().verbs().find("design.edit") == nullptr);
    };

    "development mode without a designer linked: no design verbs, and it says so"_test = [] {
        const auto dir = scratch_dir("player-no-designer");
        write(dir / "logic" / "side.stub", R"j({"omui": "0.1", "view": {"root": {"type": "column", "children": [{"type": "text", "id": "hello", "props": {"text": "hi"}}]}}})j");
        player::Manifest m;
        m.id = "org.openmui.player-no-designer";
        m.base = dir;
        m.scripts = {{"stub", "logic/side.stub"}};
        Headless h(HeadlessOptions{.size = {320, 240}});
        player::Player p(h.runtime(), m, {stub()}, {.design = true});
        const auto started = p.start();
        expect(!started.has_value() && started.error().contains("no designer")) << (started ? std::string("started") : started.error());
        h.settle();
        expect(eq(name_of(h, "side/hello"), std::string("hi"))) << "the application runs all the same";
        expect(h.runtime().verbs().find("design.edit") == nullptr);
    };

}};
