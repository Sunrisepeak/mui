// The stream as its reader and an agent see it: an answer arriving piece by
// piece is the same document as the whole answer at the end; the newest
// block's new bytes, and only those, fade in; the status line counts; the
// pixels at the end are pinned.
import std;
import openmui;
import stream;
import openmui.testing;

using namespace omui::testing;
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

int main(int argc, char** argv) {
    Suite s{"example.markdown-stream"};

    s.test("the demo, played: partial on the way, the whole answer at the end", [] {
        App a;
        const auto tokens = stream::demo_tokens();
        stream::play(a.model, tokens, 50);
        for (int i = 0; i < 60; ++i) a.h.step();  // a second: about 50 pieces
        check(a.model.chunks > 10 && a.model.chunks < tokens.size(), std::format("{} pieces so far", a.model.chunks));
        check(!a.model.ended);
        check(find(a.h, Role::heading).has_value(), "the title is there already");
        for (int i = 0; i < 60 * 30 && !a.model.ended; ++i) a.h.step();
        a.h.settle();
        check(a.model.ended && a.model.source.peek() == joined(tokens), "every piece, in order");
        check(a.model.status.peek().starts_with("done"));
        check(find(a.h, Role::table) && find(a.h, Role::code) && find(a.h, Role::list) && find(a.h, Role::blockquote), "all of it formed");
        check(a.model.parsed / a.model.chunks < a.model.source.peek().size() / 4, "each piece parses a small part of the answer");
        check(a.h.tree().violations().empty());
    });

    s.test("only the new bytes of the newest block fade in", [] {
        App a;
        a.model.append("# Title\n\nHello");
        a.h.step();
        a.h.step();
        const NodeId p1 = a.h.runtime().find("main/view/doc/b1");
        check(p1 != NodeId{}, "the paragraph is a node");
        const Value m1 = a.h.runtime().get(p1, "glyph_motion");
        check(m1.get("begin") && m1.get("begin")->as_int() == 0, "a new block: all of it");
        a.model.append(" world");
        a.h.step();
        a.h.step();
        const NodeId p2 = a.h.runtime().find("main/view/doc/b1");
        const Value m2 = a.h.runtime().get(p2, "glyph_motion");
        check(m2.get("begin") && m2.get("begin")->as_int() == 5, "the same block grew: from where it ended (\"Hello\" is 5 bytes)");
        check(a.h.tree().node("main/view/doc/b1")->states.has(State::animating));
        a.h.settle();
        check(!a.h.tree().node("main/view/doc/b1")->states.has(State::animating), "and settles");
        App plain(false);
        plain.model.append("# Title\n\nHello");
        plain.h.step();
        plain.h.step();
        check(plain.h.runtime().get(plain.h.runtime().find("main/view/doc/b1"), "glyph_motion").is_null(), "--no-fade: no motion");
    });

    s.test("tiny pieces, as stdin delivers them, cost a tail each", [] {
        App a(false);
        const std::string answer = joined(stream::demo_tokens());
        for (std::size_t i = 0; i < answer.size(); i += 3) a.model.append(std::string_view(answer).substr(i, 3));
        a.model.end();
        a.h.settle();
        check(a.model.source.peek() == answer);
        const double per_piece = static_cast<double>(a.model.parsed) / static_cast<double>(a.model.chunks);
        std::println("    {} pieces, {:.0f} bytes parsed per piece of a {} byte answer", a.model.chunks, per_piece, answer.size());
        check(per_piece < static_cast<double>(answer.size()) / 3);
    });

    s.test("the finished answer, the same pixels on every platform", [] {
        App a(false);
        a.model.append(joined(stream::demo_tokens()));
        a.model.end();
        a.h.settle();
        Hash hash;
        const auto& px = a.h.pixels();
        for (int y = 0; y < px.height(); ++y) hash.bytes(std::as_bytes(std::span(px.row(y), static_cast<std::size_t>(px.width()))));
        check(a.h.save_png(scratch_dir("markdown-stream") / "answer.png").has_value());
        std::println("    pixels {}", hash.hex());
        // Pinned: GCC and LLVM, Linux, macOS and Windows draw these bytes.
        check_eq(hash.hex(), std::string("1dea011abd98dfe8"));
    });

    return s.run(argc, argv);
}
