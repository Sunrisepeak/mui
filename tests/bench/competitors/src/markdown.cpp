// mlibs.markdown's side of js/markdown.mjs: Markdown to HTML, the whole way
// — md4c's parse (mlibs.markdown is built on it), the document of blocks and
// marks, then the HTML from the document alone — against md4c's own HTML
// writer (md4w) and markdown-it, on the same text, in the same V8 when built
// for the Web. The text: every Markdown file of the repository's docs and the
// site's content (a corpus a person wrote), repeated to 1 MiB.
//
//   omui-bench-markdown CORPUS [--runs N] [--json]
//   omui-bench-markdown CORPUS --phases     the parse into the document, then the HTML from it
import std;
import mlibs.markdown;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::println(std::cerr, "usage: omui-bench-markdown CORPUS [--runs N] [--json]");
        return 2;
    }
    std::ifstream in(argv[1], std::ios::binary);
    const std::string text{std::istreambuf_iterator<char>(in), {}};
    if (text.empty()) return 1;
    int runs = 15;
    bool json = false, phases = false;
    for (int index = 2; index < argc; ++index) {
        const std::string_view option = argv[index];
        if (option == "--runs" && index + 1 < argc) runs = std::max(1, std::atoi(argv[++index]));
        else if (option == "--json") json = true;
        else if (option == "--phases") phases = true;
    }
    if (phases) {
        // Where the time goes: md4c's parse into the document, then the HTML from it.
        std::vector<double> parse_ms, html_ms;
        for (int run = 0; run < 3 + runs; ++run) {
            const auto start = std::chrono::steady_clock::now();
            const auto doc = mlibs::markdown::parse(text);
            const auto parsed = std::chrono::steady_clock::now();
            const std::string html = mlibs::markdown::to_html(doc);
            const auto done = std::chrono::steady_clock::now();
            if (run >= 3) {
                parse_ms.push_back(std::chrono::duration<double, std::milli>(parsed - start).count());
                html_ms.push_back(std::chrono::duration<double, std::milli>(done - parsed).count());
            }
        }
        std::ranges::sort(parse_ms);
        std::ranges::sort(html_ms);
        std::println(R"({{"parse_ms":{:.3f},"html_ms":{:.3f}}})", parse_ms[parse_ms.size() / 2], html_ms[html_ms.size() / 2]);
        return 0;
    }
    std::vector<double> times;
    std::size_t out = 0;
    for (int run = 0; run < 3 + runs; ++run) {
        const auto start = std::chrono::steady_clock::now();
        const std::string html = mlibs::markdown::to_html(mlibs::markdown::parse(text));
        const double spent = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        out = html.size();
        if (run >= 3) times.push_back(spent);
    }
    std::ranges::sort(times);
    const double median = times[times.size() / 2];
    const double mb_per_s = static_cast<double>(text.size()) / 1048576.0 / (median / 1000.0);
    if (json)
        std::println(R"({{"name":"mlibs.markdown","bytes":{},"html_bytes":{},"median_ms":{:.3f},"mb_per_s":{:.2f}}})", text.size(), out, median, mb_per_s);
    else
        std::println("mlibs.markdown: {} bytes to {} bytes of HTML, median {:.3f} ms, {:.2f} MB/s", text.size(), out, median, mb_per_s);
    return 0;
}
