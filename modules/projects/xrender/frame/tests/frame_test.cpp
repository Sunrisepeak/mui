// The frame's entry: layers composed are the reference renderer drawing each
// in turn, byte for byte; a pixel is traced to the layer and the object that
// drew it; and the frame as a subject of the agent kernel — the agent
// contract's suite on it, its layers, its pixels as a PNG, what drew a pixel.
import std;
import mlibs.agent.cts;
import mlibs.base64;
import mlibs.geom;
import mlibs.image;
import mlibs.json;
import xrender.frame;
import xrender.mesh;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace agent = mlibs::agent;
namespace contract = mlibs::agent::cts;
namespace geom = mlibs::geom;
namespace raster = xrender::raster;

namespace {

constexpr int W = 160, H = 96;

// A world behind: a sky and a sun (object 3).
void world(xrender::DisplayList& list) {
    list.fill_rect({0, 0, W, H}, geom::Color::hex(0x0f172a));
    list.begin_node(3);
    list.fill_round_rect({100, 12, 40, 40}, geom::CornerRadii::all(20), xrender::Paint::solid(geom::Color::hex(0xf59e0b)));
    list.end_node();
}
// An interface in front: a panel (object 7) with a button in it (object 9); the rest of it draws nothing.
void front_layer(xrender::DisplayList& list) {
    list.begin_node(7);
    list.fill_round_rect({8, 56, 96, 32}, geom::CornerRadii::all(6), xrender::Paint::solid(geom::Color::hex(0xf8fafc)));
    list.begin_node(9);
    list.fill_round_rect({16, 62, 40, 20}, geom::CornerRadii::all(4), xrender::Paint::solid(geom::Color::hex(0x4f46e5)));
    list.end_node();
    list.end_node();
}

// A frame drawn and shown: what a program holds.
struct Picture final : xrender::FrameSubject {
    xrender::DisplayList behind, front;
    raster::Surface      surface{W, H};
    xrender::CpuFrame    frame;

