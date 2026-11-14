// An application drawn in another's window (W16, D-154): its frames arrive as
// the same display lists (the same hash, the same pixels), the images they
// use once per revision, and the person's input travels back and is handled.
import std;
import omui;
import xrender.vector;
import xrender.raster;
import xrender.text;
import mcpplibs.os;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
using namespace omui;
namespace agent = omui::agent;

namespace {

mlibs::text::FontCollection& fonts() {
    static mlibs::text::FontCollection collection = [] {
        mlibs::text::FontCollection made;
        (void)made.add_defaults();
        return made;
    }();
    return collection;
}

// The two ends of a local connection, as a host and its application have them.
struct Pair {
    std::filesystem::path         dir = std::filesystem::temp_directory_path() / std::format("omui-present-{}", std::random_device{}());
    mcpplibs::os::ipc::connection host, application;
    Pair() {
        std::filesystem::create_directories(dir);
        auto listener = mcpplibs::os::ipc::listener::listen(dir, "app");
        std::jthread accept([&] {
            if (auto peer = listener->accept_for(std::chrono::milliseconds(5000)); peer && *peer) application = std::move(**peer);
        });
        if (auto connected = mcpplibs::os::ipc::connection::connect(dir, "app")) host = std::move(*connected);
    }
    ~Pair() { std::filesystem::remove_all(dir); }
};

xrender::raster::Surface drawn(const xrender::DisplayList& list, xrender::ImageStore& images) {
    xrender::raster::Surface surface(160, 120);
    xrender::CpuRenderer renderer;
    (void)renderer.render(list, surface, {&images, nullptr});
    return surface;
}

}  // namespace

