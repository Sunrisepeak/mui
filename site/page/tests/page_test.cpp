// The site's one page without a window (G5): the player runs its TypeScript; an agent sets each of its four states
// through its verb (`page.state`), as the sketches were drawn, and reads what each says in the semantic tree — the
// mark, the sentence, the Live Design key and the install at rest; the key a pause and the design's course at its
// step in Live Design; the copy done while it waits; the visitor's agent beside the mark once connected. Each state is
// a picture of its own.
import std;
import mcpplibs.mtest;
import omui;
import omui.player;
import omui.entry.player;
import omui.lang.js;

using namespace mcpplibs::mtest;
using namespace omui;

namespace {

// What it draws, byte for byte.
std::vector<std::byte> picture(Headless& host) {
    const auto& pixels = host.pixels();
    std::vector<std::byte> bytes;
    for (int row = 0; row < pixels.height(); ++row) {
        const auto line = std::as_bytes(std::span(pixels.row(row), static_cast<std::size_t>(pixels.width())));
        bytes.insert(bytes.end(), line.begin(), line.end());
    }
    return bytes;
}

bool shows(Headless& host, Role role, std::string_view name) { return !host.tree().find({.role = role, .name = std::string(name)}).empty(); }

}  // namespace

static UTest cases{"site.page", [] {
    register_base_components();

    "four states set through its verb, each said in the tree and drawn as a picture of its own"_test = [] {
        auto manifest = read_manifest(std::filesystem::path(std::source_location::current().file_name()).parent_path().parent_path() / "mcpp.toml");
        expect(fatal(manifest.has_value())) << (manifest ? std::string() : manifest.error());
        Headless host({.size = manifest->size});
        player::Player page(host.runtime(), *manifest, {lang::js::typescript()});
        const auto started = page.start();
        expect(fatal(started.has_value())) << (started ? std::string() : started.error());
        const auto settle = [&] {
            for (int turn = 0; turn < 4000 && !page.idle(); ++turn) {
                host.step();
                std::this_thread::sleep_for(std::chrono::microseconds(250));
            }
            host.settle();
        };
        const auto set = [&](std::string_view state) {
            Invocation call;
            call.actor = {"agent", "site-test", {}};
            call.args = Value{}.set("state", std::string(state));
            const Envelope done = host.runtime().verbs().invoke("page.state", call);
            settle();
            return done.exit_code().value_or(-1);
        };
        settle();

        // At rest: the mark, the sentence (the page's one heading), the Live Design key, the install's two tabs and copy.
        expect(shows(host, Role::image, "omui")) << "the mark";
        const auto headings = host.tree().find({.role = Role::heading});
        expect(eq(headings.size(), std::size_t{1}) && headings.front().name.starts_with("A UI framework")) << "one sentence";
        expect(shows(host, Role::button, "Live Design") && shows(host, Role::button, "Copy") && shows(host, Role::link, "GitHub"));
        const auto rest = picture(host);

        // Live Design: the key pauses; the design's course on the mark's O stands at its step, said in the step's words.
        expect(eq(set("design"), 0));
        expect(shows(host, Role::button, "Pause") && !shows(host, Role::button, "Live Design"));
        const auto course = host.tree().find({.role = Role::slider, .name = "The page's design"});
        expect(fatal(eq(course.size(), std::size_t{1})));
        expect(eq(course.front().value.get("value")->as_double(), 3.0) && eq(course.front().value.get("max")->as_double(), 8.0));
        expect(eq(course.front().description, std::string("The sentence")));
        const auto design = picture(host);

        // Waiting: copied, the course gone.
        expect(eq(set("waiting"), 0));
        expect(shows(host, Role::button, "Copied") && host.tree().find({.role = Role::slider}).empty());
        const auto waiting = picture(host);

        // Connected: the mark has become Mui (the O with ears her outline, her face in it); the visitor's agent and its
        // omui beside it.
        expect(eq(set("connected"), 0));
        expect(shows(host, Role::image, "Mui") && !shows(host, Role::image, "omui")) << "the O is Mui";
        expect(!host.tree().find({.name = "Claude Code · omui 0.1.0 · linux-x64"}).empty()) << "the agent's chip";
        const auto connected = picture(host);

        const std::set<std::vector<std::byte>> pictures{rest, design, waiting, connected};
        expect(eq(pictures.size(), std::size_t{4})) << "four pictures";
        // Back at rest, the first picture again.
        expect(eq(set("rest"), 0));
        expect(picture(host) == rest) << "at rest again, as it was";

        // Copy, pressed as a person presses it: the Agent tab's one sentence on the clipboard (the pairing code in
        // it), and the page waits for the agent.
        const auto copy_key = host.tree().find({.role = Role::button, .name = "Copy"});
        expect(fatal(eq(copy_key.size(), std::size_t{1})));
        Invocation press;
        press.actor = {"person", "visitor", {}};
        expect(eq(host.tree().act(copy_key.front().id, "activate", press).exit_code().value_or(-1), 0));
        settle();
        const std::string copied = host.runtime().clipboard_text();
        expect(copied.starts_with("Install omui") && copied.contains("omui connect 7KQ2-M9") && copied.contains("describe")) << copied;
        expect(shows(host, Role::button, "Copied")) << "waiting";
    };
}};