    explicit Picture(bool world_ids = true) {
        world(behind);
        front_layer(front);
        const std::array layers{xrender::FrameLayer{.name = "world", .list = &behind, .object_ids = world_ids},
                                xrender::FrameLayer{.name = "ui", .list = &front, .object_ids = true}};
        frame.render(layers, surface, {.background = geom::Color::black()});
        show(frame, surface);
    }
};

struct Answered {
    std::optional<mlibs::json::Document> data;
    std::string                          error;
    [[nodiscard]] mlibs::json::Node operator[](std::string_view key) const { return data->root()["data"][key]; }
};
Answered ask(agent::SubjectSession& session, std::string_view line) {
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

}  // namespace

UTest cases{"frame", [] {
    "layers composed are the reference renderer drawing each in turn, byte for byte"_test = [] {
        Picture picture;
        xrender::CpuRenderer renderer;
        raster::Surface      expected(W, H);
        (void)renderer.render(picture.behind, expected, {}, {.background = geom::Color::black()});
        (void)renderer.render(picture.front, expected, {}, {.clear = false});
        expect(std::ranges::equal(picture.surface.pixels(), expected.pixels())) << "the same bytes";
        const auto& report = picture.frame.report();
        expect(eq(report.layers.size(), std::size_t{2}) && report.layers[0].name == "world" && report.layers[1].name == "ui");
        expect(eq(report.width, W) && report.layers[1].report.commands > 0u);
    };

    "a pixel is traced to the topmost layer that keeps what drew it, and that layer's object"_test = [] {
        Picture picture;
        const auto button = picture.frame.at(30, 70), panel = picture.frame.at(90, 70), sun = picture.frame.at(120, 30), sky = picture.frame.at(4, 4);
        expect(eq(button.layer, 1) && eq(button.object, 9u)) << "the button, in the interface";
        expect(eq(panel.layer, 1) && eq(panel.object, 7u));
        expect(eq(sun.layer, 0) && eq(sun.object, 3u)) << "where the interface drew nothing, the world's object";
        expect(eq(sky.layer, -1)) << "the sky belongs to no object";
        expect(eq(picture.frame.at(-1, 0).layer, -1) && eq(picture.frame.at(W, H).layer, -1));

        Picture unmarked(false);
        expect(eq(unmarked.frame.at(120, 30).layer, -1)) << "a layer that keeps no ids says nothing of its pixels";
        expect(unmarked.frame.object_ids(0).empty() && eq(unmarked.frame.object_ids(1).size(), static_cast<std::size_t>(W * H)));
    };

    "the agent contract on a frame: it observes and traces; what it says it has not is unsupported"_test = [] {
        const contract::Probe probe{
            .make = []() -> std::unique_ptr<agent::Subject> { return std::make_unique<Picture>(); },
            .read = R"({"op":"frame.layers"})",
            .source = R"({"op":"frame.at","at":[30,70]})",
        };
        contract::declare(probe, {.subject = "xrender.frame"});
        const std::vector<std::string> unsupported = contract::unsupported(probe.make()->capabilities());
        const auto lacks = [&](std::string_view id) { return std::ranges::find(unsupported, id) != unsupported.end(); };
        expect(!lacks("agent.source.chain") && !lacks("agent.read.pure") && !lacks("agent.authority.first")) << "run, and judged above";
        expect(lacks("agent.time.replays") && lacks("agent.preview.untouched"))
            << "a frame is a function of what it was given: it has no time of its own";
    };

    "an agent reads the frame: its layers, its pixels as drawn, what drew one"_test = [] {
        Picture picture;
        agent::SubjectSession session({&picture});
        const auto layers = ask(session, R"({"op":"frame.layers"})");
        expect(layers["size"].dump() == std::format("[{},{}]", W, H));
        expect(layers["layers"][0]["name"].text() == "world" && layers["layers"][1]["name"].text() == "ui" && layers["layers"][1]["ids"].boolean() == true);

        const auto drew = ask(session, R"({"op":"frame.at","at":[30,70]})");
        expect(drew["chain"][0]["layer"].text() == "ui" && drew["chain"][0]["object"].integer() == 9);
        expect(drew["pixel"].dump() == "[79,70,229,255]") << drew["pixel"].dump();
        expect(eq(ask(session, R"({"op":"frame.at","at":[4,4]})")["chain"].size(), std::size_t{0})) << "nothing to trace it to";
        expect(ask(session, R"({"op":"frame.at","at":[999,4]})").error == "invalid-argument");

        const auto captured = ask(session, R"({"op":"frame.capture","region":[8,56,96,32]})");
        const auto bytes = mlibs::base64::decode(captured["png"].text().value_or(""));
        expect(fatal(bytes.has_value()));
        const auto image = mlibs::image::decode_png(*bytes);
        expect(fatal(image.has_value())) << (image ? "" : image.error());
        expect(eq(image->width, 96) && eq(image->height, 32));
        // The panel's region as drawn (opaque here, so straight and premultiplied are the same bytes).
        bool same = true;
        for (int y = 0; y < 32 && same; ++y)
            for (int x = 0; x < 96 && same; ++x) {
                const raster::Rgba8 drawn = picture.surface.at(8 + x, 56 + y);
                const auto row = image->row(y);
                same = row[x * 4] == drawn.r && row[x * 4 + 1] == drawn.g && row[x * 4 + 2] == drawn.b && row[x * 4 + 3] == drawn.a;
            }
        expect(same) << "the PNG's pixels are the frame's";
        expect(ask(session, R"({"op":"frame.capture","region":[100,56,96,32]})").error == "invalid-argument") << "a region outside the frame";

        xrender::FrameSubject nothing;
        agent::SubjectSession empty({&nothing});
        expect(ask(empty, R"({"op":"frame.capture"})").error == "stale") << "no frame drawn yet";
        expect(eq(ask(empty, R"({"op":"frame.layers"})")["layers"].size(), std::size_t{0}));
    };
    "a world in three dimensions is a layer of the frame: under the interface, and a pixel of it traced to the instance, the cluster and the triangle"_test = [] {
        namespace mesh = xrender::mesh;
        namespace scene = xrender::scene;
        namespace math = mlibs::math;
        // A ball (instance 11) over a floor (instance 1), drawn by the mesh pass at the frame's size.
        const geom::ClusteredMesh ball = *geom::clustered(geom::sphere(1, 16, 32), 32), ground = *geom::clustered(geom::sheet(40, 40));
        const std::array<const geom::ClusteredMesh*, 2> meshes{&ball, &ground};
        const std::array<mesh::Material, 2> materials{{{{0.2f, 0.4f, 0.8f}, {}}, {{0.5f, 0.5f, 0.5f}, {}}}};
        scene::Scene yard;
        expect(yard.add({.id = 11, .mesh = 0, .material = 0, .world = math::translation({1.5f, 0.6f, 0}), .bounds = geom::bounds(ball.mesh)}).has_value());
        expect(yard.add({.id = 1, .mesh = 1, .material = 1, .world = math::translation({0, -1, 0}), .bounds = geom::bounds(ground.mesh)}).has_value());
        expect(yard.add(scene::Light{.id = 1, .toward = {-0.3f, -1, -0.4f}}).has_value());
        const scene::View view{.view = math::look_at({0, 1.5f, 6}, {0, 0, 0}, {0, 1, 0}), .projection = math::perspective(1.0f, static_cast<float>(W) / H, 0.5f, 60.0f)};
        scene::Extracted seen;
        scene::extract(yard, view, seen);
        mesh::Pass pass;
        mesh::Targets drawn;
        drawn.resize(W, H);
        pass.draw(yard, seen, view, {meshes, materials}, drawn);
        std::vector<std::uint32_t> objects;
        mesh::objects(drawn, objects);
        const xrender::PictureLayer world_picture{.rgba = drawn.colour, .width = W, .height = H, .objects = objects,
                                                  .describe = [&](int x, int y, std::string& members) { mesh::describe(drawn, x, y, members); }};

        xrender::DisplayList front;
        front_layer(front);
        raster::Surface surface{W, H};
        xrender::CpuFrame frame;
        const std::array layers{xrender::FrameLayer{.name = "world", .picture = &world_picture}, xrender::FrameLayer{.name = "ui", .list = &front, .object_ids = true}};
        frame.render(layers, surface, {.background = geom::Color::black()});
        xrender::FrameSubject subject;
        subject.show(frame, surface);
        agent::SubjectSession session({&subject});

        const auto said = ask(session, R"({"op":"frame.layers"})");
        expect(said["layers"][0]["name"].text() == "world" && said["layers"][0]["ids"].boolean() == true && said["layers"][0]["rows"].integer() == H);
        // The ball's middle on the screen, by the matrices.
        const math::Vec4 clip = view.projection * view.view * math::Vec4{1.5f, 0.6f, 0, 1};
        const int ball_x = static_cast<int>((clip.x / clip.w * 0.5f + 0.5f) * W), ball_y = static_cast<int>((0.5f - clip.y / clip.w * 0.5f) * H);
        const auto of_ball = ask(session, std::format(R"({{"op":"frame.at","at":[{},{}]}})", ball_x, ball_y));
        const auto link = of_ball["chain"][0];
        std::println("the pixel [{}, {}] of the frame: {}", ball_x, ball_y, of_ball["chain"].dump());
        expect(link["layer"].text() == "world" && link["instance"].integer() == 11 && link["mesh"].integer() == 0 && link["triangle"].integer().value_or(-1) >= 0 &&
               link["cluster"].integer().value_or(-1) >= 0);
        expect(gt(link["normal"][2].number().value_or(0), 0.7)) << "the ball's near side faces the camera";
        // The interface is over it: its button is the interface's object, as before.
        const auto of_button = ask(session, R"({"op":"frame.at","at":[30,70]})");
        expect(of_button["chain"][0]["layer"].text() == "ui" && of_button["chain"][0]["object"].integer() == 9 && !of_button["chain"][0]["instance"]);
        // Under the panel's rounded corner the world shows, and is the world's: the floor.
        const auto of_floor = ask(session, R"({"op":"frame.at","at":[150,90]})");
        expect(of_floor["chain"][0]["layer"].text() == "world" && of_floor["chain"][0]["instance"].integer() == 1);
        // The frame's pixels there are the picture's.
        const raster::Rgba8 pixel = surface.at(ball_x, ball_y);
        const std::size_t at = (static_cast<std::size_t>(ball_y) * W + static_cast<std::size_t>(ball_x)) * 4;
        expect(pixel.r == drawn.colour[at] && pixel.g == drawn.colour[at + 1] && pixel.b == drawn.colour[at + 2] && pixel.a == 255);
        // A picture of another size is not laid: the layer drew no row.
        mesh::Targets small;
        small.resize(8, 8);
        const xrender::PictureLayer other{.rgba = small.colour, .width = 8, .height = 8};
        const std::array wrong{xrender::FrameLayer{.name = "world", .picture = &other}};
        expect(eq(frame.render(wrong, surface, {}).layers[0].report.rows, 0));
    };
}};