UTest cases{"present", [] {
    "the application's frames drawn by the host: the same list, the same pixels; a press in the host's window handled by the application"_test = [] {
        Pair ends;
        expect(ends.host.valid() && ends.application.valid());
        Runtime rt([] {
            RuntimeOptions options;
            options.fonts = &fonts();
            options.size = {160, 120};
            return options;
        }());
        int pressed = 0;
        Signal<bool> on{false};
        rt.mount("main", column({.padding = 8, .gap = 8}, {box({.id = "go", .width = 60, .height = 30, .background = Fill(std::string("accent")), .radius = 6,
                                                                 .label = "Go", .on = {{"press", [&](const Value&) { ++pressed; on.set(true); }}}}),
                                                            when([on] { return on.get(); }, [] { return box({.id = "done", .width = 40, .height = 20, .background = Fill(std::string("success"))}); })}));
        Presenter presenter(rt, std::move(ends.application));
        Presentation host(std::move(ends.host));
        FrameOutput first = rt.frame({seconds(0), {}});
        const auto first_hash = first.list->hash();  // the runtime's list is the next frame's after it
        expect(presenter.send(first));
        const xrender::DisplayList* shown = host.receive(std::chrono::milliseconds(2000));
        expect(shown && eq(shown->hash(), first.list->hash())) << "the same list";
        if (shown) expect(std::ranges::equal(drawn(*shown, host.images()).bytes(), drawn(*first.list, rt.images()).bytes())) << "the same pixels";

        // The person presses in the host's window: the input crosses back, the application handles it.
        const Rect go = rt.screen_bounds(rt.find("main/go"));
        const Point at{go.x + go.width / 2, go.y + go.height / 2};
        const std::array<InputEvent, 2> press{PointerEvent{.kind = PointerKind::down, .position = at, .button = MouseButton::left, .buttons = 1},
                                              PointerEvent{.kind = PointerKind::up, .position = at, .button = MouseButton::left}};
        expect(host.send_input(press));
        std::vector<InputEvent> input;
        for (int tries = 0; tries < 200 && input.size() < 2; ++tries) {
            auto more = presenter.input();
            input.insert(input.end(), more.begin(), more.end());
            if (input.size() < 2) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        expect(eq(input.size(), std::size_t{2}));
        FrameOutput second = rt.frame({seconds(0.1), std::move(input)});
        expect(eq(pressed, 1)) << "handled by the application";
        FrameOutput third = rt.frame({seconds(2), {}});  // what the press showed, settled
        expect(presenter.send(third));
        shown = host.receive(std::chrono::milliseconds(2000));
        expect(shown && eq(shown->hash(), third.list->hash())) << "the host draws the application's frame";
        expect(shown && shown->hash() != first_hash) << "what the press made";
        expect(eq(host.frames(), std::uint64_t{2}));
        // The host's window resized: the application is told.
        expect(host.resize({320, 240}, 2));
        std::optional<std::pair<Size, float>> resized;
        for (int tries = 0; tries < 200 && !resized; ++tries) {
            resized = presenter.resized();
            if (!resized) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        expect(resized && resized->first.width == 320 && resized->second == 2.0f);
    };

    "a new build takes over: the host asks the old one what the person set and gives it to the new one before showing it"_test = [] {
        Pair old_ends, new_ends;
        const auto make = [] {
            return Runtime([] {
                RuntimeOptions options;
                options.fonts = &fonts();
                options.size = {160, 120};
                return options;
            }());
        };
        Runtime old_build = make(), new_build = make();
        old_build.mount("main", column({}, {text({.id = "t", .text = "x"})}));
        new_build.mount("main", column({}, {text({.id = "t", .text = "x"})}));
        (void)old_build.frame({seconds(0), {}});
        (void)new_build.frame({seconds(0), {}});
        Presenter old_presenter(old_build, std::move(old_ends.application)), new_presenter(new_build, std::move(new_ends.application));
        Presentation old_host(std::move(old_ends.host)), new_host(std::move(new_ends.host));
        expect(old_host.ask_state());
        for (int tries = 0; tries < 200 && !old_host.state(); ++tries) {
            (void)old_presenter.input();  // the application reads what the host sent between frames
            (void)old_host.receive(std::chrono::milliseconds(5));
        }
        expect(old_host.state().has_value() && old_host.state()->get("nodes")) << "the old build's state, answered";
        if (old_host.state()) expect(new_host.restore(*old_host.state()));
    };

    "a build's problem shown beside its element: the one running says where the element built nearest above the line is"_test = [] {
        Pair ends;
        Runtime rt([] {
            RuntimeOptions options;
            options.fonts = &fonts();
            options.size = {160, 120};
            return options;
        }());
        const int title_line = static_cast<int>(std::source_location::current().line()) + 1;
        Element title = text({.id = "title", .text = "Title"});
        const int field_line = static_cast<int>(std::source_location::current().line()) + 1;
        Element field = box({.id = "field", .width = 80, .height = 20});
        rt.mount("main", column({.padding = 8, .gap = 8}, {std::move(title), std::move(field)}));
        (void)rt.frame({seconds(0), {}});
        // The compiler names the file relative to the package, the element's place may be absolute: their trailing components match.
        const std::string file = std::format("tests/{}", std::filesystem::path(std::source_location::current().file_name()).filename().string());
        expect(rt.path(built_near(rt, file, field_line)).ends_with("field")) << "a problem on the element's own line";
        expect(rt.path(built_near(rt, file, title_line + 1)).ends_with("title")) << "a problem below it, before the next";
        expect(!built_near(rt, file, title_line - 1)) << "none built above it in that file";
        expect(!built_near(rt, "other/thing.cpp", field_line)) << "another file";
        Presenter presenter(rt, std::move(ends.application));
        Presentation host(std::move(ends.host));
        expect(host.ask_places(Value(Value::List{Value{}.set("file", file).set("line", field_line), Value{}.set("file", "nope.cpp").set("line", 1)})));
        for (int tries = 0; tries < 200 && !host.places(); ++tries) {
            (void)presenter.input();
            (void)host.receive(std::chrono::milliseconds(5));
        }
        expect(fatal(host.places().has_value() && host.places()->list() && host.places()->list()->size() == 2));
        const Value& found = (*host.places()->list())[0];
        const Rect drawn = rt.drawn_bounds(rt.find("main/field"));
        expect(found.get("width") && std::abs(found.get("width")->as_double() - drawn.width) < 0.01 && std::abs(found.get("y")->as_double() - drawn.y) < 0.01)
            << found.json();
        expect((*host.places()->list())[1].is_null()) << "a place no element was built at: null";
    };

    "an application's own font: a host that lacks the face is sent it once and draws it as the application does"_test = [] {
        Pair ends;
        Runtime rt([] {
            RuntimeOptions options;
            options.fonts = &fonts();
            options.size = {200, 60};
            return options;
        }());
        rt.mount("main", column({.padding = 8}, {text({.id = "code", .text = "let answer = 42;", .style = "code"})}));
        const FrameOutput first = rt.frame({seconds(0), {}});
        // The host has Inter only: the monospace face is the application's to give.
        mlibs::text::FontCollection host_fonts;
        expect(fatal(host_fonts.add_file(mlibs::text::FontCollection::default_directory() / "Inter.ttf").has_value()));
        Presentation host(std::move(ends.host), &host_fonts);
        Presenter presenter(rt, std::move(ends.application));
        expect(presenter.send(first));
        const xrender::DisplayList* got = nullptr;
        for (int tries = 0; tries < 200 && !got; ++tries) got = host.receive(std::chrono::milliseconds(5));
        expect(fatal(got != nullptr));
        expect(eq(host_fonts.face_count(), std::size_t{2})) << "the face it lacked, added";
        const auto drawn_with = [](const xrender::DisplayList& list, mlibs::text::FontCollection& faces, xrender::ImageStore& images) {
            xrender::text::GlyphCache glyphs(faces);
            xrender::raster::Surface surface(200, 60);
            xrender::CpuRenderer renderer;
            (void)renderer.render(list, surface, {&images, &glyphs});
            return std::vector<std::uint8_t>(surface.bytes().begin(), surface.bytes().end());
        };
        const auto own = drawn_with(*first.list, fonts(), rt.images());
        const auto hosted = drawn_with(*got, host_fonts, host.images());
        expect(own == hosted) << "the same pixels as the application's own";
        long inked = 0;
        for (std::size_t index = 3; index < own.size(); index += 4) inked += own[index] != 0;
        expect(inked > 50) << "text was drawn";
        // A frame after: the face is not sent again.
        rt.mount("main", column({.padding = 8}, {text({.id = "code", .text = "let answer = 43;", .style = "code"})}));
        expect(presenter.send(rt.frame({seconds(0.1), {}})));
        const xrender::DisplayList* again = nullptr;
        for (int tries = 0; tries < 200 && !again; ++tries) again = host.receive(std::chrono::milliseconds(5));
        expect(again != nullptr && host_fonts.face_count() == 2) << "sent once";
    };

    "images: sent with the first frame that uses them, again only when they change"_test = [] {
        Pair ends;
        Runtime rt([] {
            RuntimeOptions options;
            options.fonts = &fonts();
            options.size = {160, 120};
            return options;
        }());
        std::vector<xrender::raster::Rgba8> pixels(16, xrender::raster::Rgba8{200, 10, 10, 255});
        const xrender::ImageRef picture = rt.images().add(4, 4, pixels);
        xrender::DisplayList list;
        list.draw_image(picture, {0, 0, 4, 4}, {10, 10, 40, 40}, xrender::Sampling::nearest);
        Presenter presenter(rt, std::move(ends.application));
        Presentation host(std::move(ends.host));
        expect(presenter.send({.list = &list}));
        const auto* shown = host.receive(std::chrono::milliseconds(2000));
        expect(shown && host.images().get(picture) && std::ranges::equal(host.images().get(picture)->bytes(), rt.images().get(picture)->bytes()));
        if (shown) expect(std::ranges::equal(drawn(*shown, host.images()).bytes(), drawn(list, rt.images()).bytes())) << "the picture drawn the same";
        // Changed in place: sent again at its new revision.
        auto changed = std::make_shared<xrender::raster::Surface>(4, 4);
        std::ranges::fill(changed->pixels(), xrender::raster::Rgba8{10, 10, 200, 255});
        expect(rt.images().update(picture, changed));
        expect(presenter.send({.list = &list}));
        shown = host.receive(std::chrono::milliseconds(2000));
        expect(shown && host.images().get(picture) && host.images().get(picture)->at(0, 0) == xrender::raster::Rgba8{10, 10, 200, 255}) << "the new pixels";
    };
}};
