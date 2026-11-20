// omui-devtools eval: a judge's answer unblinded, a sheet's majority (a tie is par), a dimension its weakest pair; a
// blinded sheet's size and letters; the first JSON object in an agent's words; a ledger line as the ledgers hold it
// (every committed compare-v1 line written back the same); a session's fresh tokens with cache reads apart.
import std;
import mcpplibs.mtest;
import nlohmann.json;
import mlibs.image;
import omui.devtools.eval;

using namespace mcpplibs::mtest;
namespace eval = omui::devtools::eval;

namespace {

std::filesystem::path root() { return std::filesystem::path(std::source_location::current().file_name()).parent_path().parent_path() / ".." / ".." / ".."; }

mlibs::image::Image filled(int width, int height, std::uint8_t value, std::uint8_t alpha = 255) {
    mlibs::image::Image out;
    out.width = width;
    out.height = height;
    out.rgba.resize(static_cast<std::size_t>(width) * height * 4);
    for (std::size_t at = 0; at < out.rgba.size(); at += 4) out.rgba[at] = out.rgba[at + 1] = out.rgba[at + 2] = value, out.rgba[at + 3] = alpha;
    return out;
}

}  // namespace

static UTest cases{"devtools.eval", [] {
    "a winner unblinded against which side was ours; the majority, a tie par; a dimension its weakest pair"_test = [] {
        expect(eq(eval::unblind("A", "A"), std::string("ahead")));
        expect(eq(eval::unblind("B", "A"), std::string("behind")));
        expect(eq(eval::unblind("even", "B"), std::string("par")));
        expect(eq(eval::majority(std::vector<std::string>{"ahead", "ahead", "behind"}), std::string("ahead")));
        expect(eq(eval::majority(std::vector<std::string>{"ahead", "behind"}), std::string("par")));
        expect(eq(eval::majority(std::vector<std::string>{"ahead", "behind", "par"}), std::string("par")));
        expect(eq(eval::weakest(std::vector<std::string>{"ahead", "par", "ahead"}), std::string("par")));
        expect(eq(eval::weakest(std::vector<std::string>{"ahead", "behind"}), std::string("behind")));
    };

    "a sheet: both pictures under a 44-pixel band, 24 apart, over white, lettered A and B in black"_test = [] {
        const auto left = filled(40, 30, 200), right = filled(50, 20, 0, 0);  // the right one transparent: white
        const auto sheet = eval::sheet(left, right);
        expect(eq(sheet.width, 40 + 24 + 50) && eq(sheet.height, 30 + 44));
        expect(eq(int(sheet.row(44)[0]), 200)) << "the left picture at the band's foot";
        expect(eq(int(sheet.row(44)[(40 + 24) * 4]), 255)) << "a transparent pixel over white";
        expect(eq(int(sheet.row(8)[(8 + 4) * 4]), 0)) << "A's apex, black";
        const auto strip = eval::row(std::vector{filled(10, 5, 9), filled(10, 8, 9)}, 2);
        expect(eq(strip.width, 22) && eq(strip.height, 8));
    };

    "the first JSON object in an agent's words, braces in strings followed"_test = [] {
        const auto found = eval::first_object("Here it is: {\"winner\": \"A\", \"reasons\": [\"a } in words\"]} — done");
        expect(fatal(found.has_value()));
        expect(eq((*found)["winner"].get<std::string>(), std::string("A")));
        expect(!eval::first_object("no object { here").has_value());
    };

    "a ledger line as the ledgers hold it: every committed compare-v1 line written back the same"_test = [] {
        std::ifstream in(root() / ".agents/docs/evaluation/ledger.eval.xdef.jsonl", std::ios::binary);
        expect(fatal(static_cast<bool>(in)));
        int compared = 0;
        for (std::string line; std::getline(in, line);) {
            if (line.find("\"compare-v1\"") == std::string::npos) continue;
            const auto record = nlohmann::ordered_json::parse(line);
            expect(eq(eval::ledger_line(record), line));
            ++compared;
        }
        expect(compared > 0) << "the ledger has compare-v1 lines";
    };

    "a session's tokens: the fresh ones (input, cache writes, output) apart from cache reads"_test = [] {
        const auto tokens = eval::tokens_of(nlohmann::json{{"input_tokens", 10}, {"cache_creation_input_tokens", 200}, {"cache_read_input_tokens", 5000}, {"output_tokens", 30}});
        expect(eq(tokens.fresh(), std::int64_t{240}) && eq(tokens.cache_read, std::int64_t{5000}));
    };
}};
