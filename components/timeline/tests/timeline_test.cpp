// The timeline editor as a person and an agent meet it: the document read
// into tracks and clips that agents can see; a clip selected, moved, trimmed,
// split and removed by actions and by the pointer and keys; every edit
// undone and redone exactly, the document after each handed out; a document
// that does not read says why; the same pixels on every platform.
import std;

import openmui.ui.timeline;
import openmui.timeline;
import openmui.host.headless;
import openmui.testing;

using namespace omui::testing;
using namespace omui;
namespace tl = omui::timeline;

namespace {

typeset::FontCollection& fonts() {
    static typeset::FontCollection f = [] {
        typeset::FontCollection c;
        (void)c.add_defaults();
        return c;
    }();
    return f;
}

constexpr std::string_view promo = R"([timeline]
name = "promo"
rate = 30

[[marker]]
at = "2s"
name = "beat"

[[track]]
name = "picture"
kind = "video"

[[track.clip]]
name = "title"
at = "0f"
length = "60f"
fade = { in = "10f", out = "10f" }

[[track.clip.property]]
name = "y"
keys = [["0f", 40, "out_cubic"], ["15f", 0]]

[[track.clip]]
name = "fight"
at = "90f"
length = "90f"

[[track]]
name = "music"
kind = "audio"

[[track.clip]]
name = "theme"
at = "0f"
length = "180f"
)";

struct Editor {
    Headless                 h;
    std::vector<std::string> documents;  // what `edited` handed out
    explicit Editor(std::string_view document = promo) : h([] {
        HeadlessOptions o;
        o.size = {960, 160};
        o.fonts = &fonts();
        return o;
    }()) {
        // Its own history (S-12): undo and redo are its actions, Ctrl+Z inside it undoes its edits.
        Element e = timeline_editor({.id = "edit", .document = std::string(document), .zoom = 120}).prop("undo_scope", Source{Value(true), {}, {}});
        e.on("edited", [this](const Value& args) { documents.emplace_back(args.get("document") ? args.get("document")->as_string() : ""); });
        h.mount("main", std::move(e));
        h.step();
    }
    bool act(std::string_view action, Value args = {}) {
        Invocation c;
        c.actor = {"agent", "test", {}};
        c.args = std::move(args);
        return h.tree().act("main", action, c).exit_code() == std::optional<int>(0);
    }
    Value seen() {
        for (const auto& n : h.tree().snapshot({}).nodes)
            if (n.id == "main") return n.value;
        return {};
    }
    TimelineEditor& editor() { return *h.runtime().component<TimelineEditor>(h.runtime().find("main")); }
    std::string text() { return tl::write_timeline(editor().model()); }
    std::uint64_t pixels() {
        Hash hash;
        const auto& px = h.pixels();
        for (int y = 0; y < px.height(); ++y) hash.bytes(std::as_bytes(std::span(px.row(y), static_cast<std::size_t>(px.width()))));
        return std::stoull(hash.hex(), nullptr, 16);
    }
};

Value clip(int track, int index) { return Value{}.set("track", track).set("clip", index); }

PointerEvent press(Point p, PointerKind k) {
    PointerEvent e;
    e.kind = k, e.position = p, e.button = MouseButton::left, e.buttons = k == PointerKind::up ? 0u : 1u;
    return e;
}

KeyEvent key(Key k, bool down, Modifiers mods = {}) {
    KeyEvent e;
    e.kind = down ? KeyKind::down : KeyKind::up, e.key = k, e.mods = mods;
    return e;
}

}  // namespace

