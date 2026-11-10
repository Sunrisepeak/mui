// `omui dev` for a C++ application, end to end (W16, D-154): omui-devhost owns
// the window (headless here) and builds example 01 in the background; each
// build that succeeds runs in a process of its own, drawn in the host's window;
// a change builds again and the new build takes over; a build that fails shows
// its problems and the one running stays; fixed, it takes over again. Read as an
// agent reads it: the host's verb dev.status over its endpoint; a failure's
// problem is pinned beside the element built nearest above its line. The design's history is the
// host's: a commit, a change, a revert written back to the source and built — the history going on
// across the builds. It runs `mcpp build` in this
// workspace, so it runs outside `mcpp test` (built with --no-run; mcpp#915).
import std;
import omui.agent;
import mcpplibs.os;
import mcpplibs.mtest;
import mcpplibs.mtest.e2e;

using namespace mcpplibs::mtest;
namespace os = mcpplibs::os;

namespace {

const std::filesystem::path repository = std::filesystem::path(std::source_location::current().file_name()).parent_path().parent_path().parent_path().parent_path().parent_path();

// Example 01's source, put back however the case ends.
struct Kept {
    std::filesystem::path file;
    std::string           text;
    explicit Kept(std::filesystem::path path) : file(std::move(path)), text(os::fs::read(file).value_or("")) {}
    ~Kept() { (void)os::fs::write_atomic(file, text); }
    void write(std::string_view changed) const { (void)os::fs::write_atomic(file, changed); }
};

}  // namespace

E2E devhost{"devhost", resource{"serial:examples/01-hello-im"}, timeout{std::chrono::minutes{15}}, [] {
    "builds come and go in the host's window: shown, taken over, a failure shown with the one running kept, fixed"_test[artifact{"target:omui-devhost"}] = [] {
        const auto project = repository / "examples" / "01-hello-im";
        const Kept source(project / "src" / "hello.cppm");
        const auto dir = std::filesystem::temp_directory_path() / std::format("omui-devhost-{}", std::random_device{}());
        std::filesystem::create_directories(dir);
        os::process::spawn_options options;
        options.program = os::fs::from_utf8(e2e::program("omui-devhost"));
        options.args = {"--project", os::fs::utf8(project), "--serve", "--agent-dir", os::fs::utf8(dir)};
        options.out = options.err = os::process::stdio::inherit;  // the builds' output with the case's
        options.bound_lifetime = true;
        auto host = os::process::process::spawn(options);
        expect(host.has_value());
        if (!host) return;
        std::optional<omui::agent::Client> client;
        for (int tries = 0; tries < 200 && !client; ++tries) {
            if (auto connected = omui::agent::Client::connect("org.openmui.devhost", dir)) client.emplace(std::move(*connected));
            else std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        expect(client.has_value()) << "the host's endpoint";
        if (!client) return;
        omui::Value last;
        const auto status = [&] {
            for (const omui::Value& record : client->call(omui::Value{}.set("op", "invoke").set("verb", "dev.status")))
                if (record.get("kind") && record.get("kind")->as_string() == "data") last = *record.get("data");
            return last;
        };
        // A design verb's data and exit code.
        const auto invoke = [&](std::string_view verb, omui::Value args) {
            omui::Value data;
            std::int64_t code = -1;
            for (const omui::Value& record : client->call(omui::Value{}.set("op", "invoke").set("verb", verb).set("args", std::move(args)))) {
                const std::string_view kind = record.get("kind") ? record.get("kind")->as_string() : std::string_view{};
                if (kind == "data") data = *record.get("data");
                if (kind == "result") code = record.get("exitCode")->as_int();
            }
            return std::pair{data, code};
        };
        const auto number = [](const omui::Value& value, std::string_view key) { return value.get(key) && value.get(key)->is_number() ? value.get(key)->as_int() : -1; };
        const auto wait_for = [&](auto holds) {
            for (int tries = 0; tries < 900; ++tries) {
                if (holds(status())) return true;
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
            return false;
        };
        expect(wait_for([&](const omui::Value& s) { return number(s, "shown") == 1 && number(s, "frames") > 0; })) << "the first build shown: " << last.json();
        std::string changed = source.text;
        changed.replace(changed.find("\"Hello, openmui\", {.style"), 16, "\"Hello again, openmui\"");
        source.write(changed);
        expect(wait_for([&](const omui::Value& s) { return number(s, "shown") == 2 && !s.get("fading")->as_bool(); })) << "a change taken over: " << last.json();
        // A mistake inside the view, under the title: its problem shown beside the title.
        std::string broken = changed;
        broken.insert(broken.find('\n', broken.find("\"Hello again, openmui\"")) + 1, "                    int broken = \"not a number\";\n");
        source.write(broken);
        expect(wait_for([&](const omui::Value& s) {
            return number(s, "failed") >= 1 && number(s, "problems") >= 1 && number(s, "pinned") >= 1 && number(s, "shown") == 2;
        })) << "a failure shown beside its element, the one running kept: " << last.json();
        source.write(source.text);
        expect(wait_for([&](const omui::Value& s) { return number(s, "shown") == 3 && number(s, "problems") == 0; })) << "fixed, taken over: " << last.json();
        // The design's history, the host's: kept, changed, put back — written and built, the history going on.
        const std::filesystem::path design = project / ".omui" / "design";
        std::filesystem::remove_all(design);
        const auto [kept, kept_code] = invoke("design.history.commit", omui::Value{}.set("message", "hello again"));
        expect(eq(kept_code, std::int64_t{0}) && kept.get("id")) << kept.json();
        std::string once_more = source.text;
        once_more.replace(once_more.find("\"Hello, openmui\", {.style"), 16, "\"Hello once more, openmui\"");
        source.write(once_more);
        expect(wait_for([&](const omui::Value& s) { return number(s, "shown") == 4 && !s.get("fading")->as_bool(); })) << "the change built: " << last.json();
        const auto [before, before_code] = invoke("design.history", omui::Value(omui::Value::Map{}));
        expect(eq(before_code, std::int64_t{0}) && before.get("uncommitted") && before.get("uncommitted")->as_bool() &&
               before.get("log")->list()->size() == 1)
            << "one commit, and a change not committed: " << before.json();
        const auto [second, second_code] = invoke("design.history.commit", omui::Value{}.set("message", "once more"));
        expect(eq(second_code, std::int64_t{0})) << second.json();
        // Back a step: the first commit's text written and built, kept as a new commit (nothing lost).
        const auto [reverted, reverted_code] = invoke("design.history.revert", omui::Value{}.set("to", "HEAD~1"));
        expect(eq(reverted_code, std::int64_t{0}) && reverted.get("commit")) << reverted.json();
        expect(eq(os::fs::read(source.file).value_or(""), source.text)) << "the commit's text written back";
        expect(wait_for([&](const omui::Value& s) { return number(s, "shown") == 5 && !s.get("fading")->as_bool(); })) << "and built, taken over: " << last.json();
        const auto [after, after_code] = invoke("design.history", omui::Value(omui::Value::Map{}));
        expect(eq(after_code, std::int64_t{0}) && after.get("log")->list()->size() == 3 && !after.get("uncommitted")->as_bool())
            << "the history went on across the builds: " << after.json();
        std::filesystem::remove_all(design);
        host->kill();
        std::filesystem::remove_all(dir);
    };
}};
