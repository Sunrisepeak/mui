// A built directory as a static host sends it: gzip there and back, patterns, a directory measured, budgets held.
import std;
import mlibs.delivery;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace delivery = mlibs::delivery;

static UTest cases{"mlibs.delivery", [] {
    "gzip: a member that reads back; text packs, noise does not"_test = [] {
        std::string text;
        for (int line = 0; line < 400; ++line) text += std::format("<p data-omui-target=\"main/doc/b{}\">A paragraph of a page.</p>\n", line);
        const std::string packed = delivery::gzip(text);
        expect(packed.size() < text.size() / 5 && packed.starts_with("\x1f\x8b")) << packed.size();
        const auto back = delivery::gunzip(packed);
        expect(back.has_value() && *back == text);
        // Prose (words in an order that does not repeat): the harder level is not the larger. A text of one line
        // repeated is no measure of levels — the fast level's shorter search can come out ahead on it.
        constexpr std::array<std::string_view, 12> words{"tree", "render", "agent", "pixel", "route", "page", "the", "of", "a", "semantic", "reads", "written"};
        std::string prose;
        std::uint32_t state = 2026;
        for (int word = 0; word < 6000; ++word) {
            state = state * 1664525u + 1013904223u;
            prose += words[(state >> 16) % words.size()];
            prose += (state >> 8) % 9 == 0 ? ".\n" : " ";
        }
        expect(delivery::gzip(prose, 9).size() <= delivery::gzip(prose, 1).size()) << delivery::gzip(prose, 9).size() << " " << delivery::gzip(prose, 1).size();
        expect(!delivery::gunzip(packed.substr(0, packed.size() / 2)).has_value()) << "half a member is none";
        expect(delivery::gunzip(delivery::gzip("")).value_or("x").empty());
    };

    "patterns: * within a directory, ** across them, ? one character"_test = [] {
        expect(delivery::matches("**/index.html", "index.html") && delivery::matches("**/index.html", "guide/start/index.html") && !delivery::matches("**/index.html", "guide/xindex.html"));
        expect(delivery::matches("*.wasm", "app.wasm") && !delivery::matches("*.wasm", "bin/app.wasm") && delivery::matches("**/*.wasm", "bin/app.wasm"));
        expect(delivery::matches("**", "a/b/c") && delivery::matches("media/**", "media/a/b.png") && !delivery::matches("media/**", "other/a.png"));
        expect(delivery::matches("page?.md", "page1.md") && !delivery::matches("page?.md", "page10.md") && !delivery::matches("a?b", "a/b"));
        expect(delivery::compressed_kind(".html") && delivery::compressed_kind(".wasm") && !delivery::compressed_kind(".png") && !delivery::compressed_kind(".woff2"));
    };

    "a directory measured: every file both ways, what a visitor downloads, budgets each and summed, the manifest as data"_test = [] {
        const auto directory = std::filesystem::temp_directory_path() / "openmui-tests" / "mlibs-delivery";
        std::error_code error;
        std::filesystem::remove_all(directory, error);
        std::filesystem::create_directories(directory / "guide" / "start", error);
        const auto write = [&](std::string_view path, std::string_view bytes) { std::ofstream(directory / path, std::ios::binary) << bytes; };
        std::string page;
        for (int line = 0; line < 200; ++line) page += "<p>the same words again and again</p>\n";
        write("index.html", page);
        write("guide/start/index.html", page + page);
        write("picture.png", std::string(3000, '\x7f'));  // a picture is sent as it is, however it would pack

        const std::array budgets{delivery::Budget{"a page readable", "**/index.html", delivery::Measure::sent, delivery::Over::each, 400},
                                 delivery::Budget{"the pages", "**/index.html", delivery::Measure::bytes, delivery::Over::sum, 10'000},
                                 delivery::Budget{"scripts", "**/*.js", delivery::Measure::sent, delivery::Over::sum, 0},
                                 delivery::Budget{"pictures", "*.png", delivery::Measure::sent, delivery::Over::sum, 0}};
        const auto manifest = delivery::measure(directory, budgets);
        expect(manifest.has_value()) << (manifest ? "" : manifest.error());
        if (!manifest) return;
        expect(manifest->files.size() == 3u && manifest->files[0].path == "guide/start/index.html" && manifest->files[2].path == "picture.png") << "by path";
        const delivery::File& picture = manifest->files[2];
        expect(picture.bytes == 3000u && picture.gzip < 100u && !picture.compressed && picture.sent() == 3000u) << "a picture goes out as stored";
        const delivery::File& home = manifest->files[1];
        expect(home.compressed && home.sent() == home.gzip && home.gzip < home.bytes / 10);
        expect(manifest->sent == manifest->files[0].gzip + home.gzip + 3000u && manifest->bytes == page.size() * 3 + 3000u);
        expect(manifest->verdicts.size() == 4u);
        expect(manifest->verdicts[3].within && manifest->verdicts[3].found == 3000u && manifest->verdicts[3].files == 1u) << "a share: none stated, counted and held to nothing";
        expect(manifest->verdicts[0].within && manifest->verdicts[0].files == 2u && manifest->verdicts[0].where == "guide/start/index.html") << "each: the largest named";
        expect(!manifest->verdicts[1].within && manifest->verdicts[1].found == page.size() * 3 && manifest->verdicts[1].where.empty()) << "a sum over its budget";
        expect(manifest->verdicts[2].within && manifest->verdicts[2].files == 0u) << "nothing of the kind is within any budget";
        expect(!manifest->within());
        const std::string json = delivery::to_json(*manifest);
        expect(json.starts_with("{\"$format\":\"delivery@1\"") && json.contains("\"name\":\"the pages\"") && json.contains("\"within\":false") &&
               json.contains("{\"path\":\"picture.png\",\"bytes\":3000,") && json.contains("\"compressed\":false"))
            << json;
        expect(!delivery::measure(directory / "nowhere").has_value());
        const delivery::File one = delivery::measured("Guide/Page.HTML", page);
        expect(one.compressed && one.bytes == page.size() && one.gzip == home.gzip) << "a file from its bytes: the same numbers";
        expect(!delivery::measured("a.dir/README", "x").compressed) << "a dot in a directory's name is no extension";
    };
}};
