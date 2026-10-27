// The application's own component, from its contract to every language: the
// generated C++ builder and the behaviour (actions, keys, read-only); the same
// rating from Lua and from a view document is the same semantic node; the
// TypeScript reading list as a person and an agent use it; pinned pixels.
import std;
import openmui;
import openmui.player;
import openmui.entry.player;
import openmui.lang;
import openmui.lang.js;
import openmui.lang.lua;
import openmui.abi;
import openmui.viewdoc;
import shelf;
import openmui.testing;

using namespace omui::testing;
using namespace omui;

namespace {

std::filesystem::path here() { return std::filesystem::path(std::source_location::current().file_name()).parent_path().parent_path(); }

std::int64_t act(Headless& h, std::string_view node, std::string_view action, Value args = {}) {
    Invocation call;
    call.actor = {"agent", "test", {}};
    call.args = std::move(args);
    return h.tree().act(node, action, call).exit_code().value_or(-1);
}

double value_of(Headless& h, std::string_view node) {
    auto n = h.tree().node(node);
    const Value* v = n ? n->value.get("value") : nullptr;
    return v ? v->as_double() : -1;
}

// A node's meaning: role, name, value, states — not its place.
std::string meaning(Headless& h, std::string_view node) {
    auto n = h.tree().node(node);
    return n ? std::format("{}|{}|{}|{}", spec::name(n->role), n->name, n->value.json(), n->states.bits) : "<none>";
}

void settle(Headless& h, player::Player& p) {
    for (int i = 0; i < 4000 && !p.idle(); ++i) {
        h.step();
        std::this_thread::sleep_for(std::chrono::microseconds(250));
    }
    h.settle();
}

}  // namespace

