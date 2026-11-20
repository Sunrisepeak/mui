// The site's one page without a window (G5): the player runs its TypeScript; an agent sets each of its four states
// through its verb (`page.state`), as the sketches were drawn, and reads what each says in the semantic tree — the
// mark, the sentence, the Live Design key and the install at rest; the key a pause and the design's course at its
// step in Live Design; the copy done while it waits; the visitor's agent beside the mark once connected. Each state is
// a picture of its own, and the README's (scratch "readme-site": omui-devtools media site copies them in).
import std;
import mcpplibs.mtest;
import mlibs.testing;
import omui;
import omui.player;
import omui.entry.player;
import omui.lang.js;

using namespace mcpplibs::mtest;
using namespace mlibs::testing;
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

// The README's picture of a state, under 1 MiB.
void still(Headless& host, std::string_view state) {
    static const auto pictures = scratch_dir("readme-site");  // made once: asked again, it starts empty
    const auto file = pictures / std::format("state-{}.png", state);
    expect(host.save_png(file).has_value() && std::filesystem::file_size(file) < 1024 * 1024) << file.generic_string();
}

bool shows(Headless& host, Role role, std::string_view name) { return !host.tree().find({.role = role, .name = std::string(name)}).empty(); }

// The first node under `at`, in the tree's order, whose own id is `id` (what the page draws, named or not).
NodeId named(const Runtime& runtime, NodeId at, std::string_view id) {
    for (const NodeId child : runtime.children(at)) {
        if (const std::string path = runtime.path(child); path == id || path.ends_with(std::format("/{}", id))) return child;
        if (const NodeId deeper = named(runtime, child, id); deeper) return deeper;
    }
    return {};
}

}  // namespace

// The components the page's build derived from its scripts (build.mcpp: closure(), B7b): it is tested with what its
// program carries, not with every base component.
extern "C++" void omui_application_components();

