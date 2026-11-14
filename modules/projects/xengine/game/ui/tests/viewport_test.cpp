// A world in an interface: a viewport node of the UI tree shows a picture drawn elsewhere — here a yard drawn by
// the renderer's mesh reference — in a frame with no window and no device; the node says which subject it
// shows, and that subject is found beside the interface while the node shows it.
import std;
import omui.core;
import omui.view;
import omui.semantic;
import omui.host.headless;
import xengine.ui;
import xrender.mesh;
import xrender.raster;
import xrender.text;
import mlibs.agent;
import mlibs.control;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
using namespace omui;
namespace mesh = xrender::mesh;
namespace scene = xrender::scene;
namespace math = mlibs::math;
namespace geom = mlibs::geom;

namespace {

mlibs::text::FontCollection& fonts() {
    static mlibs::text::FontCollection made = [] {
        mlibs::text::FontCollection collection;
        (void)collection.add_defaults();
        return collection;
    }();
    return made;
}

// A world that only says its name: what the node names.
struct World final : mlibs::agent::Subject {
    [[nodiscard]] std::string_view name() const noexcept override { return "world"; }
    [[nodiscard]] std::string_view interface_name() const noexcept override { return "test.world@0.1"; }
    [[nodiscard]] mlibs::agent::Capabilities capabilities() const noexcept override { return {}; }
    [[nodiscard]] std::span<const mlibs::control::CatalogEntry> operations() const noexcept override { return {}; }
    void operate(std::string_view, const mlibs::agent::Request&, mlibs::agent::Answer&) override {}
};

// A ball on a floor under a sun, drawn by the reference; the ball's colour is the material's.
struct Drawn {
    std::vector<geom::ClusteredMesh> made;
    std::vector<const geom::ClusteredMesh*> meshes;
    std::vector<mesh::Material> materials{{{0.5f, 0.5f, 0.5f}, {}}, {{0.15f, 0.4f, 0.8f}, {}}};
    scene::Scene world;
    mesh::Pass pass;
    mesh::Targets targets;
    xrender::raster::Surface picture{320, 180};
    Drawn() {
        made.push_back(*geom::clustered(geom::sheet(200, 200, 4, 4)));
        made.push_back(*geom::clustered(geom::sphere(1, 32, 64), 32));
        for (const geom::ClusteredMesh& one : made) meshes.push_back(&one);
        expect(world.add({.id = 1, .mesh = 0, .material = 0, .world = math::translation({0, -1, 0}), .bounds = geom::bounds(made[0].mesh)}).has_value());
        expect(world.add({.id = 2, .mesh = 1, .material = 1, .world = math::translation({0, 0, 0}), .bounds = geom::bounds(made[1].mesh)}).has_value());
        expect(world.add(scene::Light{.id = 1, .kind = scene::LightKind::sun, .intensity = 0.9f, .toward = {-0.4f, -1, -0.5f}}).has_value());
        targets.resize(320, 180);
    }
    void draw() {
        const scene::View view{.view = math::look_at({0, 1.5f, 6}, {0, 0, 0}, {0, 1, 0}), .projection = math::perspective(1.0f, 16.0f / 9.0f, 0.5f, 100.0f)};
        scene::Extracted seen;
        scene::extract(world, view, seen);
        pass.draw(world, seen, view, {meshes, materials, {}}, targets);
        // The pass's picture is opaque: its bytes are the renderer's premultiplied pixels as they are.
        for (int y = 0; y < 180; ++y)
            for (int x = 0; x < 320; ++x) {
                const std::size_t at = (static_cast<std::size_t>(y) * 320 + x) * 4;
                picture.at(x, y) = {targets.colour[at], targets.colour[at + 1], targets.colour[at + 2], 255};
            }
    }
};

}  // namespace

