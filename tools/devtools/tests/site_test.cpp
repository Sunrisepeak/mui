// omui-devtools site: the split's page edit (the loader's decoded size for the program, stated
// once) and the newest of several tool paths by their versions, as `sort -V | tail -1` chose.
import std;
import mcpplibs.mtest;
import omui.devtools.site;

using namespace mcpplibs::mtest;
namespace site = omui::devtools::site;

UTest cases{
    "devtools.site", [] {
        "the program's decoded size replaced where the page states it once; a page that does not, refused"_test =
            [] {
                const std::string page =
                    R"(var omuiAssetBytes = {"omui-site-ab.wasm":4398351,"omui-site-ab.js":200580};)";
                const auto edited = site::with_decoded_size(page, "omui-site-ab.wasm", 10363252);
                expect(
                    edited.has_value() &&
                    *edited ==
                        R"(var omuiAssetBytes = {"omui-site-ab.wasm":10363252,"omui-site-ab.js":200580};)");
                expect(!site::with_decoded_size(page, "other.wasm", 1).has_value());
                expect(!site::with_decoded_size(page + page, "omui-site-ab.wasm", 1).has_value())
                    << "stated twice";
            };

        "the newest path: version parts compared as numbers"_test = [] {
            const auto newest =
                site::newest({"/x/emsdk/5.10.2/bin/wasm-split", "/x/emsdk/6.0.9/bin/wasm-split",
                              "/x/emsdk/5.9.0/bin/wasm-split"});
            expect(newest.has_value() &&
                   *newest == std::filesystem::path("/x/emsdk/6.0.9/bin/wasm-split"));
            expect(!site::newest({}).has_value());
        };
    }};
