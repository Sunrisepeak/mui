// The infinite canvas (canvas-markdown §1): the scene's grid answers what a
// brute-force scan answers; the camera keeps its precision far from the
// origin, zooms about a point, and its springs and flings settle where they
// say; the component draws what the camera sees and only that, switches a
// node's tier with its size on screen, makes the largest live and moves them
// with the camera, states what is on screen to the semantic tree, finds
// anything in the scene, and answers the wheel, a drag and a fling.
import std;

import openmui.ui.canvas;
import openmui.host.headless;
import openmui.render.vector;
import openmui.raster;
import openmui.testing;

using namespace omui::testing;
using namespace omui;

namespace {

// A deterministic generator (the scene's tests are the same everywhere).
struct Rng {
    std::uint64_t s = 0x9e3779b97f4a7c15ull;
    double next() {
        s ^= s << 13, s ^= s >> 7, s ^= s << 17;
        return static_cast<double>(s >> 11) * (1.0 / 9007199254740992.0);
    }
};

std::vector<std::string> brute(const world::Scene& s, const world::WorldRect& r) {
    std::vector<std::string> out;
    s.each([&](world::NodeRef n) {
        if (s.rect(n).overlaps(r)) out.emplace_back(s.key(n));
    });
    return out;
}

std::vector<std::string> keys(const world::Scene& s, const std::vector<world::NodeRef>& nodes) {
    std::vector<std::string> out;
    for (auto n : nodes) out.emplace_back(s.key(n));
    return out;
}

std::int64_t act(Headless& h, std::string_view id, std::string_view action, Value args = {}, Value* data = nullptr) {
    Invocation call;
    call.args = std::move(args);
    call.actor = {"agent", "test", {}};
    auto e = h.tree().act(id, action, call);
    if (data && e.data_value()) *data = *e.data_value();
    h.step();
    return e.exit_code().value_or(-1);
}

std::size_t count_rects(const render::DisplayList& l) {
    std::size_t n = 0;
    for (const auto& c : l.commands()) n += std::holds_alternative<render::FillRect>(c) || std::holds_alternative<render::FillRoundRect>(c);
    return n;
}

}  // namespace

