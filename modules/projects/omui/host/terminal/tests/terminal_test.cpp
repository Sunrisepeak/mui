// The terminal host: a terminal's bytes as events; the window's components, unchanged, in cells — the picture a
// text baseline; keys and the mouse operate it as a person would; an agent reads the picture and goes from a cell
// back to the node that drew it; the agent contract's suite on its subject.
import std;
import mlibs.agent.cts;
import mlibs.image;
import mlibs.json;
import mlibs.testing;
import omui.agent;
import omui.host.terminal;
import omui.ui.base;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
using namespace mlibs::testing;
using namespace omui;
namespace kernel = mlibs::agent;
namespace contract = mlibs::agent::cts;

namespace {

// Example 03's counter, in brief: a count, the way to the goal, and the buttons that change it.
struct Counter {
    Signal<int> count = signal(0);
    View view() const {
        const Signal<int> c = count;
        return column({.padding = 16, .gap = 16}, {
            text({.id = "count", .text = derive([=] { return std::format("Count {}", c()); })}),
            row({.id = "controls", .gap = 8}, {
                button({.id = "minus", .text = "-1", .on_activated = [=] { c.set(std::max(c.peek() - 1, 0)); }}),
                button({.id = "plus", .text = "+1", .variant = ButtonVariant::primary, .on_activated = [=] { c.set(c.peek() + 1); }}),
            }),
        });
    }
};

std::string mouse_click(int column, int row) {
    return std::format("\x1b[<0;{};{}M\x1b[<0;{};{}m", column + 1, row + 1, column + 1, row + 1);
}

struct Answered {
    std::optional<mlibs::json::Document> data;
    std::string                          error;
    [[nodiscard]] mlibs::json::Node operator[](std::string_view key) const { return data->root()["data"][key]; }
};
Answered ask(kernel::SubjectSession& session, std::string_view line) {
    Answered out;
    session.request(line, [&](std::string_view record) {
        auto parsed = mlibs::json::Document::parse(record, "record");
        expect(fatal(parsed.has_value())) << record;
        const auto kind = parsed->root()["kind"].text();
        if (kind == "error") out.error = parsed->root()["code"].text().value_or("");
        if (kind == "data" && !out.data) out.data.emplace(std::move(*parsed));
    });
    return out;
}

// A subject that owns the terminal it shows: the contract suite makes a new one for each case.
class Owned final : public kernel::Subject {
public:
    Owned() : terminal_(std::make_unique<Terminal>(TerminalOptions{.columns = 30, .rows = 6, .agent = false})), subject_(*terminal_) {
        terminal_->mount("main", counter_.view());
        terminal_->settle();
    }
    [[nodiscard]] std::string_view name() const noexcept override { return subject_.name(); }
    [[nodiscard]] std::string_view interface_name() const noexcept override { return subject_.interface_name(); }
    [[nodiscard]] kernel::Capabilities capabilities() const noexcept override { return subject_.capabilities(); }
    [[nodiscard]] std::span<const mlibs::control::CatalogEntry> operations() const noexcept override { return subject_.operations(); }
    [[nodiscard]] std::string_view listing() const noexcept override { return subject_.listing(); }
    [[nodiscard]] mlibs::control::Guide guide() const noexcept override { return subject_.guide(); }
    [[nodiscard]] std::uint64_t state_hash() const override { return subject_.state_hash(); }
    void operate(std::string_view operation, const kernel::Request& request, kernel::Answer& answer) override { subject_.operate(operation, request, answer); }

private:
    Counter                   counter_;
    std::unique_ptr<Terminal> terminal_;
    terminal::Subject         subject_;
};

}  // namespace

