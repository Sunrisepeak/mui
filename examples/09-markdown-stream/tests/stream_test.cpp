// The stream as its reader and an agent see it: an answer arriving piece by
// piece is the same document as the whole answer at the end; the newest
// block's new bytes, and only those, fade in; the status line counts; the
// pixels at the end are pinned.
import std;
import omui;
import stream;
import mlibs.testing;
import mcpplibs.mtest;

using namespace mlibs::testing;
using namespace mcpplibs::mtest;
using namespace omui;

namespace {

std::optional<spec::SemanticNode> find(Headless& h, Role r) {
    for (const auto& n : h.tree().snapshot({}).nodes)
        if (n.role == r) return n;
    return std::nullopt;
}

struct App {
    stream::Model model;
    Headless      h{HeadlessOptions{.size = {860, 720}}};
    explicit App(bool fade = true) {
        model.fade = fade;
        model.runtime = &h.runtime();
        h.mount("main", stream::app(model).view());
        h.settle();
    }
};

std::string joined(const std::vector<std::string>& tokens) {
    std::string s;
    for (const auto& t : tokens) s += t;
    return s;
}

}  // namespace

UTest cases{"example.markdown-stream", [] {

    "the demo, played: partial on the way, the whole answer at the end"_test = [] {
        App a;
        const auto tokens = stream::demo_tokens();
        stream::play(a.model, tokens, 50);
        for (int i = 0; i < 60; ++i) a.h.step();  // a second: about 50 pieces
        expect(a.model.chunks > 10 && a.model.chunks < tokens.size()) << std::format("{} pieces so far", a.model.chunks);
        expect(!a.model.ended);
        expect(find(a.h, Role::heading).has_value()) << "the title is there already";
        for (int i = 0; i < 60 * 30 && !a.model.ended; ++i) a.h.step();
        a.h.settle();
        expect(a.model.ended && a.model.source.peek() == joined(tokens)) << "every piece, in order";
        expect(a.model.status.peek().starts_with("done"));
        expect(find(a.h, Role::table) && find(a.h, Role::code) && find(a.h, Role::list) && find(a.h, Role::blockquote)) << "all of it formed";
        expect(a.model.parsed / a.model.chunks < a.model.source.peek().size() / 4) << "each piece parses a small part of the answer";
        expect(a.h.tree().violations().empty());
    };

    "only the new bytes of the newest block fade in"_test = [] {
        App a;
        a.model.append("# Title\n\nHello");
        a.h.step();
        a.h.step();
        const NodeId p1 = a.h.runtime().find("main/view/doc/b1");
        expect(p1 != NodeId{}) << "the paragraph is a node";
        const Value m1 = a.h.runtime().get(p1, "glyph_motion");
        expect(m1.get("begin") && m1.get("begin")->as_int() == 0) << "a new block: all of it";
        a.model.append(" world");
        a.h.step();
        a.h.step();
        const NodeId p2 = a.h.runtime().find("main/view/doc/b1");
        const Value m2 = a.h.runtime().get(p2, "glyph_motion");
        expect(m2.get("begin") && m2.get("begin")->as_int() == 5) << "the same block grew: from where it ended (\"Hello\" is 5 bytes)";
        expect(a.h.tree().node("main/view/doc/b1")->states.has(State::animating));
        a.h.settle();
        expect(!a.h.tree().node("main/view/doc/b1")->states.has(State::animating)) << "and settles";
        App plain(false);
        plain.model.append("# Title\n\nHello");
        plain.h.step();
        plain.h.step();
        expect(plain.h.runtime().get(plain.h.runtime().find("main/view/doc/b1"), "glyph_motion").is_null()) << "--no-fade: no motion";
    };

    "tiny pieces, as stdin delivers them, cost a tail each"_test = [] {
        App a(false);
        const std::string answer = joined(stream::demo_tokens());
        for (std::size_t i = 0; i < answer.size(); i += 3) a.model.append(std::string_view(answer).substr(i, 3));
        a.model.end();
        a.h.settle();
        expect(a.model.source.peek() == answer);
        const double per_piece = static_cast<double>(a.model.parsed) / static_cast<double>(a.model.chunks);
        std::println("    {} pieces, {:.0f} bytes parsed per piece of a {} byte answer", a.model.chunks, per_piece, answer.size());
        expect(per_piece < static_cast<double>(answer.size()) / 3);
    };

    "the finished answer, the same pixels on every platform"_test = [] {
        App a(false);
        a.model.append(joined(stream::demo_tokens()));
        a.model.end();
        a.h.settle();
        Hash hash;
        const auto& px = a.h.pixels();
        for (int y = 0; y < px.height(); ++y) hash.bytes(std::as_bytes(std::span(px.row(y), static_cast<std::size_t>(px.width()))));
        expect(a.h.save_png(scratch_dir("markdown-stream") / "answer.png").has_value());
        std::println("    pixels {}", hash.hex());
        // Pinned: GCC and LLVM, Linux, macOS and Windows draw these bytes.
        expect(eq(hash.hex(), std::string("a99930d8b3882068")));
    };
}};
