// A field drawn with no interface anywhere in the program (D-162): an agent
// places sand and steps the field through the agent kernel, the field is put
// on screen through the renderer's own frame entry (xrender.frame), and the
// same agent reads the picture — its layers, a pixel traced back, the pixels
// as a PNG. Two subjects in one session: `field` and `frame`.
import std;
import mlibs.agent;
import mlibs.base64;
import mlibs.image;
import mlibs.json;
import mlibs.testing;
import xengine.cellular;
import xengine.cellular.agent;
import xengine.cellular.present;
import xrender.frame;
import mcpplibs.mtest;

using namespace mlibs::testing;
using namespace mcpplibs::mtest;
namespace agent = mlibs::agent;
namespace ca = xengine::cellular;
namespace geom = mlibs::geom;
namespace raster = xrender::raster;

namespace {

constexpr ca::Shape shape{64, 48};
constexpr int       cell = 4;  // pixels a cell

// Channel 0: 1 is sand, falling a cell a step into what is empty below; outside the field is a wall.
ca::Program sand() {
    const std::array<ca::Offset, 1> below{{{0, 1}}}, above{{{0, -1}}};
    const std::array<ca::Rule, 2> rules{{
        {{ca::Condition::self({0, {1, 1}}), ca::Condition::neighbors({0, {0, 0}}, below, {1, 1})}, {{0, ca::WriteKind::set, 0}}},
        {{ca::Condition::self({0, {0, 0}}), ca::Condition::neighbors({0, {1, 1}}, above, {1, 1})}, {{0, ca::WriteKind::set, 1}}},
    }};
    return ca::Program::prepare(1, rules).value();
}

// What the program holds: a field an agent operates, its picture, the frame it is drawn into.
struct Sandbox {
    ca::agent::Field       field;
    xrender::ImageStore    images;
    ca::FieldPresenter     presenter;
    xrender::DisplayList   list;
    raster::Surface        surface{static_cast<int>(shape.width) * cell, static_cast<int>(shape.height) * cell};
    xrender::CpuFrame      frame;
    xrender::FrameSubject  picture;
    agent::SubjectSession  session;

    Sandbox()
        : field(ca::agent::Field::make(sand(), shape, {}, {.boundary = {ca::Edge::constant, 2}}).value()),
          presenter(ca::FieldPresenter::make(images, shape, 1, {{{{{0, {1, 1}}}, geom::Color::hex(0xe9c46a)}}, geom::Color::hex(0x14213d)}, {16, 16}).value()),
          session({&field, &picture}) {
        draw();
    }
    // The field as it is now, through the frame's entry: one layer, one image, one command.
    void draw() {
        expect(presenter.present(field.state().cells).has_value());
        list.clear();
        list.begin_node(1);
        presenter.draw(list, {0, 0, static_cast<float>(surface.width()), static_cast<float>(surface.height())});
        list.end_node();
        const xrender::FrameLayer layer{.name = "field", .list = &list, .resources = {&images, nullptr}, .object_ids = true};
        (void)frame.render({&layer, 1}, surface, {.background = geom::Color::black()});
        picture.show(frame, surface);
    }
    [[nodiscard]] mlibs::json::Document ask(std::string_view line) {
        std::optional<mlibs::json::Document> data;
        session.request(line, [&](std::string_view record) {
            auto parsed = mlibs::json::Document::parse(record, "record");
            expect(fatal(parsed.has_value())) << record;
            expect(parsed->root()["kind"].text() != "error") << record;
            if (parsed->root()["kind"].text() == "data" && !data) data.emplace(std::move(*parsed));
        });
        expect(fatal(data.has_value())) << line;
        return std::move(*data);
    }
};

}  // namespace

UTest cases{"simulation.cellular.offscreen", [] {
    "an agent places sand, steps the field and reads its picture — with no interface in the program"_test = [] {
        Sandbox sandbox;
        const auto who = sandbox.ask(R"({"op":"subjects"})");
        expect(who.root()["data"]["subjects"][0]["name"].text() == "field" && who.root()["data"]["subjects"][1]["name"].text() == "frame");
        const std::string empty = sandbox.ask(R"({"op":"frame.layers"})").root()["data"]["hash"].text().value_or("");

        (void)sandbox.ask(R"({"op":"field.place","channel":0,"value":1,"region":[20,2,24,6]})");
        (void)sandbox.ask(R"({"op":"field.step","steps":60})");
        sandbox.draw();

        const auto layers = sandbox.ask(R"({"op":"frame.layers"})");
        expect(layers.root()["data"]["size"].dump() == std::format("[{},{}]", shape.width * cell, shape.height * cell));
        expect(layers.root()["data"]["layers"][0]["name"].text() == "field" && layers.root()["data"]["layers"][0]["missing"]["images"].integer() == 0);
        expect(layers.root()["data"]["hash"].text() != empty) << "the picture changed with the field";

        // The sand lies on the floor: the field says so in numbers, the frame in pixels.
        const auto floor = sandbox.ask(R"({"op":"field.observe","region":[20,42,24,6]})");
        expect(floor.root()["data"]["channels"][0]["sum"].integer() == 144) << "24 × 6 grains, all fallen";
        const auto grain = sandbox.ask(std::format(R"({{"op":"frame.at","at":[{},{}]}})", 30 * cell + 1, 45 * cell + 1));
        expect(grain.root()["data"]["pixel"].dump() == "[233,196,106,255]") << grain.root()["data"]["pixel"].dump();
        expect(grain.root()["data"]["chain"][0]["layer"].text() == "field") << "a pixel traced to the layer that drew it";
        const auto sky = sandbox.ask(std::format(R"({{"op":"frame.at","at":[{},{}]}})", 30 * cell + 1, 4 * cell + 1));
        expect(sky.root()["data"]["pixel"].dump() == "[20,33,61,255]") << "where it fell from is empty again";

        // The same pixels as a PNG, kept for a reviewer.
        const auto captured = sandbox.ask(R"({"op":"frame.capture"})");
        const auto bytes = mlibs::base64::decode(captured.root()["data"]["png"].text().value_or(""));
        expect(fatal(bytes.has_value()));
        const auto image = mlibs::image::decode_png(*bytes);
        expect(fatal(image.has_value()));
        expect(eq(image->width, static_cast<int>(shape.width) * cell) && eq(image->height, static_cast<int>(shape.height) * cell));
        const auto file = scratch_dir("cellular-offscreen") / "sand.png";
        std::ofstream(file, std::ios::binary).write(reinterpret_cast<const char*>(bytes->data()), static_cast<std::streamsize>(bytes->size()));
        std::println("    the field's picture: {}", file.generic_string());

        // The same requests, the same picture.
        Sandbox again;
        (void)again.ask(R"({"op":"field.place","channel":0,"value":1,"region":[20,2,24,6]})");
        (void)again.ask(R"({"op":"field.step","steps":60})");
        again.draw();
        expect(again.ask(R"({"op":"frame.layers"})").root()["data"]["hash"].text() == layers.root()["data"]["hash"].text());
    };
}};
