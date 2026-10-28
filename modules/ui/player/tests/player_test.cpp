// The player: an application's components, views and scripts started from a
// manifest; files that change on disk reloaded in place, state kept; errors
// kept with their places while the old view stays.
import std;
import openmui.core;
import openmui.view;
import openmui.abi;
import openmui.player;
import openmui.ui.base;
import openmui.host.headless;
import openmui.testing;

using namespace omui::testing;
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

player::Language stub() {
    return {"stub", {".stub"}, [](abi::OmuiRuntime rt) -> std::unique_ptr<player::Engine> { return std::make_unique<Stub>(rt); }, {}};
}

constexpr std::string_view counter = R"j({"omui": "0.1", "view": {"signals": {"count": {"type": "int"}},
  "root": {"type": "column", "children": [
    {"type": "badge", "id": "badge", "props": {"label": "clicks"}},
    {"type": "text", "id": "n", "props": {"text": {"expr": "format('{} clicks', count)"}}},
    {"type": "button", "id": "inc", "props": {"text": "+1"}, "on": {"activated": [{"set": "count", "expr": "count + 1"}]}}]}}})j";

constexpr std::string_view badge = R"j({"omui": "0.1", "component": {"name": "badge",
  "interface": {"props": {"label": {"type": "string"}}},
  "root": {"type": "text", "id": "t", "props": {"text": {"expr": "upper(label)"}}}}})j";

std::string name_of(Headless& h, std::string_view id) {
    auto n = h.tree().node(id);
    return n ? n->name : std::string("<none>");
}

}  // namespace

int main(int argc, char** argv) {
    Suite s{"player"};
    register_base_components();

    s.test("a manifest's components, views and scripts start; saved files reload in place", [] {
        const auto dir = scratch_dir("player");
        write(dir / "ui" / "main.view.omui.json", counter);
        write(dir / "ui" / "badge.view.omui.json", badge);
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
        check(started.has_value(), started ? "" : started.error());
        h.settle();
        check(p.idle());
        check_eq(name_of(h, "main/badge/t"), std::string("CLICKS"));
        check_eq(name_of(h, "side"), std::string("from a script"));
        // (The script's view is mounted over the main one: an agent's action reaches the button.)
        Invocation call;
        call.actor = {"agent", "test", {}};
        const auto e = h.tree().act("main/inc", "activate", call);
        check_eq(e.exit_code().value_or(-1), 0);
        h.settle();
        check_eq(name_of(h, "main/n"), std::string("1 clicks"));

        // A view saved: in place, its state kept.
        std::string next(counter);
        next.replace(next.find("{} clicks"), 9, "{} taps");
        write(dir / "ui" / "main.view.omui.json", next);
        check(p.poll() == std::vector<std::string>{"ui/main.view.omui.json"});
        h.settle();
        check_eq(name_of(h, "main/n"), std::string("1 taps"));
        // A component saved: its instances rebuilt.
        std::string loud(badge);
        loud.replace(loud.find("upper(label)"), 12, "format('<{}>', label)");
        write(dir / "ui" / "badge.view.omui.json", loud);
        check(p.poll() == std::vector<std::string>{"ui/badge.view.omui.json"});
        h.settle();
        check_eq(name_of(h, "main/badge/t"), std::string("<clicks>"));
        // A script saved: run again.
        write(dir / "logic" / "side.stub", R"j({"omui": "0.1", "view": {"root": {"type": "text", "props": {"text": "again"}}}})j");
        check(p.poll() == std::vector<std::string>{"logic/side.stub"});
        h.settle();
        check_eq(name_of(h, "side"), std::string("again"));
        // A view saved with a mistake: nothing changes, the problem says where.
        write(dir / "ui" / "main.view.omui.json", R"j({"omui": "0.1", "view": {"root": {"type": "colum"}}})j");
        check(p.poll().empty());
        check(!p.problems().empty() && p.problems().back().find("ui/main.view.omui.json:1:43: error: no component is named `colum`") != std::string::npos,
              p.problems().empty() ? "" : p.problems().back());
        h.settle();
        check_eq(name_of(h, "main/n"), std::string("1 taps"));
    });

    s.test("watch reloads on the runtime's timers", [] {
        const auto dir = scratch_dir("player-watch");
        write(dir / "ui" / "main.view.omui.json", R"j({"omui": "0.1", "view": {"root": {"type": "text", "props": {"text": "one"}}}})j");
        player::Manifest m;
        m.base = dir;
        m.views = {"ui/main.view.omui.json"};
        Headless h(HeadlessOptions{.size = {320, 240}});
        player::Player p(h.runtime(), m, {});
        check(p.start().has_value());
        p.watch(seconds(0.1));
        h.settle(seconds(0.05));
        check_eq(name_of(h, "main"), std::string("one"));
        write(dir / "ui" / "main.view.omui.json", R"j({"omui": "0.1", "view": {"root": {"type": "text", "props": {"text": "two"}}}})j");
        (void)h.advance(seconds(0.2));
        check_eq(name_of(h, "main"), std::string("two"));
    });

    s.test("what cannot start is a problem, not a crash", [] {
        const auto dir = scratch_dir("player-problems");
        player::Manifest m;
        m.base = dir;
        m.views = {"ui/missing.view.omui.json"};
        m.scripts = {{"cobol", "logic/main.cbl"}};
        Headless h(HeadlessOptions{.size = {320, 240}});
        player::Player p(h.runtime(), m, {stub()});
        auto r = p.start();
        check(!r);
        check(r.error().find("ui/missing.view.omui.json: cannot be read") != std::string::npos, r.error());
        check(r.error().find("logic/main.cbl: no language `cobol`") != std::string::npos, r.error());
    });

    return s.run(argc, argv);
}