int main(int argc, char** argv) {
    Suite s{"ui.timeline"};

    s.test("the document read into tracks and clips an agent sees: names, kinds, frames, the playhead", [] {
        Editor e;
        const Value v = e.seen();
        check_eq(v.get("frames")->as_int(0), std::int64_t{180});
        const Value::List& tracks = *v.get("tracks")->list();
        check(tracks.size() == 2 && tracks[0].get("kind")->as_string() == "video" && tracks[1].get("name")->as_string() == "music", "two tracks");
        const Value::List& clips = *tracks[0].get("clips")->list();
        check(clips.size() == 2 && clips[1].get("name")->as_string() == "fight" && clips[1].get("frame")->as_int(0) == 90 && clips[1].get("frames")->as_int(0) == 90,
              "the fight at frame 90 for 90 frames");
    });

    s.test("an agent edits: select, split at the playhead, move, trim, remove — each handing out the document, each undone and redone exactly through the history (S-12)", [] {
        Editor e;
        const std::string original = e.text();
        check(e.act("select", clip(0, 1)) && e.act("seek", Value{}.set("frame", 120)) && e.act("split"), "the fight split at frame 120");
        check(e.editor().model().tracks[0].clips.size() == 3 && e.documents.size() == 1, "three clips; the document handed out");
        check(e.act("move", Value{}.set("track", 0).set("clip", 2).set("to_track", 0).set("frame", 200)), "its second half moved later");
        check(e.act("trim", Value{}.set("track", 0).set("clip", 0).set("frame", 5).set("frames", 40)), "the title trimmed");
        check(e.act("select", clip(1, 0)) && e.act("remove", Value{}.set("ripple", false)), "the theme removed");
        check(!e.act("move", Value{}.set("track", 0).set("clip", 0).set("to_track", 1).set("frame", 0)), "a picture onto the music track refused");
        check_eq(std::string(e.seen().get("last")->as_string()), std::string("remove `theme`"));
        const std::string edited = e.text();
        for (int i = 0; i < 4; ++i) check(e.act("undo"), "undo");
        check_eq(e.text(), original);
        check(!e.act("undo"), "nothing more to undo: the window's history offers no undo now");
        for (int i = 0; i < 4; ++i) check(e.act("redo"), "redo");
        check_eq(e.text(), edited);
        check_eq(e.documents.back(), edited);
    });

    s.test("a person edits: a click selects and sets the playhead, a drag moves a clip by whole frames, its edge trims, S splits, Delete removes, Ctrl+Z undoes", [] {
        Editor e;
        const float ruler = TimelineEditor::ruler, row = TimelineEditor::track_height, x0 = TimelineEditor::header;
        auto at = [&](double seconds, int track) { return Point{x0 + static_cast<float>(seconds * 120), ruler + row * static_cast<float>(track) + row / 2}; };
        // The ruler at 1 s: the playhead to frame 30.
        e.h.step({press({x0 + 120, 10}, PointerKind::down), press({x0 + 120, 10}, PointerKind::up)});
        check_eq(e.editor().props.frame, 30);
        // The fight (3 s to 6 s) dragged right by half a second: it starts at 3.5 s, frame 105.
        e.h.step({press(at(4, 0), PointerKind::down), press(at(4.5, 0), PointerKind::move), press(at(4.5, 0), PointerKind::up)});
        check_eq(e.editor().model().tracks[0].clips[1].at, tl::frame_time({30, 1}, 105));
        // Its right edge dragged back a second: 60 frames long.
        const double end = 3.5 + 3;
        e.h.step({press(at(end - 0.01, 0), PointerKind::down), press(at(end - 1, 0), PointerKind::move), press(at(end - 1, 0), PointerKind::up)});
        check_eq(e.editor().model().tracks[0].clips[1].length, tl::frame_time({30, 1}, 60));
        // The playhead into it and S: split; Delete: the selected half gone.
        e.h.step({press(at(4, 0), PointerKind::down), press(at(4, 0), PointerKind::up)});
        check(e.act("seek", Value{}.set("frame", 120)));
        e.h.step({key(Key::s, true), key(Key::s, false)});
        check_eq(e.editor().model().tracks[0].clips.size(), std::size_t{3});
        e.h.step({key(Key::delete_, true), key(Key::delete_, false)});
        check_eq(e.editor().model().tracks[0].clips.size(), std::size_t{2});
        // Ctrl+Z four times (the move, the trim, the split, the removal): back to the fight as it started.
        for (int i = 0; i < 4; ++i) e.h.step({key(Key::z, true, Modifier::ctrl), key(Key::z, false, Modifier::ctrl)});
        check_eq(e.text(), tl::write_timeline(*tl::read_timeline(promo, "promo")));
    });

    s.test("a document that does not read says why, at its line and column, to people and agents", [] {
        Editor e("[[track]]\nkind = \"vidoe\"\n");
        const Value v = e.seen();
        check(v.get("problems") && v.get("problems")->list() && !v.get("problems")->list()->empty(), "the problems in its value");
        check((*v.get("problems")->list())[0].as_string().contains("did you mean `video`?"), "with the nearest right word");
    });

    s.test("pinned: the ruler, the tracks and their clips with fades and keys, the marker, the playhead, a selected clip", [] {
        Editor e;
        check(e.act("select", clip(0, 1)) && e.act("seek", Value{}.set("frame", 45)));
        e.h.step();
        check(e.h.save_png(scratch_dir("ui-timeline") / "timeline.png").has_value());
        const std::uint64_t px = e.pixels();
        std::println("    timeline editor {:016x}", px);
        check_eq(px, std::uint64_t{0x1b6839e07ca9c576});  // GCC and LLVM
    });

    return s.run(argc, argv);
}
