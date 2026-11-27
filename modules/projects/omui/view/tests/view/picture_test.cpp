// A picture a host has: where the host answers for a picture's source (a Web page's own pictures, decoded by its
// browser; a site's files as its pages are written), its pixels are the picture's — asked before any file is read;
// what the host does not answer for is a file's, as before.
import std;
import omui.view;
import mcpplibs.mtest;

using namespace omui;
using namespace mcpplibs::mtest;

static UTest cases{"picture", [] {
    "a host's picture: its pixels and its size are the node's; one it has that did not come says why; one it has not is a file's"_test = [] {
        RuntimeOptions options;
        options.size = {200, 200};
        Runtime rt(options);
        std::vector<std::string> asked;
        rt.set_picture_reader([&](std::string_view source) -> std::optional<std::expected<Picture, std::string>> {
            asked.emplace_back(source);
            if (source == "media/mark.svg") {
                Picture mark{40, 20, std::vector<std::uint8_t>(40 * 20 * 4, 255), false};
                mark.rgba[3] = 128;  // one pixel half clear: premultiplied as it is stored
                return mark;
            }
            if (source == "media/lost.png") return std::unexpected(std::string("the page's picture media/lost.png did not load"));
            return std::nullopt;
        });
        std::vector<std::string> said;
        Element mark = image({.id = "mark", .source = "media/mark.svg", .alt = "The mark"});
        mark.on("loaded", [&](const Value& value) { said.push_back(std::format("loaded {}x{}", value.get("width")->as_int(), value.get("height")->as_int())); });
        Element lost = image({.id = "lost", .source = "media/lost.png", .alt = "Lost"});
        lost.on("failed", [&](const Value& value) { said.push_back(std::string(value.get("reason")->as_string())); });
        Element file = image({.id = "file", .source = "media/no-such-file.png", .alt = "A file"});
        file.on("failed", [&](const Value&) { said.emplace_back("the file's decoder was asked"); });
        rt.mount("main", column({.align = "start"}, {std::move(mark), std::move(lost), std::move(file)}));
        expect(rt.frame({seconds(0), {}}).failure.empty());
        expect(asked == std::vector<std::string>{"media/mark.svg", "media/lost.png", "media/no-such-file.png"}) << asked.size();
        const Rect box = rt.bounds(rt.find("main/mark"));
        expect(box.width == 40.0f && box.height == 20.0f) << box.width << "x" << box.height << ": the picture's own size";
        expect(rt.bounds(rt.find("main/lost")).height == 0.0f) << "a picture that did not come takes no room";
        expect(std::ranges::contains(said, std::string("loaded 40x20")) && std::ranges::contains(said, std::string("the page's picture media/lost.png did not load")) &&
               std::ranges::contains(said, std::string("the file's decoder was asked")))
            << said.size();
    };
}};
