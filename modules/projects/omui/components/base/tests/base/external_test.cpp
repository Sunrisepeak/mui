// A web page in the interface (W11, D-141: an external surface): the system's
// own web view, placed by the host over the node — opened at the address,
// placed where the node is laid out and moved with it, its address, title and
// loading said in the tree as the host tells them, driven by an agent (an
// external effect: it answers) and closed with the node; and, where the host
// has no web view, a card that opens the system's browser.
import std;
import omui.ui.base;
import omui.host.headless;
import mlibs.testing;
import mcpplibs.mtest;

using namespace mlibs::testing;
using namespace mcpplibs::mtest;
using namespace omui;

namespace {

mlibs::text::FontCollection& fonts() {
    static mlibs::text::FontCollection collection = [] {
        mlibs::text::FontCollection made;
        (void)made.add_defaults();
        return made;
    }();
    return collection;
}

HeadlessOptions options(Size size = {520, 360}) {
    HeadlessOptions made;
    made.size = size;
    made.fonts = &fonts();
    return made;
}

std::int64_t act(Headless& h, std::string_view id, std::string_view action, Value args = {}, std::string answer = {}) {
    Invocation call;
    call.args = std::move(args);
    call.actor = {"agent", "test", {}};
    call.answer = std::move(answer);
    const auto answered = h.tree().act(id, action, call);
    h.step();
    return answered.exit_code().value_or(-1);
}

// A host's web view, as the test sees it: what it was asked, and a way to tell the page's changes.
struct FakeWebView {
    std::vector<std::string>                              opened;
    std::vector<std::pair<std::string, std::string>>      commands;
    std::vector<Rect>                                     placed;
    std::vector<std::uint64_t>                            closed;
    std::function<void(const WebPage&)>                   tell;

    Runtime::NativeSurfaces surfaces() {
        Runtime::NativeSurfaces made;
        made.web_open = [this](std::string_view url, std::function<void(const WebPage&)> changed) -> std::uint64_t {
            opened.emplace_back(url);
            tell = std::move(changed);
            return 7;
        };
        made.web_place = [this](std::uint64_t, Rect bounds, bool) { placed.push_back(bounds); };
        made.web_command = [this](std::uint64_t, std::string_view command, std::string_view argument) {
            commands.emplace_back(command, argument);
            return true;
        };
        made.web_close = [this](std::uint64_t page) { closed.push_back(page); };
        return made;
    }
};

bool near(const Rect& a, const Rect& b) { return std::abs(a.x - b.x) < 0.5f && std::abs(a.y - b.y) < 0.5f && std::abs(a.width - b.width) < 0.5f && std::abs(a.height - b.height) < 0.5f; }

}  // namespace

static UTest cases{"ui.base.external", [] {
    register_base_components();

    "the system's web view: opened at the address, placed where the node is and moved with it, the page told into the tree, driven by an agent, closed with the node"_test = [] {
        Headless h(options());
        FakeWebView host;
        h.runtime().set_native_surfaces(host.surfaces());
        auto url = signal(std::string("https://example.org/"));
        auto wide = signal(400.0f);
        h.mount("main", column({.padding = 20}, {web_view({.id = "page", .label = "Docs", .url = url, .width = wide, .height = 240})}));
        h.settle();
        expect(eq(host.opened, std::vector<std::string>{"https://example.org/"})) << "opened at its address";
        expect(fatal(!host.placed.empty()));
        expect(near(host.placed.back(), h.runtime().bounds(h.runtime().find("main/page")))) << "placed where it is laid out";
        // Laid out again: moved with it.
        wide.set(300.0f);
        h.settle();
        expect(near(host.placed.back(), h.runtime().bounds(h.runtime().find("main/page"))) && std::abs(host.placed.back().width - 300) < 0.5f) << "moved with it";
        // The page as the host tells it: loading, then its title; the address into the binding.
        host.tell({.url = "https://example.org/", .loading = true});
        h.settle();
        auto node = h.tree().node("main/page");
        expect(node && node->states.has(State::busy) && node->name == "Docs") << "loading, named by its label until the page has a title";
        host.tell({.url = "https://example.org/guide", .title = "Guide", .can_go_back = true});
        h.settle();
        node = h.tree().node("main/page");
        expect(node && !node->states.has(State::busy) && node->name == "Guide" && node->value == Value("https://example.org/guide")) << "its title, its address";
        expect(eq(url.peek(), std::string("https://example.org/guide"))) << "the binding follows the page";
        // An agent: going elsewhere reaches outside (it answers), back where there is a page before.
        expect(act(h, "main/page", "navigate", Value{}.set("url", "https://example.org/api")) != 0) << "not without its answer";
        expect(eq(act(h, "main/page", "navigate", Value{}.set("url", "https://example.org/api"), "yes"), std::int64_t{0}));
        expect(act(h, "main/page", "navigate", Value{}.set("url", "file:///etc/passwd"), "yes") != 0) << "only web addresses";
        expect(eq(act(h, "main/page", "back", {}, "yes"), std::int64_t{0}));
        expect(eq(host.commands, std::vector<std::pair<std::string, std::string>>{{"navigate", "https://example.org/api"}, {"back", ""}}));
        host.tell({.url = "https://example.org/", .title = "Example"});
        h.settle();
        expect(act(h, "main/page", "back", {}, "yes") != 0) << "no page before this one";
        // The application changes the address: the view goes there.
        url.set("https://example.org/news");
        h.settle();
        expect(host.commands.back() == std::pair<std::string, std::string>{"navigate", "https://example.org/news"});
        // Gone with its node.
        h.mount("main", column({}, {}));
        h.settle();
        expect(eq(host.closed, std::vector<std::uint64_t>{7})) << "closed with the node";
    };

    "no web view here: a card saying what the page is, which opens the system's browser — pressed by a person, or by an agent that answers"_test = [] {
        Headless h(options());
        h.runtime().set_url_opener([](std::string_view) { return true; });  // the system's browser, as a desktop host has it
        h.mount("main", column({.padding = 20}, {web_view({.id = "page", .label = "Docs", .url = std::string("https://example.org/"), .width = 360, .height = 160})}));
        h.settle();
        const auto node = h.tree().node("main/page");
        expect(node && node->name == "Docs" && node->description == "opens in the browser" && node->value == Value("https://example.org/"));
        expect(h.click("main/page"));
        h.settle();
        expect(eq(h.runtime().opened(), std::vector<std::string>{"https://example.org/"})) << "a person's press opens it";
        expect(eq(act(h, "main/page", "navigate", Value{}.set("url", "https://example.org/other"), "yes"), std::int64_t{0}));
        expect(eq(act(h, "main/page", "open_external", {}, "yes"), std::int64_t{0}));
        expect(h.runtime().opened().back() == "https://example.org/other") << "the address it was given";
        expect(act(h, "main/page", "back", {}, "yes") != 0) << "no history without a web view";
        expect(h.save_png(scratch_dir("ui-base-external") / "card.png").has_value());
    };
}};