int main(int argc, char** argv) {
    Suite s{"ui.canvas"};
    register_canvas_component();

    s.test("the scene's grid finds what a scan finds, bottom to top, as nodes come, move and go", [] {
        world::Scene sc(256);
        Rng rng;
        std::vector<world::NodeRef> nodes;
        for (int i = 0; i < 20000; ++i)
            nodes.push_back(sc.add({.kind = "box", .x = rng.next() * 100000 - 50000, .y = rng.next() * 100000 - 50000,
                                    .width = static_cast<float>(10 + rng.next() * 400), .height = static_cast<float>(10 + rng.next() * 300)}));
        // A few huge ones (the large list).
        for (int i = 0; i < 5; ++i) nodes.push_back(sc.add({.kind = "box", .x = -60000, .y = -60000.0 + i * 20000.0, .width = 120000, .height = 500}));
        for (int i = 0; i < 2000; ++i) sc.move(nodes[static_cast<std::size_t>(i * 7)], rng.next() * 1000, rng.next() * 1000);
        for (int i = 0; i < 1000; ++i) sc.remove(nodes[static_cast<std::size_t>(i * 13 + 1)]);
        check_eq(sc.size(), std::size_t{20005 - 1000});
        std::vector<world::NodeRef> got;
        for (const world::WorldRect r : {world::WorldRect{-1000, -1000, 3000, 3000}, world::WorldRect{20000, -5000, 800, 600},
                                         world::WorldRect{-70000, -70000, 140000, 140000}, world::WorldRect{1e6, 1e6, 10, 10}}) {
            sc.query(r, got);
            check(keys(sc, got) == brute(sc, r), std::format("the query at {} {} {}×{}", r.x, r.y, r.width, r.height));
        }
        // Keys name nodes; a key used again replaces its node.
        const auto a = sc.add({.kind = "box", .x = 0, .y = 0, .width = 10, .height = 10, .key = "a"});
        check(sc.find("a") == std::optional(a));
        const auto again = sc.add({.kind = "box", .x = 5, .y = 5, .width = 10, .height = 10, .key = "a"});
        check(!sc.valid(a) && sc.find("a") == std::optional(again));
        sc.set(again, "title", std::string("Notes"));
        sc.set(again, "progress", 0.5);
        check(sc.text(again, "title") == "Notes" && sc.number(again, "progress") == 0.5 && sc.number(again, "absent", 7) == 7);
        // Hits: the topmost; raising changes it.
        const auto under = sc.add({.kind = "box", .x = 2000000, .y = 0, .width = 100, .height = 100, .key = "under"});
        const auto over = sc.add({.kind = "box", .x = 2000050, .y = 50, .width = 100, .height = 100, .key = "over"});
        check(sc.hit(2000075, 75)->node == over);
        sc.raise(under);
        check(sc.hit(2000075, 75)->node == under);
        // A script's columns, in one call.
        std::vector<double> xs{0, 100, 200}, ys{0, 0, 0};
        std::vector<float> ws{50, 50, 50}, hs{40, 40, 40};
        const std::vector<std::string> kinds{"box"}, names{"p0", "p1", "p2"};
        const auto pushed = sc.push(xs, ys, ws, hs, kinds, names);
        check(pushed.size() == 3 && sc.key(pushed[2]) == "p2" && sc.rect(pushed[1]).x == 100);
    });

    s.test("the camera: precise far from the origin, zooming about a point, springs and flings that settle", [] {
        const Size vp{800, 600};
        world::Camera c{1e9, -2e9, 3};
        // A world point a billion units out lands on the right screen pixel.
        const Point p = c.to_screen(1e9 + 10.25, -2e9 - 4.5, vp);
        check(std::abs(p.x - (400 + 30.75f)) < 1e-3f && std::abs(p.y - (300 - 13.5f)) < 1e-3f, std::format("{} {}", p.x, p.y));
        // Zooming keeps the world point under the cursor where it is.
        const Point cursor{123, 456};
        const auto before = c.to_world(cursor, vp);
        c.zoom_at(cursor, 2.5, vp, 0.01, 100);
        const auto after = c.to_world(cursor, vp);
        check(std::abs(before[0] - after[0]) < 1e-6 && std::abs(before[1] - after[1]) < 1e-6 && c.zoom == 7.5);
        c.zoom_at(cursor, 1000, vp, 0.01, 100);
        check_eq(c.zoom, 100.0);
        // Fit: the rectangle whole, centred.
        const auto f = world::Camera::fit({0, 0, 1600, 600}, vp, 0, 0.01, 100);
        check(f.zoom == 0.5 && f.x == 800 && f.y == 300);
        // A spring arrives exactly, and is between before.
        const world::CameraMotion m({0, 0, 1}, {1000, 500, 4}, hanim::seconds(1));
        check(m.at(hanim::seconds(1)) == world::Camera{0, 0, 1});
        const auto mid = m.at(hanim::seconds(1.1));
        check(mid.x > 0 && mid.x < 1000 && mid.zoom > 1 && mid.zoom < 4);
        check(m.settled(hanim::seconds(10)) && m.at(hanim::seconds(10)) == world::Camera{1000, 500, 4});
        // A fling glides the way it was thrown and stops.
        const world::Inertia fling({0, 0, 2}, {-600, 0}, hanim::seconds(0));
        const auto later = fling.at(hanim::seconds(0.2)), end = fling.at(hanim::seconds(5));
        check(later.x > 0 && end.x > later.x && fling.settled(hanim::seconds(5)));
        check(std::abs(end.x - 600.0 / 6.0 / 2.0) < 2, std::format("a fling of 600 px/s at zoom 2 glides ~50 world units, not {}", end.x));
    });

    s.test("the component draws what the camera sees, by tier, and makes the largest live", [] {
        auto& sc = world::scene("tiers");
        sc.clear();
        int custom_draws = 0;
        sc.kind("card", world::Kind{.tiers = {{0, world::Tier::Draw::box}, {60, world::Tier::Draw::custom}, {400, world::Tier::Draw::promote}},
                                    .draw = [&](world::DrawContext& d) {
                                        ++custom_draws;
                                        d.list.fill_round_rect(d.screen, CornerRadii::all(6), render::Paint::solid(Color::hex(0x2563eb)));
                                    },
                                    .promote = [](const world::Scene& s, world::NodeRef n) -> Element {
                                        return column({.padding = 8}, {text({.id = "title", .text = std::string(s.text(n, "title"))})});
                                    },
                                    .role = Role::article});
        for (int i = 0; i < 100; ++i) {
            const auto n = sc.add({.kind = "card", .x = (i % 10) * 300.0, .y = (i / 10) * 200.0, .width = 240, .height = 160,
                                   .key = std::format("c{}", i), .fill = Color::hex(0x94a3b8), .radius = 8});
            sc.set(n, "title", std::format("Card {}", i));
        }
        Headless h({.size = {800, 600}});
        h.mount("main", column({.width = 800, .height = 600}, {canvas({.id = "board", .scene = "tiers", .x = 1350, .y = 1000, .zoom = 0.2, .width = 800, .height = 600})}));
        h.settle();
        // Far out: every card a box (48 px on screen), nothing custom, nothing live.
        check_eq(custom_draws, 0);
        check(count_rects(h.runtime().display_list()) >= 100, "a hundred boxes, and the background");
        // Closer: the cards on screen are drawn by the kind, and only they.
        check_eq(act(h, "main/board", "zoom_to", Value(Value::Map{{"zoom", Value(1.0)}})), std::int64_t{0});
        h.settle();
        const int drawn = custom_draws;
        check(drawn > 0, "custom drawing happened");
        custom_draws = 0;
        h.step();  // nothing changed: nothing drawn again
        check_eq(custom_draws, 0);
        // Close: the centred card is live — a node of the tree — and follows the camera.
        check_eq(act(h, "main/board", "focus_node", Value(Value::Map{{"key", Value("c55")}})), std::int64_t{0});
        h.settle();
        const auto live = h.tree().node("main/board/c55/column:0/title");
        check(live && live->name == "Card 55", "the live card's text is in the tree");
        const auto at = h.tree().node("main/board/c55");
        check(at.has_value() && at->bounds.has_value(), "the live card has bounds");
        if (at && at->bounds) {
            // It sits where its world rectangle is on screen.
            const auto b = *at->bounds;
            check(std::abs(b.x + b.width / 2 - 400) < 2 && std::abs(b.y + b.height / 2 - 300) < 2, std::format("centred: {} {} {}×{}", b.x, b.y, b.width, b.height));
        }
        // Panning moves it with the camera in the same frame.
        h.scroll({400, 300}, {-50, 0});
        const auto moved = h.tree().node("main/board/c55");
        check(moved && moved->bounds && at && at->bounds && std::abs((moved->bounds->x - at->bounds->x) - 50) < 1, "moved 50 px with the pan");
    });

    s.test("live nodes of a scene made or filled after the canvas mounted: placed, and composed again when touched", [] {
        // Kinds always live: a node is a card from its first pixel.
        const auto card = [](world::Scene& sc) {
            sc.kind("live", world::Kind{.tiers = {{0, world::Tier::Draw::promote}},
                                        .promote = [](const world::Scene& s, world::NodeRef n) -> Element {
                                            return text({.id = "title", .text = std::string(s.text(n, "title"))});
                                        }});
        };
        const auto add = [](world::Scene& sc, std::string_view key, double x, std::string title) {
            const auto n = sc.add({.kind = "live", .x = x, .y = 0, .width = 200, .height = 100, .key = key});
            sc.set(n, "title", std::move(title));
            return n;
        };
        Headless h({.size = {800, 600}});
        // Named before anything made it: the canvas watches it from mount.
        world::drop_scene("late");
        h.mount("main", column({.width = 800, .height = 600},
                               {canvas({.id = "made", .scene = "late", .x = 100, .y = 50, .width = 800, .height = 300}),
                                canvas({.id = "filled", .scene = "filled", .x = 100, .y = 50, .width = 800, .height = 300})}));
        auto& filled = world::scene("filled");
        filled.clear();
        card(filled);
        h.settle();
        check(!h.tree().node("main/made/a"), "nothing live yet");
        auto& late = world::scene("late");
        card(late);
        add(late, "a", 0, "Made later");
        h.settle();
        const auto made = h.tree().node("main/made/a/title");
        check(made && made->name == "Made later", "the scene made after mount: its node live");
        // Filled from a timer: in a frame before its paint, which must not
        // swallow the change the next frame's step places.
        h.runtime().after(hanim::seconds(0), [&] { add(filled, "b", 0, "From a timer"); });
        h.settle();
        const auto timed = h.tree().node("main/filled/b/title");
        check(timed && timed->name == "From a timer", "the node a timer added: live");
        // A node gone goes; a touched one is composed again from its columns.
        filled.remove(*filled.find("b"));
        add(filled, "c", 0, "Third");
        h.settle();
        check(!h.tree().node("main/filled/b") && h.tree().node("main/filled/c/title"), "the removed node gone, the added one live");
        const auto c = *filled.find("c");
        filled.set(c, "title", std::string("Renamed"));
        filled.touch(c);
        h.settle();
        const auto renamed = h.tree().node("main/filled/c/title");
        check(renamed && renamed->name == "Renamed", "the touched node composed again");
    });

    s.test("texture tiers: rasterised in the background, the nearest ready level stretched meanwhile, stale content kept until the new one is in", [] {
        auto& sc = world::scene("textures");
        sc.clear();
        std::atomic<int> built = 0;
        sc.kind("doc", world::Kind{.tiers = {{0, world::Tier::Draw::box}, {60, world::Tier::Draw::texture}},
                                   .content = [&](const world::Scene& s, world::NodeRef n, render::DisplayList& dl) {
                                       ++built;
                                       const auto r = s.rect(n);
                                       dl.fill_rect({0, 0, static_cast<float>(r.width), static_cast<float>(r.height)},
                                                    Color::hex(static_cast<std::uint32_t>(s.number(n, "color", 0xdc2626))));
                                       dl.fill_rect({0, 0, static_cast<float>(r.width), 20}, Color::hex(0x1d4ed8));
                                   }});
        for (int i = 0; i < 12; ++i)
            sc.add({.kind = "doc", .x = (i % 4) * 300.0, .y = (i / 4) * 250.0, .width = 240, .height = 200, .key = std::format("d{}", i),
                    .fill = Color::hex(0x94a3b8)});
        Headless h({.size = {800, 600}});
        h.mount("main", column({.width = 800, .height = 600},
                               {canvas({.id = "board", .scene = "textures", .x = 570, .y = 350, .zoom = 0.6, .width = 800, .height = 600})}));
        h.step();
        // The first frame: boxes (nothing is rasterised yet).
        auto at = [&](world::NodeRef n) {
            const auto r = sc.rect(n);
            const Point p = world::Camera{570, 350, 0.6}.to_screen(r.x + r.width / 2, r.y + r.height / 2, {800, 600});
            return h.pixels().at(static_cast<int>(p.x), static_cast<int>(p.y));
        };
        const auto d5 = *sc.find("d5");
        const raster::Rgba8 grey = raster::premultiply(Color::hex(0x94a3b8)), red = raster::premultiply(Color::hex(0xdc2626));
        check(at(d5) == grey, "a box until its texture is in");
        h.settle();
        check(built.load() >= 12, std::format("every visible node's content was built ({})", built.load()));
        check(at(d5) == red, "the texture, once in");
        // The content changes: the stale texture stays until the new one is in.
        sc.set(d5, "color", 0x16a34a);
        sc.touch(d5);
        h.step();
        check(at(d5) == red, "the old texture meanwhile, not a box");
        h.settle();
        check(at(d5) == raster::premultiply(Color::hex(0x16a34a)), "the new content");
        // Closer: a sharper level is asked for; the one in is stretched meanwhile, never a box.
        h.runtime().set(h.runtime().find("main/board"), "zoom", Value(2.5));
        h.runtime().set(h.runtime().find("main/board"), "x", Value(sc.rect(d5).x + 120));
        h.runtime().set(h.runtime().find("main/board"), "y", Value(sc.rect(d5).y + 100));
        h.step();
        const auto centre = h.pixels().at(400, 300);
        check(centre == raster::premultiply(Color::hex(0x16a34a)), "the coarser texture, stretched, at once");
        h.settle();
        check(h.pixels().at(400, 300) == raster::premultiply(Color::hex(0x16a34a)), "and the sharper one when it is in");
        h.runtime().set(h.runtime().find("main/board"), "zoom", Value(0.6));
        h.runtime().set(h.runtime().find("main/board"), "x", Value(570.0));
        h.runtime().set(h.runtime().find("main/board"), "y", Value(350.0));
        h.settle();
        // A tight memory budget: textures go, nodes fall back to boxes, and the frame still draws.
        h.runtime().set(h.runtime().find("main/board"), "cache_mb", Value(0.3));
        h.settle();
        int boxes = 0, textured = 0;
        for (int i = 0; i < 12; ++i) {
            const auto px = at(*sc.find(std::format("d{}", i)));
            boxes += px == grey;
            textured += !(px == grey);
        }
        check(textured >= 1, "what fits stays");
    });

    s.test("texture tiers from a view: laid out offscreen on the pool, the same pixels as the live node, drawn again in a new theme", [] {
        auto& sc = world::scene("views");
        sc.clear();
        // One element for the texture and the live node: a card of the theme's surface, a coloured band, a title.
        auto card = [](const world::Scene& s, world::NodeRef n) {
            return column({.width = Dim(static_cast<float>(s.rect(n).width)), .height = Dim(static_cast<float>(s.rect(n).height)), .background = Fill("surface")},
                          {box({.height = 40, .background = Fill(Color::hex(static_cast<std::uint32_t>(s.number(n, "band", 0x7c3aed))))}),
                           text({.text = std::string(s.text(n, "title")), .style = "heading"})});
        };
        sc.kind("card", world::Kind{.tiers = {{0, world::Tier::Draw::box}, {60, world::Tier::Draw::texture}, {700, world::Tier::Draw::promote}},
                                    .promote = card,
                                    .view = card});
        for (int i = 0; i < 6; ++i) {
            const auto n = sc.add({.kind = "card", .x = (i % 3) * 320.0, .y = (i / 3) * 260.0, .width = 280, .height = 220, .key = std::format("c{}", i),
                                   .fill = Color::hex(0x94a3b8)});
            sc.set(n, "title", std::format("Card {}", i));
        }
        sc.set(*sc.find("c4"), "band", 0x0ea5e9);
        const world::Camera cam{460, 240, 1.0};
        Headless h({.size = {800, 600}});
        h.mount("main", column({.width = 800, .height = 600},
                               {canvas({.id = "board", .scene = "views", .x = cam.x, .y = cam.y, .zoom = cam.zoom, .width = 800, .height = 600})}));
        h.step();
        const auto c4 = *sc.find("c4");
        auto at = [&](double wx, double wy) {
            const Point p = cam.to_screen(wx, wy, {800, 600});
            return h.pixels().at(static_cast<int>(p.x), static_cast<int>(p.y));
        };
        const auto band = [&] { return at(sc.rect(c4).x + 140, sc.rect(c4).y + 20); };
        check(band() == raster::premultiply(Color::hex(0x94a3b8)), "a box before its texture is in");
        h.settle();
        check(band() == raster::premultiply(Color::hex(0x0ea5e9)), "the view's band, from the texture");
        // The texture is the view's own pixels: the same region drawn live (promoted, at 1:1) is the same bytes.
        const Rect region = [&] {
            const Rect r = cam.to_screen(sc.rect(c4), {800, 600});
            return Rect{std::round(r.x), std::round(r.y), 280, 220};
        }();
        const auto textured = h.png(region);
        h.runtime().set(h.runtime().find("main/board"), "zoom", Value(1.0));
        sc.kind("card", world::Kind{.tiers = {{0, world::Tier::Draw::box}, {60, world::Tier::Draw::promote}}, .promote = card, .view = card});
        h.settle();
        const auto live = h.png(region);
        check(textured && live && *textured == *live, "the texture and the live node, byte for byte");
        // A new theme: drawn again in it.
        sc.kind("card", world::Kind{.tiers = {{0, world::Tier::Draw::box}, {60, world::Tier::Draw::texture}}, .promote = card, .view = card});
        h.settle();
        const auto light = at(sc.rect(c4).x + 140, sc.rect(c4).y + 150);
        h.runtime().set_theme(Theme::dark_theme());
        h.settle();
        const auto dark = at(sc.rect(c4).x + 140, sc.rect(c4).y + 150);
        check(light == raster::premultiply(Theme::light().color("surface")) && dark == raster::premultiply(Theme::dark_theme().color("surface")),
              "the card's surface follows the theme");
    });

    s.test("semantics: what is on screen, the viewport's actions, and find over the whole scene", [] {
        auto& sc = world::scene("search");
        sc.clear();
        sc.kind("note", world::Kind{.role = Role::article});
        for (int i = 0; i < 1000; ++i) {
            const auto n = sc.add({.kind = i % 2 ? "note" : "box", .x = (i % 40) * 150.0, .y = (i / 40) * 150.0, .width = 100, .height = 100,
                                   .key = std::format("n{}", i)});
            sc.set(n, "title", i % 100 == 7 ? std::format("Groceries {}", i) : std::format("Note {}", i));
        }
        sc.preset("overview", {3000, 1875, 0.12});
        Headless h({.size = {800, 600}});
        h.mount("main", column({.width = 800, .height = 600}, {canvas({.id = "board", .label = "Notes", .scene = "search", .x = 400, .y = 300, .zoom = 1, .width = 800, .height = 600})}));
        h.settle();
        const auto board = h.tree().node("main/board");
        check(board && board->role == Role::canvas && board->name == "Notes");
        // Only what is on screen is stated (an 800×600 view of a 6000×3750 scene).
        std::size_t items = 0;
        for (const auto& n : h.tree().snapshot({}).nodes) items += n.id.starts_with("main/board/n");
        check(items > 10 && items < 60, std::format("{} items on screen", items));
        const auto n41 = h.tree().node("main/board/n41");
        check(n41 && n41->role == Role::article && n41->name == "Note 41" && n41->bounds.has_value());
        // find: over the whole scene.
        Value found;
        check_eq(act(h, "main/board", "find", Value(Value::Map{{"query", Value(Value::Map{{"name", Value("groceries")}})}}), &found), std::int64_t{0});
        check(found.list() && found.list()->size() == 10, "ten groceries in the whole scene");
        check_eq(act(h, "main/board", "find", Value(Value::Map{{"query", Value(Value::Map{{"near", Value(Value::List{Value(4000.0), Value(3000.0)})},
                                                                                            {"radius", Value(200.0)},
                                                                                            {"kind", Value("note")}})}}),
                     &found),
                 std::int64_t{0});
        check(found.list() && !found.list()->empty(), "notes near a point");
        // The viewport's actions.
        check_eq(act(h, "main/board", "camera_preset", Value(Value::Map{{"name", Value("overview")}})), std::int64_t{0});
        h.settle();
        items = 0;
        for (const auto& n : h.tree().snapshot({}).nodes) items += n.id.starts_with("main/board/n");
        check_eq(items, std::size_t{1000});
        check(act(h, "main/board", "camera_preset", Value(Value::Map{{"name", Value("nowhere")}})) != 0, "an unknown view is refused");
        check_eq(act(h, "main/board", "fit", Value(Value::Map{{"target", Value("n999")}})), std::int64_t{0});
        h.settle();
        check(h.tree().node("main/board/n999").has_value());
    });

    s.test("frame demand (D-70): the fastest while the camera moves on its own, none once it rests", [] {
        auto& sc = world::scene("pacing");
        sc.clear();
        sc.add({.kind = "box", .x = 0, .y = 0, .width = 100, .height = 100, .key = "a", .fill = Color::hex(0x16a34a)});
        sc.add({.kind = "box", .x = 3000, .y = 2000, .width = 100, .height = 100, .key = "b", .fill = Color::hex(0x2563eb)});
        Headless h({.size = {800, 600}});
        h.mount("main", column({.width = 800, .height = 600}, {canvas({.id = "board", .scene = "pacing", .width = 800, .height = 600})}));
        h.settle();
        check(h.runtime().frame_demand().idle(), "at rest");
        Invocation call;
        call.actor = {"agent", "test", {}};
        call.args = Value{}.set("key", Value("b"));
        (void)h.tree().act("main/board", "focus_node", call);
        h.step();
        const auto& d = h.runtime().frame_demand();
        check(!d.idle() && std::isinf(d.max_hz) && d.reason == "camera spring", d.reason);
        h.settle();
        check(h.runtime().frame_demand().idle(), "settled: none");
    });

    s.test("input: the wheel zooms about the cursor, a drag pans, a fling glides", [] {
        auto& sc = world::scene("input");
        sc.clear();
        sc.add({.kind = "box", .x = 0, .y = 0, .width = 100, .height = 100, .key = "a", .fill = Color::hex(0x16a34a)});
        auto x = signal(0.0), y = signal(0.0), zoom = signal(1.0);
        Headless h({.size = {800, 600}});
        h.mount("main", column({.width = 800, .height = 600}, {canvas({.id = "board", .scene = "input", .x = x, .y = y, .zoom = zoom, .width = 800, .height = 600})}));
        h.settle();
        // A wheel notch about (600, 300): that point stays put; the binding sees the camera.
        h.scroll({600, 300}, {0, -120}, false);
        h.step();
        check(zoom.get() > 1, "zoomed in");
        const double wx = x.get() + (600 - 400) / zoom.get();
        check(std::abs(wx - 200) < 1e-6, std::format("the point under the cursor stays: {}", wx));
        // A drag pans by what the pointer moved.
        const double x0 = x.get(), z = zoom.get();
        h.step({PointerEvent{.kind = PointerKind::down, .position = {400, 300}, .button = MouseButton::left, .buttons = 1}});
        h.step({PointerEvent{.kind = PointerKind::move, .position = {300, 300}, .buttons = 1}});
        check(std::abs((x.get() - x0) - 100 / z) < 1e-6, "dragged 100 px left: the camera moved right");
        // Released moving: it glides on, slowing, and stops.
        h.step({PointerEvent{.kind = PointerKind::move, .position = {280, 300}, .buttons = 1}});
        h.step({PointerEvent{.kind = PointerKind::up, .position = {280, 300}, .button = MouseButton::left}});
        const double released = x.get();
        h.advance(hanim::seconds(0.2));
        const double gliding = x.get();
        h.settle();
        check(gliding > released && x.get() > gliding, std::format("glides: {} → {} → {}", released, gliding, x.get()));
    });

    return s.run(argc, argv);
}
