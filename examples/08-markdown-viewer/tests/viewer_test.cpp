// The viewer as its reader and an agent use it: the contents follow and lead
// the reading, links to documents open here and Back returns, a web link
// leaves only when asked (a person's click is the answer), a saved file shows
// at once; and its pixels are pinned.
import std;
import openmui;
import viewer;
import openmui.testing;

using namespace omui::testing;
using namespace omui;

namespace {

std::filesystem::path docs() { return std::filesystem::path(std::source_location::current().file_name()).parent_path() / "data" / "docs"; }

std::optional<spec::SemanticNode> find(Headless& h, Role r, std::string_view name) {
    for (const auto& n : h.tree().snapshot({}).nodes)
        if (n.role == r && n.name == name) return n;
    return std::nullopt;
}

std::int64_t act(Headless& h, std::string_view id, std::string_view action, std::string answer = {}) {
    Invocation call;
    call.answer = std::move(answer);
    call.actor = {"agent", "test", {}};
    auto e = h.tree().act(id, action, call);
    h.settle();
    return e.exit_code().value_or(-1);
}

struct App {
    viewer::Model            model;
    std::vector<std::string> browsed;
    Headless                 h;
    explicit App(Size size = {980, 720}) : h(HeadlessOptions{.size = size}) {
        model.browse = [this](std::string_view a) { browsed.emplace_back(a); };
    }
    void start(const std::filesystem::path& where) {
        check(model.open(where), model.status.peek());
        AppSpec spec = viewer::app(model);
        spec.setup(h.runtime());
        h.mount("main", spec.view());
        h.settle();
    }
};

}  // namespace

int main(int argc, char** argv) {
    Suite s{"example.markdown-viewer"};

    s.test("a folder opens its index; its headings are the contents", [] {
        App a;
        a.start(docs());
        check(a.model.path.peek().ends_with("docs/index.md"));
        check_eq(a.model.toc.peek().size(), std::size_t{4});
        check(find(a.h, Role::button, "Installing").has_value() && find(a.h, Role::heading, "Handbook").has_value());
        check(a.h.tree().violations().empty());
    });

    s.test("the contents lead the reading: a heading scrolls into view", [] {
        App a({980, 260});
        a.start(docs());
        check(find(a.h, Role::heading, "Appendix")->states.has(State::offscreen));
        check(a.h.click(find(a.h, Role::button, "Appendix")->id));
        a.h.settle();
        check(!find(a.h, Role::heading, "Appendix")->states.has(State::offscreen), "in view after its entry");
        check(!a.model.current.peek().empty(), "the current heading is tracked");
    });

    s.test("a link to a document opens it here; Back returns; an anchor in it is shown", [] {
        App a({980, 300});
        a.start(docs());
        check(a.h.click(find(a.h, Role::link, "guide")->id));
        a.h.settle();
        check(a.model.path.peek().ends_with("guide/start.md") && find(a.h, Role::heading, "Getting started").has_value());
        check(!find(a.h, Role::button, "Back")->states.has(State::disabled), "Back can return");
        check(a.h.click("main/header/back"));
        a.h.settle();
        check(a.model.path.peek().ends_with("docs/index.md"));
        // A link with an anchor: the other document, scrolled to the heading.
        // (The link is below the fold: a reader scrolls to it first.)
        const std::string starting = find(a.h, Role::link, "Starting")->id;
        check_eq(act(a.h, starting, "scroll_into_view"), std::int64_t{0});
        check(a.h.click(starting));
        a.h.settle();
        const auto first = find(a.h, Role::heading, "First steps");
        check(first && !first->states.has(State::offscreen), "the anchor's heading is in view");
    });

    s.test("a web link: an agent states the answer, a person's click is it", [] {
        App a;
        a.start(docs());
        const std::string site = find(a.h, Role::link, "The project's site")->id;
        check_eq(act(a.h, site, "activate"), std::int64_t{spec::exit_code::needs_answer});
        check(a.browsed.empty(), "nothing opened without the answer");
        check_eq(act(a.h, site, "activate", "yes"), std::int64_t{0});
        check(a.h.click(site));
        a.h.settle();
        check((a.browsed == std::vector<std::string>{"https://example.com/handbook", "https://example.com/handbook"}));
    });

    s.test("a saved file shows at once", [] {
        const auto dir = scratch_dir("markdown-viewer");
        std::filesystem::copy(docs(), dir, std::filesystem::copy_options::recursive);
        App a;
        a.start(dir / "index.md");
        { std::ofstream(dir / "index.md", std::ios::trunc) << "# Handbook\n\n## A new chapter\n\nWritten just now.\n"; }
        for (int i = 0; i < 90; ++i) a.h.step();  // the stamp is read twice a second
        a.h.settle();
        check(find(a.h, Role::heading, "A new chapter").has_value(), "reloaded");
        check_eq(a.model.toc.peek().size(), std::size_t{2});
        check_eq(a.model.status.peek(), std::string("reloaded"));
    });

    s.test("the same pixels on every platform", [] {
        App a;
        a.start(docs());
        Hash hash;
        const auto& px = a.h.pixels();
        for (int y = 0; y < px.height(); ++y) hash.bytes(std::as_bytes(std::span(px.row(y), static_cast<std::size_t>(px.width()))));
        check(a.h.save_png(scratch_dir("markdown-viewer-pixels") / "handbook.png").has_value());
        std::println("    pixels {}", hash.hex());
        // Pinned: GCC and LLVM, Linux, macOS and Windows draw these bytes.
        check_eq(hash.hex(), std::string("490a13c700d32cde"));
    });

    return s.run(argc, argv);
}