static UTest cases{"site", [] {
    omui_application_components();

    "four states set through its verb, each said in the tree and drawn as a picture of its own"_test = [] {
        auto manifest = read_manifest(std::filesystem::path(std::source_location::current().file_name()).parent_path().parent_path() / "mcpp.toml");
        expect(fatal(manifest.has_value())) << (manifest ? std::string() : manifest.error());
        Headless host({.size = manifest->size});
        // The Web host's part (the agent link, spec §6), as it plays it: looking starts with a code and says so.
        host.runtime().set_agent_linker({[&] {
                                             host.runtime().agent_link_changed({"waiting", "7KQ2-M9X4", {}, {}, 0});
                                             return std::string("7KQ2-M9X4");
                                         },
                                         [] {}});
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
        still(host, "rest");

        // Live Design: the key pauses; the design's course on the mark's O stands at its step, said in the step's words.
        expect(eq(set("design"), 0));
        expect(shows(host, Role::button, "Pause") && !shows(host, Role::button, "Live Design"));
        const auto course = host.tree().find({.role = Role::slider, .name = "The page's design"});
        expect(fatal(eq(course.size(), std::size_t{1})));
        expect(eq(course.front().value.get("value")->as_double(), 3.0) && eq(course.front().value.get("max")->as_double(), 8.0));
        expect(eq(course.front().description, std::string("The sentence")));
        const auto design = picture(host);
        still(host, "design");

        // Waiting: copied, the course gone.
        expect(eq(set("waiting"), 0));
        expect(shows(host, Role::button, "Copied") && host.tree().find({.role = Role::slider}).empty());
        const auto waiting = picture(host);
        still(host, "waiting");
        // And the O breathes (the style's ambient motion, spec M-7), sampled over a whole breath: its ring 4 % wider at
        // the top of it than at the bottom; a frame of its own asked for, which no run waits for.
        const NodeId ring = named(host.runtime(), host.runtime().root(), "breath");
        expect(fatal(host.runtime().valid(ring)));
        std::vector<float> widths;
        for (int sample = 0; sample < 16; ++sample) {
            (void)host.advance(seconds(0.2));
            widths.push_back(host.runtime().drawn_bounds(ring).width);
        }
        const auto [narrowest, widest] = std::ranges::minmax(widths);
        expect(widest / narrowest > 1.035f && widest / narrowest < 1.045f) << std::format("breathing: {} to {}", narrowest, widest);
        expect(host.runtime().next_timer().has_value() && !host.runtime().next_work().has_value()) << "its frames ambient";

        // Connected (its sketch: no agent has paired): the mark holds Mui, her head in the circle; beside it, where
        // the visitor's agent will be named.
        expect(eq(set("connected"), 0));
        expect(shows(host, Role::image, "Mui") && !shows(host, Role::image, "omui")) << "the O is Mui";
        expect(!host.tree().find({.name = "Your agent · omui · your machine"}).empty()) << "the agent's chip, before one pairs";
        const auto connected = picture(host);
        still(host, "connected");

        const std::set<std::vector<std::byte>> pictures{rest, design, waiting, connected};
        expect(eq(pictures.size(), std::size_t{4})) << "four pictures";
        // Back at rest, the first picture again, and nothing asks for a frame (the ring unseen rests).
        expect(eq(set("rest"), 0));
        expect(picture(host) == rest) << "at rest again, as it was";
        expect(!host.runtime().next_timer().has_value()) << "at rest: no frame asked for";

        // The word becomes Mui (the style's `gather`): sampled over its course — her pixels on their way out of the
        // word's strokes, neither picture part-way, the connected picture exactly at its end; the frames kept for a
        // person to look at (scratch "site-gather").
        {
            Invocation call;
            call.actor = {"agent", "site-test", {}};
            call.args = Value{}.set("state", "connected");
            expect(eq(host.runtime().verbs().invoke("page.state", call).exit_code().value_or(-1), 0));
            const auto frames = scratch_dir("site-gather");
            std::vector<std::vector<std::byte>> between;
            for (int step = 1; step <= 4; ++step) {
                (void)host.advance(seconds(0.25));
                between.push_back(picture(host));
                expect(host.save_png(frames / std::format("gather-{}.png", step)).has_value());
            }
            expect(between[0] != rest && between[0] != connected && between[1] != rest && between[1] != connected) << "part-way, neither";
            expect(between[0] != between[1]) << "moving";
            settle();
            expect(picture(host) == connected) << "at its end, the connected picture exactly";
            expect(eq(set("rest"), 0));
            expect(picture(host) == rest) << "and back to the word";
        }

        // Copy, pressed as a person presses it: the Agent tab's one sentence on the clipboard (the pairing code in
        // it), and the page waits for the agent.
        const auto copy_key = host.tree().find({.role = Role::button, .name = "Copy"});
        expect(fatal(eq(copy_key.size(), std::size_t{1})));
        Invocation press;
        press.actor = {"person", "visitor", {}};
        expect(eq(host.tree().act(copy_key.front().id, "activate", press).exit_code().value_or(-1), 0));
        settle();
        const std::string copied = host.runtime().clipboard_text();
        expect(copied.starts_with("Install omui") && copied.contains("omui connect 7KQ2-M9X4") && copied.contains("describe"))
            << "the link's own code: " << copied;
        expect(shows(host, Role::button, "Copied")) << "waiting";
        expect(eq(host.runtime().agent_link().state, std::string("waiting")));

        // The visitor's agent runs `omui connect 7KQ2-M9X4`: the bridge pairs, and the page hears who and with what.
        AgentLink paired{"paired", "7KQ2-M9X4", "Claude Code", {}, 47110};
        paired.version = "0.1.0";
        paired.platform = "linux-x86_64";
        host.runtime().agent_link_changed(paired);
        settle();
        expect(shows(host, Role::image, "Mui")) << "connected: the O is Mui";
        expect(!host.tree().find({.name = "Claude Code · omui 0.1.0 · linux-x86_64"}).empty()) << "the agent, its omui, its machine";
        // The bridge goes: waiting again, the O open.
        AgentLink lost = paired;
        lost.state = "lost";
        host.runtime().agent_link_changed(lost);
        settle();
        expect(shows(host, Role::image, "omui") && shows(host, Role::button, "Copied")) << "lost: waiting again";
    };
}};