int main(int argc, char** argv) {
    Suite s{"example.rating"};
    register_base_components();
    shelf::register_components();

    s.test("the generated builder and the behaviour: half stars, actions, keys, read-only", [] {
        Headless h(HeadlessOptions{.size = {300, 120}});
        auto stars = signal(2.0);
        std::vector<double> changed;
        h.mount("main", column({.padding = 10, .gap = 10}, {
            shelf::rating({.id = "book", .half = true, .label = "Middlemarch", .value = stars, .on_changed = [&](double v) { changed.push_back(v); }}),
            shelf::rating({.id = "average", .label = "Average", .readonly = true, .value = 3.6}),
        }));
        h.settle();
        check_eq(meaning(h, "main/book"), std::string(R"j(slider|Middlemarch|{"value":2,"min":0,"max":5,"step":0.5}|2)j"));
        check_eq(act(h, "main/book", "set_value", Value{}.set("value", 3.7)), std::int64_t{0});
        check(stars.peek() == 3.5, std::format("{}: snapped to half stars, both ways", stars.peek()));
        check_eq(act(h, "main/book", "increment"), std::int64_t{0});
        check_eq(act(h, "main/book", "set_value", Value{}.set("value", "max")), std::int64_t{0});
        check(stars.peek() == 5.0);
        check_eq(act(h, "main/book", "clear"), std::int64_t{0});
        check(stars.peek() == 0.0 && changed == std::vector<double>{3.5, 4.0, 5.0, 0.0}, "every change, an event");
        check_eq(act(h, "main/book", "set_value", Value{}.set("value", "lots")), std::int64_t{1});  // the contract's types
        // The keys are the contract's: End is set_value(max).
        h.runtime().focus(h.runtime().find("main/book"));
        h.key(Key::end);
        h.key(Key::left);
        check(stars.peek() == 4.5, std::format("{}", stars.peek()));
        check_eq(act(h, "main/average", "increment"), std::int64_t{1});  // read-only refuses
        check(h.tree().node("main/average")->states.has(State::readonly));
        check(shelf::Rating::contract().name == "rating" && shelf::Rating::contract().action("clear"), "the contract, from the file");
    });

    s.test("the same rating from Lua and from a view document is the same node as the C++ builder's", [] {
        Headless h(HeadlessOptions{.size = {300, 200}});
        h.mount("cpp", shelf::rating({.id = "r", .half = true, .label = "Dune", .value = 3.5}));
        const auto doc = viewdoc::View::load(h.runtime(), R"j({"omui": "0.1", "view": {"root":
            {"type": "rating", "id": "r", "props": {"value": 3.5, "half": true, "label": "Dune"}}}})j");
        check(doc.has_value());
        if (!doc) return;
        auto view = *doc;
        view.mount(h.runtime(), "doc");
        const abi::OmuiRuntime handle = abi::attach(h.runtime(), "test");
        {
            lang::Host host(handle);
            lang::Session session(host);
            lang::lua::Script lua(session);
            auto ran = lua.run(R"lua(assert(omui.mount("lua", omui.Rating { id = "r", value = 3.5, half = true, label = "Dune" })))lua");
            for (int i = 0; i < 2000 && ran.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready; ++i) {
                host.drain();
                h.step();
            }
            auto r = ran.get();
            check(r.has_value(), r ? "" : r.error().text());
            for (int i = 0; i < 50; ++i) {
                host.drain();
                h.step();
            }
            h.settle();
            check_eq(meaning(h, "lua/r"), meaning(h, "cpp/r"));
            check_eq(meaning(h, "doc/r"), meaning(h, "cpp/r"));
        }
        abi::detach(handle);
    });

    s.test("the reading list in TypeScript: rating, an agent's verb, the filter, adding a book, pixels", [] {
        auto m = read_manifest(here() / "mcpp.toml");
        check(m.has_value(), m ? "" : m.error());
        if (!m) return;
        Headless h(HeadlessOptions{.size = m->size});
        player::Player p(h.runtime(), *m, {lang::js::typescript()});
        auto started = p.start();
        check(started.has_value(), started ? "" : started.error());
        settle(h, p);
        check(p.problems().empty(), p.problems().empty() ? "" : p.problems()[0]);
        check_eq(h.tree().node("main/summary/average")->name, std::string("4.5 average"));
        check_eq(h.tree().node("main/summary/rated")->name, std::string("3 of 4 rated"));
        check(value_of(h, "main/summary/overall") == 4.5);

        // A person rates the unrated book, as an agent would through the tree.
        check_eq(act(h, "main/books/b4/stars", "set_value", Value{}.set("value", 3)), std::int64_t{0});
        settle(h, p);
        check_eq(h.tree().node("main/summary/average")->name, std::string("4.1 average"));
        check_eq(h.tree().node("main/summary/rated")->name, std::string("4 of 4 rated"));
        // An agent's verb.
        Invocation call;
        call.actor = {"agent", "test", {}};
        call.args = Value{}.set("key", "b2").set("stars", 2.2);
        const Envelope e = h.runtime().verbs().invoke("books.rate", call);
        check_eq(e.exit_code().value_or(-1), 0);
        settle(h, p);
        check(value_of(h, "main/books/b2/stars") == 2.0, std::format("{}", value_of(h, "main/books/b2/stars")));
        check_eq(h.tree().node("main/summary/average")->name, std::string("3.5 average"));
        // Four stars and up.
        check_eq(act(h, "main/favourites", "toggle"), std::int64_t{0});
        settle(h, p);
        check(!h.tree().node("main/books/b2/stars") && !h.tree().node("main/books/b4/stars") && h.tree().node("main/books/b1/stars"),
              "only b1 and b3 are shown");
        check_eq(act(h, "main/favourites", "toggle"), std::int64_t{0});
        // Adding a book.
        check(h.tree().node("main/add/add")->states.has(State::disabled), "nothing to add yet");
        check_eq(act(h, "main/add/title", "set_value", Value{}.set("value", "Piranesi")), std::int64_t{0});
        settle(h, p);
        check_eq(act(h, "main/add/add", "activate"), std::int64_t{0});
        settle(h, p);
        check(h.tree().node("main/books/b5/stars").has_value(), "the new book, unrated");
        check_eq(h.tree().node("main/summary/rated")->name, std::string("4 of 5 rated"));

        h.runtime().focus(NodeId{});
        h.settle();
        Hash hash;
        const auto& px = h.pixels();
        for (int y = 0; y < px.height(); ++y) hash.bytes(std::as_bytes(std::span(px.row(y), static_cast<std::size_t>(px.width()))));
        std::println("example.rating pixels {}", hash.hex());
        check(h.save_png(scratch_dir("example-rating") / "shelf.png").has_value());
        check_eq(hash.hex(), std::string("739540b564420dff"));
    });

    return s.run(argc, argv);
}