static UTest viewport_cases{"xengine.ui.viewport", [] {
    "a world drawn by the mesh reference shows in a headless interface's frame, pixel for pixel in its node's box; the node names its subject; a picture drawn again shows again"_test = [] {
        HeadlessOptions options;
        options.level = HeadlessLevel::pixels;
        options.size = {400, 300};
        options.fonts = &fonts();
        Headless host(options);
        Element viewport;
        viewport.type = &component_type<xengine::ui::Viewport>();
        viewport.id = "world";
        host.mount("main", column({.padding = 20, .gap = 8}, {text({.id = "title", .text = "The yard"}), std::move(viewport)}));
        host.step();
        const NodeId node = host.runtime().find("main/world");
        auto* shown = host.runtime().component<xengine::ui::Viewport>(node);
        expect(fatal(shown != nullptr));
        const Rect box = host.runtime().bounds(node);
        expect(eq(box.width, 360.0f) && eq(box.height, 202.5f)) << std::format("{} × {}", box.width, box.height);
        const std::size_t beside = host.runtime().subjects().size();  // the host's own (its frame)

        Drawn yard;
        World world;
        yard.draw();
        shown->show({.label = "the yard", .subject = &world, .pixels = [&]() -> std::optional<xrender::raster::ImageView> { return yard.picture.view(); }, .width = 320, .height = 180});
        host.step();
        // Where the node is, the frame is the picture (scaled 320 → 360 across): the ball's blue at the box's middle.
        const auto frame_at = [&](float share_x, float share_y) { return host.pixels().at(static_cast<int>(box.x + box.width * share_x), static_cast<int>(box.y + box.height * share_y)); };
        const auto picture_at = [&](float share_x, float share_y) { return yard.picture.at(static_cast<int>(320 * share_x), static_cast<int>(180 * share_y)); };
        int worst = 0;
        for (const float share_y : {0.1f, 0.3f, 0.5f, 0.7f, 0.9f})
            for (const float share_x : {0.1f, 0.3f, 0.5f, 0.7f, 0.9f}) {
                const auto in_frame = frame_at(share_x, share_y);
                const auto in_picture = picture_at(share_x, share_y);
                worst = std::max({worst, std::abs(in_frame.r - in_picture.r), std::abs(in_frame.g - in_picture.g), std::abs(in_frame.b - in_picture.b)});
            }
        const auto middle = frame_at(0.5f, 0.5f);
        std::println("the yard in an interface: the node at ({}, {}) {} × {}; its middle ({}, {}, {}); at 25 places the frame is within {} levels of the picture", box.x, box.y, box.width, box.height,
                     middle.r, middle.g, middle.b, worst);
        expect(gt(int(middle.b), int(middle.r) + 40)) << "the ball's blue";
        expect(le(worst, 6)) << "smoothed from 320 to 360 across, nothing else done to it";
        expect(host.node_at_pixel(box.center()) == node) << "a pixel of the world is the viewport's";
        // The node says what it shows; the subject is beside the interface while it does.
        bool found = false;
        for (const auto& one : host.tree().snapshot().nodes)
            if (one.name == "the yard") found = true, expect(one.value.json() == "\"world\"") << one.value.json();
        expect(found) << "in the semantic tree by its label";
        expect(eq(host.runtime().subjects().size(), beside + 1) && std::ranges::contains(host.runtime().subjects(), static_cast<mlibs::agent::Subject*>(&world)));
        // What drew a pixel of the interface, asked on through the node to the world: its subject, the place in
        // its picture, and what the world says is there — here the instance the reference's id buffer names.
        shown->show({.label = "the yard", .subject = &world, .pixels = [&]() -> std::optional<xrender::raster::ImageView> { return yard.picture.view(); }, .width = 320, .height = 180,
                     .describe = [&](float across, float down) {
                         const int x = std::min(static_cast<int>(across * 320), 319), y = std::min(static_cast<int>(down * 180), 179);
                         const mesh::Pixel who = yard.targets.ids[static_cast<std::size_t>(y) * 320 + x];
                         Value said;
                         if (who.draw != mesh::none) said.set("instance", static_cast<std::int64_t>(yard.targets.draws[who.draw].id)).set("triangle", static_cast<std::int64_t>(who.triangle));
                         return said;
                     }});
        host.step();
        const std::string on_ball = host.runtime().shown_at(node, box.center()).json(), on_sky = host.runtime().shown_at(node, {box.x + 5, box.y + 5}).json();
        std::println("what the viewport shows at its middle: {}; at its corner: {}", on_ball, on_sky);
        expect(on_ball.contains("\"subject\":\"world\"") && on_ball.contains("\"at\":[0.5,0.5]") && on_ball.contains("\"instance\":2")) << on_ball;
        expect(on_sky.contains("\"subject\":\"world\"") && !on_sky.contains("instance")) << on_sky;
        expect(host.runtime().shown_at(host.runtime().find("main/title"), {30, 30}).json() == "null") << "a node that shows nothing from elsewhere says nothing";
        // A place in the node is a place in the world's picture.
        const auto place = shown->within({box.width / 2, box.height / 4});
        expect(place.has_value() && eq(place->x, 0.5f) && eq(place->y, 0.25f));
        expect(!shown->within({-1, 5}).has_value());

        // The ball turns red; drawn again and told so, the frame shows it. (The host draws a frame when its
        // pixels are asked for: the one kept is drawn first.)
        (void)host.pixels();
        yard.materials[1].base = {0.8f, 0.2f, 0.15f};
        yard.draw();
        host.step();
        expect(gt(int(frame_at(0.5f, 0.5f).b), int(frame_at(0.5f, 0.5f).r))) << "not told: the frame kept is still the old one";
        shown->changed();
        host.step();
        expect(gt(int(frame_at(0.5f, 0.5f).r), int(frame_at(0.5f, 0.5f).b) + 40)) << "told: the new picture";
        const auto kept = host.save_png(std::filesystem::temp_directory_path() / "openmui-tests" / "xengine-ui-viewport.png");
        expect(kept.has_value());
        shown->clear();
        host.step();
        expect(eq(host.runtime().subjects().size(), beside)) << "and gone with it";
    };
    "the pointer kept to the world: a press on a viewport that is looked about holds it; the hand's movement gathers there and nothing else sees the pointer; Escape lets it go"_test = [] {
        HeadlessOptions options;
        options.level = HeadlessLevel::layout;
        options.size = {400, 300};
        options.fonts = &fonts();
        Headless host(options);
        Element viewport;
        viewport.type = &component_type<xengine::ui::Viewport>();
        viewport.id = "world";
        int pressed = 0;
        host.mount("main", column({.padding = 20, .gap = 8}, {box({.id = "menu", .width = 80, .height = 28, .on = {{"press", [&](const Value&) { ++pressed; }}}}), std::move(viewport)}));
        host.step();
        const NodeId node = host.runtime().find("main/world");
        auto* shown = host.runtime().component<xengine::ui::Viewport>(node);
        expect(fatal(shown != nullptr));
        shown->show({.label = "the yard", .look = true});
        host.step();
        const Rect box = host.runtime().bounds(node), menu = host.runtime().bounds(host.runtime().find("main/menu"));
        expect(!shown->holds_pointer() && !host.pointer_held());
        host.click(box.center());
        expect(shown->holds_pointer() && host.pointer_held()) << "a press on it: the host holds the pointer";
        // The hand moves: no place in the interface changes, the movement gathers.
        host.move_by({10, -4});
        host.move_by({2.5f, 1});
        const Point looked = shown->take_look();
        expect(eq(looked.x, 12.5f) && eq(looked.y, -3.0f)) << std::format("{}, {}", looked.x, looked.y);
        expect(eq(shown->take_look().x, 0.0f)) << "taken once";
        // A click while it is kept is the world's, wherever the pointer was left: the menu is not pressed.
        host.click(menu.center());
        expect(eq(pressed, 0));
        // Escape is the person's way out, and then the interface has its pointer again.
        host.key(Key::escape);
        expect(!shown->holds_pointer() && !host.pointer_held());
        host.click(menu.center());
        expect(eq(pressed, 1));
        // Held again, then the window loses focus: let go.
        host.click(box.center());
        expect(shown->holds_pointer());
        host.step({FocusEvent{.gained = false}});
        expect(!shown->holds_pointer() && !host.pointer_held());
        // A viewport that is not looked about does not take the pointer.
        shown->show({.label = "the yard"});
        host.click(box.center());
        expect(!shown->holds_pointer());
    };
}};
