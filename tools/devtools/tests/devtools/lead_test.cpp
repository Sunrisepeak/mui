// omui-devtools lead and bench: a benchmark's lines become samples (a series field, a vector comparison's error
// samples); the verdict pairs rounds, takes the strongest competitor and judges from the interval's lower end; the
// bootstrap is the same on every run; capabilities by support; the corpus is the repository's writing, 1 MiB.
import std;
import mcpplibs.mtest;
import omui.devtools.bench;
import omui.devtools.lead;

using namespace mcpplibs::mtest;
namespace lead = omui::devtools::lead;

namespace {

std::filesystem::path root() { return std::filesystem::path(std::source_location::current().file_name()).parent_path().parent_path() / ".." / ".." / ".."; }

std::string joined(const std::vector<std::string>& lines) {
    std::string text;
    for (const auto& line : lines) text += line + "\n";
    return text;
}

}  // namespace

static UTest cases{"devtools.lead", [] {
    "a benchmark's JSON lines become samples, tagged with series, round and key"_test = [] {
        const auto plain = lead::samples("warming up\n{\"median_ms\": 1.5, \"ok\": true, \"name\": \"x\"}\n", "hanim", "", "lottie-200", 3);
        expect(eq(plain.size(), std::size_t{1}));
        expect(eq(plain.front(), std::string(R"({"series":"hanim","round":3,"key":"lottie-200","median_ms":1.5})"))) << plain.front();
        const auto named = lead::samples("{\"player\":\"thorvg\",\"median_ms\":0.9}\n{\"player\":\"rlottie\",\"median_ms\":40}\n", "lottie", "player", "k", 1);
        expect(eq(named.size(), std::size_t{2}) && named[1].contains("\"series\":\"rlottie\"")) << joined(named);
        // A vector comparison: one line a renderer, and the pictures' differences as error samples against the reference.
        const auto vector = lead::samples(
            R"({"scene":"fills-1000","renderer":"skia 0.1","median_ms":11.6,"skia against xrender":{"mean_difference":0.2},"its SVG against xrender":{"mean_difference":9}})", "skia", "", "", 2);
        expect(eq(vector.size(), std::size_t{2})) << joined(vector);
        expect(vector[0].contains(R"("series":"skia","round":2,"key":"fills-1000","median_ms":11.6)")) << vector[0];
        expect(vector[1].contains(R"("series":"skia","round":2,"key":"fills-1000","error_vs_xrender":0.2)")) << vector[1];
    };

    "the verdict: ratios paired by round, the strongest competitor, the interval's lower end"_test = [] {
        const std::string metrics = R"({"metric":[
            {"id":"m.lower","project":"hanim","key":"k","field":"ms","better":"lower","ours":"us","competitors":["fast","slow"]},
            {"id":"m.device","project":"xrender","better":"lower","status":"unverified","needs":"a phone","competitors":["x"]},
            {"id":"m.none","project":"hanim","key":"other","field":"ms","better":"lower","ours":"us","competitors":["fast"]}]})";
        std::vector<std::string> samples;
        for (int round = 1; round <= 8; ++round) {
            samples.push_back(std::format(R"({{"series":"us","round":{},"key":"k","ms":{}}})", round, 1.0 + 0.01 * round));
            samples.push_back(std::format(R"({{"series":"fast","round":{},"key":"k","ms":{}}})", round, 1.5 + 0.01 * round));
            samples.push_back(std::format(R"({{"series":"slow","round":{},"key":"k","ms":{}}})", round, 30.0));
        }
        samples.push_back(R"({"series":"fast","round":9,"key":"k","ms":0.1})");  // no round 9 of ours: not paired
        std::vector<std::string> skipped;
        const lead::Context context{"2026-11-16", "here-8t", "abcdef12"};
        const auto judged = lead::verdicts(metrics, joined(samples), "", context, skipped);
        expect(judged.has_value() && eq(judged->size(), std::size_t{2}));
        expect(eq(skipped, std::vector<std::string>{"m.none"}));
        if (!judged || judged->size() != 2) return;
        const std::string& record = (*judged)[0].record;
        expect(record.contains(R"("best":{"name":"fast")") && record.contains(R"("verdict":"ahead")") && record.contains(R"("rounds":8)")) << record;
        expect(record.contains(R"("others":[{"name":"slow")") && record.contains(R"("machine":"here-8t")")) << record;
        expect((*judged)[1].record.contains(R"("verdict":"unverified")")) << (*judged)[1].record;
        // An unverified metric the ledger already holds as such is not recorded again.
        const auto again = lead::verdicts(metrics, joined(samples), (*judged)[1].record + "\n", context, skipped);
        expect(again.has_value() && eq(again->size(), std::size_t{1}));
    };

    "the bootstrap interval is seeded: the same on every run, around the median"_test = [] {
        const std::vector<double> values{1.0, 1.2, 0.9, 1.1, 1.05, 0.95, 1.3, 1.0, 1.15, 0.98};
        const auto first = lead::interval(values), second = lead::interval(values);
        expect(eq(first.first, second.first) && eq(first.second, second.second));
        const double middle = lead::median(values);
        expect(first.first <= middle && middle <= first.second) << std::format("[{}, {}] around {}", first.first, first.second, middle);
        expect(eq(lead::median({3.0, 1.0, 2.0, 4.0}), 2.5));
        expect(eq(lead::tier(0.9), std::string_view("behind")) && eq(lead::tier(1.0), std::string_view("par")) &&
               eq(lead::tier(1.2), std::string_view("ahead")) && eq(lead::tier(10.0), std::string_view("far_ahead")));
    };

    "a capability: far ahead when none has it, ahead in part, par when one has it whole"_test = [] {
        expect(eq(lead::capability_verdict(std::vector<std::string>{"none", "none"}), std::string_view("far_ahead")));
        expect(eq(lead::capability_verdict(std::vector<std::string>{"none", "app-defined"}), std::string_view("ahead")));
        expect(eq(lead::capability_verdict(std::vector<std::string>{"partial", "full"}), std::string_view("par")));
    };

    "the repository's lead pages are the ledger's"_test = [] {
        const auto problems = lead::problems(root());
        std::string listed;
        for (const auto& [where, what] : problems) listed += where + ": " + what + "\n";
        expect(problems.empty()) << listed;
    };

    "the markdown corpus is the repository's writing, at least 1 MiB"_test = [] {
        const std::string text = omui::devtools::bench::corpus(root());
        expect(text.size() >= std::size_t{1} << 20);
        expect(text.contains("\n\n# ")) << "files joined by a blank line";
    };
}};
