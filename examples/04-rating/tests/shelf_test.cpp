// The application's own component, from its contract to every language: the
// generated C++ builder and the behaviour (actions, keys, read-only); the same
// rating from TypeScript and from a view document is the same semantic node; the
// TypeScript reading list as a person and an agent use it; pinned pixels.
import std;
import omui;
import omui.player;
import omui.entry.player;
import omui.lang;
import omui.lang.js;
import omui.abi;
import omui.viewdoc;
import shelf;
import mlibs.testing;
import mcpplibs.mtest;

using namespace mlibs::testing;
using namespace mcpplibs::mtest;
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

UTest cases{"example.rating", [] {
    register_base_components();
    shelf::register_components();

    "the generated builder and the behaviour: half stars, actions, keys, read-only"_test = [] {
        Headless h(HeadlessOptions{.size = {300, 120}});
        auto stars = signal(2.0);
        std::vector<double> changed;
        h.mount("main", column({.padding = 10, .gap = 10}, {
            shelf::rating({.id = "book", .half = true, .label = "Middlemarch", .value = stars, .on_changed = [&](double v) { changed.push_back(v); }}),
            shelf::rating({.id = "average", .label = "Average", .readonly = true, .value = 3.6}),
        }));
        h.settle();
        expect(eq(meaning(h, "main/book"), std::string(R"j(slider|Middlemarch|{"value":2,"min":0,"max":5,"step":0.5}|2)j")));
        expect(eq(act(h, "main/book", "set_value", Value{}.set("value", 3.7)), std::int64_t{0}));
        expect(stars.peek() == 3.5) << std::format("{}: snapped to half stars, both ways", stars.peek());
        expect(eq(act(h, "main/book", "increment"), std::int64_t{0}));
        expect(eq(act(h, "main/book", "set_value", Value{}.set("value", "max")), std::int64_t{0}));
        expect(stars.peek() == 5.0);
        expect(eq(act(h, "main/book", "clear"), std::int64_t{0}));
        expect(stars.peek() == 0.0 && changed == std::vector<double>{3.5, 4.0, 5.0, 0.0}) << "every change, an event";
        expect(eq(act(h, "main/book", "set_value", Value{}.set("value", "lots")), std::int64_t{1}));  // the contract's types
        // The keys are the contract's: End is set_value(max).
        h.runtime().focus(h.runtime().find("main/book"));
        h.key(Key::end);
        h.key(Key::left);
        expect(stars.peek() == 4.5) << std::format("{}", stars.peek());
        expect(eq(act(h, "main/average", "increment"), std::int64_t{1}));  // read-only refuses
        expect(h.tree().node("main/average")->states.has(State::readonly));
        expect(shelf::Rating::contract().name == "rating" && shelf::Rating::contract().action("clear")) << "the contract, from the file";
    };

    "the same rating from TypeScript and from a view document is the same node as the C++ builder's"_test = [] {
        Headless h(HeadlessOptions{.size = {300, 200}});
        h.mount("cpp", shelf::rating({.id = "r", .half = true, .label = "Dune", .value = 3.5}));
        const auto doc = viewdoc::View::load(h.runtime(), R"j({"omui": "0.1", "view": {"root":
            {"type": "rating", "id": "r", "props": {"value": 3.5, "half": true, "label": "Dune"}}}})j");
        expect(doc.has_value());
        if (!doc) return;
        auto view = *doc;
        view.mount(h.runtime(), "doc");
        const abi::OmuiRuntime handle = abi::attach(h.runtime(), "test");
        {
            lang::Host host(handle);
            lang::Session session(host);
            lang::js::Script script(session);
            auto ran = script.run(R"ts(import { mount, rating } from "openmui"; mount("ts", rating({ id: "r", value: 3.5, half: true, label: "Dune" }));)ts");
            for (int i = 0; i < 2000 && ran.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready; ++i) {
                host.drain();
                h.step();
            }
            auto r = ran.get();
            expect(r.has_value()) << (r ? "" : r.error().text());
            for (int i = 0; i < 50; ++i) {
                host.drain();
                h.step();
            }
            h.settle();
            expect(eq(meaning(h, "ts/r"), meaning(h, "cpp/r")));
            expect(eq(meaning(h, "doc/r"), meaning(h, "cpp/r")));
        }
        abi::detach(handle);
    };

    "the reading list in TypeScript: rating, an agent's verb, the filter, adding a book, pixels"_test = [] {
        auto m = read_manifest(here() / "mcpp.toml");
        expect(m.has_value()) << (m ? "" : m.error());
        if (!m) return;
        Headless h(HeadlessOptions{.size = m->size});
        player::Player p(h.runtime(), *m, {lang::js::typescript()});
        auto started = p.start();
        expect(started.has_value()) << (started ? "" : started.error());
        settle(h, p);
        expect(p.problems().empty()) << (p.problems().empty() ? "" : p.problems()[0]);
        expect(eq(h.tree().node("main/summary/average")->name, std::string("4.5 average")));
        expect(eq(h.tree().node("main/summary/rated")->name, std::string("3 of 4 rated")));
        expect(value_of(h, "main/summary/overall") == 4.5);

        // A person rates the unrated book, as an agent would through the tree.
        expect(eq(act(h, "main/books/b4/stars", "set_value", Value{}.set("value", 3)), std::int64_t{0}));
        settle(h, p);
        expect(eq(h.tree().node("main/summary/average")->name, std::string("4.1 average")));
        expect(eq(h.tree().node("main/summary/rated")->name, std::string("4 of 4 rated")));
        // An agent's verb.
        Invocation call;
        call.actor = {"agent", "test", {}};
        call.args = Value{}.set("key", "b2").set("stars", 2.2);
        const Envelope e = h.runtime().verbs().invoke("books.rate", call);
        expect(eq(e.exit_code().value_or(-1), 0));
        settle(h, p);
        expect(value_of(h, "main/books/b2/stars") == 2.0) << std::format("{}", value_of(h, "main/books/b2/stars"));
        expect(eq(h.tree().node("main/summary/average")->name, std::string("3.5 average")));
        // Four stars and up.
        expect(eq(act(h, "main/favourites", "toggle"), std::int64_t{0}));
        settle(h, p);
        expect(!h.tree().node("main/books/b2/stars") && !h.tree().node("main/books/b4/stars") && h.tree().node("main/books/b1/stars")) << "only b1 and b3 are shown";
        expect(eq(act(h, "main/favourites", "toggle"), std::int64_t{0}));
        // Adding a book.
        expect(h.tree().node("main/add/add")->states.has(State::disabled)) << "nothing to add yet";
        expect(eq(act(h, "main/add/title", "set_value", Value{}.set("value", "Piranesi")), std::int64_t{0}));
        settle(h, p);
        expect(eq(act(h, "main/add/add", "activate"), std::int64_t{0}));
        settle(h, p);
        expect(h.tree().node("main/books/b5/stars").has_value()) << "the new book, unrated";
        expect(eq(h.tree().node("main/summary/rated")->name, std::string("4 of 5 rated")));

        h.runtime().focus(NodeId{});
        h.settle();
        Hash hash;
        const auto& px = h.pixels();
        for (int y = 0; y < px.height(); ++y) hash.bytes(std::as_bytes(std::span(px.row(y), static_cast<std::size_t>(px.width()))));
        std::println("example.rating pixels {}", hash.hex());
        expect(h.save_png(scratch_dir("example-rating") / "shelf.png").has_value());
        expect(eq(hash.hex(), std::string("04f5474390eb3b5c")));
    };
}};