UTest cases{"terminal", [] {

    "a terminal's bytes are the events every host delivers: keys with modifiers, text, the mouse, paste, focus"_test = [] {
        terminal::Input input({.column = 8, .row = 16});
        const auto events = input.feed("a\x1b[1;5C\x1b[<0;3;2M\x1b[<0;3;2m\x1b[200~two\r\nlines\x1b[201~\x1b[I");
        std::vector<std::string> seen;
        for (const InputEvent& event : events) {
            if (const auto* key = std::get_if<KeyEvent>(&event); key && key->kind == KeyKind::down)
                seen.push_back(std::format("key {}{}", key->mods.has(Modifier::ctrl) ? "Ctrl+" : "", name(key->key)));
            else if (const auto* text = std::get_if<TextEvent>(&event)) seen.push_back("text " + text->text);
            else if (const auto* pointer = std::get_if<PointerEvent>(&event); pointer && pointer->kind != PointerKind::move)
                seen.push_back(std::format("{} {},{}", pointer->kind == PointerKind::down ? "down" : "up", pointer->position.x, pointer->position.y));
            else if (const auto* focus = std::get_if<FocusEvent>(&event)) seen.push_back(focus->gained ? "focus" : "blur");
        }
        const std::vector<std::string> expected{"key A", "text a", "key Ctrl+Right", "down 20,24", "up 20,24", "text two\nlines", "focus"};
        expect(seen == expected) << std::format("{}", seen);
    };

    "a sequence cut between reads is kept; an Escape alone is the key once nothing follows it"_test = [] {
        terminal::Input input;
        expect(input.feed("\x1b[").empty() && input.pending());
        const auto arrow = input.feed("A");
        expect(arrow.size() == 2 && std::get<KeyEvent>(arrow[0]).key == Key::up);
        expect(input.feed("\x1b").empty());
        const auto escape = input.flush();
        expect(escape.size() == 2 && std::get<KeyEvent>(escape[0]).key == Key::escape);
        expect(input.feed("中").size() == 1) << "UTF-8 text, no key of its own";
        expect(input.feed("\xe4\xb8").empty() && input.feed("\xad").size() == 1) << "a character cut between reads";
        (void)input.feed("\x03");
        expect(input.interrupted()) << "Ctrl+C asks the program to end";
    };

    "the window's components, unchanged, in cells: the picture is a text baseline"_test = [] {
        Counter counter;
        Terminal t({.columns = 30, .rows = 6, .agent = false});
        t.mount("main", counter.view());
        t.settle();
        // The baseline: a padding of 16 pixels is a row and two columns, the button's border its brackets, the
        // primary button its fill (a colour, not text).
        expect(eq(t.frame().text(), std::string("\n  Count 0\n\n\n  [ -1 ]   +1\n")));
    };

    "the mouse clicks a button where its cells are, and the keyboard presses it, as a person would"_test = [] {
        Counter counter;
        Terminal t({.columns = 30, .rows = 6, .agent = false});
        t.mount("main", counter.view());
        t.settle();
        const auto plus = t.cell_of("main/controls/plus");
        expect(fatal(plus.has_value())) << "the button is shown";
        t.step(mouse_click(plus->first, plus->second));
        t.settle();
        expect(eq(counter.count.peek(), 1));
        expect(t.frame().text().contains("Count 1")) << t.frame().text();
        // Tab to the first button, Tab to the next, Enter: what a keyboard's person does.
        t.step("\t");
        t.step("\t");
        t.step("\r");
        t.settle();
        expect(eq(counter.count.peek(), 2)) << t.frame().text();
        expect(t.frame().text().contains("Count 2"));
    };

    "a terminal carries no pixel parts: a change shows at once, an effect is left out, a picture says why it is not there"_test = [] {
        // Nothing here installs them, as a terminal's program does not (terminal design §9 step ③): absent, not broken.
        expect(!morphs_installed() && mrender::installed_effects() == nullptr && mlibs::image::decoder() == nullptr);
        const Signal<int> count = signal(0);
        std::string failed;
        Element picture = image({.id = "logo", .source = "logo.png", .alt = "the logo", .width = Dim(16), .height = Dim(32)});
        picture.on("failed", [&](const Value& why) { failed = std::string(why.get("reason") ? why.get("reason")->as_string() : ""); });
        Terminal t({.columns = 30, .rows = 6, .agent = false});
        t.mount("main", column({}, {
            text({.id = "count", .text = derive([=] { return std::format("Count {}", count()); })})
                .prop("pixel_effect", Source(Value{}.set("name", "dissolve").set("progress", 0.5))),
            std::move(picture),
        }));
        t.settle();
        expect(t.frame().text().contains("Count 0")) << t.frame().text();
        expect(failed.contains("mlibs.image.decoder")) << failed;
        count.set(1);
        t.step();
        expect(t.frame().text().contains("Count 1")) << "the next frame, not a morph's: " << t.frame().text();
    };

    "an agent reads the picture and goes from a cell back to the node that drew it"_test = [] {
        Counter counter;
        Terminal t({.columns = 30, .rows = 6, .agent = false});
        t.mount("main", counter.view());
        t.settle();
        terminal::Subject subject(t);
        kernel::SubjectSession session({&subject});
        const auto frame = ask(session, R"({"op":"terminal.frame"})");
        expect(frame["size"].dump() == "[30,6]" && frame["text"].text().value_or("").contains("Count 0")) << frame["text"].text().value_or("");
        const auto cells = ask(session, R"({"op":"terminal.frame","cells":true})");
        expect(eq(cells["cells"].size(), std::size_t{6}) && eq(cells["cells"].items()[0].size(), std::size_t{30}));
        const auto plus = t.cell_of("main/controls/plus");
        expect(fatal(plus.has_value()));
        const auto at = ask(session, std::format(R"({{"op":"terminal.at","at":[{},{}]}})", plus->first, plus->second));
        expect(at["node"]["path"].text().value_or("").contains("plus")) << at.data->root().dump();
        expect(ask(session, R"({"op":"terminal.at","at":[99,0]})").error == "invalid-argument");
    };

    "the agent contract on the terminal's picture: it observes and traces; what it says it has not is unsupported"_test = [] {
        const contract::Probe probe{
            .make = []() -> std::unique_ptr<kernel::Subject> { return std::make_unique<Owned>(); },
            .read = R"({"op":"terminal.frame"})",
            .source = R"({"op":"terminal.at","at":[3,1]})",
        };
        contract::declare(probe, {.subject = "omui.host.terminal"});
        const std::vector<std::string> unsupported = contract::unsupported(probe.make()->capabilities());
        const auto lacks = [&](std::string_view id) { return std::ranges::find(unsupported, id) != unsupported.end(); };
        expect(!lacks("agent.source.chain") && !lacks("agent.read.pure"));
        expect(lacks("agent.time.replays")) << "a picture is a function of the interface: no time of its own";
    };

    "an agent operates it through the endpoint as it does a window: the interface's actions, and the picture's text"_test = [] {
        omui::agent::install();  // this program makes its own host: it is its root (K6)
        const auto dir = scratch_dir("terminal-endpoint");
        Counter counter;
        Terminal t({.columns = 30, .rows = 6, .app = "counter-terminal", .endpoint_directory = dir});
        t.mount("main", counter.view());
        t.settle();
        if (!t.endpoint()) {
            std::println("    skipped: local endpoints are not supported here");
            return;
        }
        std::atomic<bool> done{false};
        std::vector<Value> acted, read, captured;
        std::jthread agent_thread([&] {
            if (auto client = omui::agent::Client::connect("counter-terminal", dir)) {
                acted = client->call(parse_json(R"({"op":"act","node":"main/controls/plus","action":"activate","actor":"agent:test"})").value());
                read = client->call(parse_json(R"({"op":"terminal.frame"})").value());
                captured = client->call(parse_json(R"({"op":"capture"})").value());
            }
            done = true;
        });
        for (int i = 0; i < 3000 && !done; ++i) {
            t.step();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        agent_thread.join();
        expect(!acted.empty() && acted.back().get("exitCode")->as_int() == 0);
        expect(eq(counter.count.peek(), 1));
        const Value* picture = read.empty() ? nullptr : read.front().get("data");
        expect(picture && std::string(picture->get("text")->as_string()).contains("Count 1")) << (read.empty() ? std::string("no answer") : read.front().json());
        expect(!captured.empty() && captured.back().get("exitCode")->as_int() != 0) << "no pixels in a terminal: refused, with the way to the text";
    };
}};
