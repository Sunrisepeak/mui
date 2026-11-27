// A site's doors (0.1.0 plan AG5): the command asked of a site's source and of the site as built answers the same —
// what is published is what is read — and what a site says of itself first is one text at every door: the command's
// `site.describe` and the llms.txt an agent that only fetches is given.
import std;
import oweb;
import mlibs.json;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;

namespace {

std::filesystem::path data() { return std::filesystem::path(std::source_location::current().file_name()).parent_path() / "data"; }

std::filesystem::path scratch(std::string_view name) {
    const auto directory = std::filesystem::temp_directory_path() / "openmui-tests" / "oweb" / name;
    std::error_code error;
    std::filesystem::remove_all(directory, error);
    std::filesystem::create_directories(directory, error);
    return directory;
}

std::string read(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// What the command answers a request with: its data record, and how it ended.
struct Answered {
    int         exit = 0;
    std::string data;    // the data record's object, as sent
    std::string errors;
};
Answered asked(std::vector<std::string_view> words) {
    oweb::Sites sites;
    mlibs::agent::SubjectSession session({&sites});
    std::ostringstream out, errors;
    Answered answered{mlibs::agent::command_line(session, words, out, errors), {}, errors.str()};
    std::istringstream lines(out.str());
    for (std::string line; std::getline(lines, line);) {
        constexpr std::string_view head = R"({"kind":"data","data":)";
        if (line.starts_with(head) && line.ends_with('}')) answered.data = line.substr(head.size(), line.size() - head.size() - 1);
    }
    return answered;
}

// One field of an answer's data, as text ("" where it is none).
std::string field(const Answered& answered, std::string_view name) {
    const auto parsed = mlibs::json::Document::parse(answered.data, "the answer");
    if (!parsed) return {};
    return std::string(parsed->root()[name].text().value_or({}));
}

}  // namespace

static UTest cases{"oweb.doors", [] {
    "the command reads a site as built as it reads its source: the same routes, the same page, the same sections found"_test = [] {
        const std::string site = (data() / "docs").generic_string(), built = scratch("doors-built").generic_string();
        const auto made = asked({"site.build", "--site", site, "--out", built, "--languages", "en,zh", "--url", "https://example.org/docs/"});
        expect(fatal(made.exit == 0)) << made.errors << made.data;
        expect(oweb::is_built(built) && !oweb::is_built(site));

        // Its routes: one table, read back as it was written.
        const auto listed_source = asked({"routes.list", "--site", site, "--languages", "en,zh"}), listed_built = asked({"routes.list", "--site", built});
        expect(listed_source.exit == 0 && listed_built.exit == 0) << listed_source.errors << listed_built.errors;
        expect(!listed_source.data.empty() && listed_source.data == listed_built.data) << listed_source.data << "\n" << listed_built.data;

        // A page: the Markdown evaluated now from the source is the file the build wrote — the one a host serves.
        for (const std::string_view as : {"markdown", "html"}) {
            const auto from_source = asked({"page.read", "--site", site, "--route", "/guide/start/", "--languages", "en,zh", "--as", as});
            const auto from_built = asked({"page.read", "--site", built, "--route", "/guide/start/", "--as", as});
            expect(from_source.exit == 0 && from_built.exit == 0) << from_source.errors << from_built.errors;
            const std::string text = field(from_built, "text");
            expect(!text.empty() && text == read(std::filesystem::path(built) / "guide/start" / (as == "html" ? "index.html" : "index.md"))) << as;
            // The Markdown is the same either way; the HTML of a source's page is written for no address (the build's
            // knows where the site is served), so only the built one is held to the file.
            if (as == "markdown") expect(field(from_source, "text") == text);
        }

        // A question: the index the site serves answers as every page evaluated now does.
        const auto found_source = asked({"docs.search", "--site", site, "--query", "first window", "--languages", "en,zh"});
        const auto found_built = asked({"docs.search", "--site", built, "--query", "first window"});
        expect(found_source.exit == 0 && found_built.exit == 0) << found_source.errors << found_built.errors;
        expect(found_source.data.contains("\"found\":[{") && found_source.data == found_built.data) << found_source.data << "\n" << found_built.data;
    };

    "what a site says of itself is one text at every door: site.describe of its source and as built, and the llms.txt it serves"_test = [] {
        const std::string site = (data() / "docs").generic_string(), built = scratch("doors-described").generic_string();
        expect(fatal(asked({"site.build", "--site", site, "--out", built, "--languages", "en,zh", "--url", "https://example.org/docs/"}).exit == 0));
        const auto of_source = asked({"site.describe", "--site", site, "--languages", "en,zh", "--url", "https://example.org/docs/"}), of_built = asked({"site.describe", "--site", built});
        expect(of_source.exit == 0 && of_built.exit == 0) << of_source.errors << of_built.errors;
        const std::string served = read(std::filesystem::path(built) / "llms.txt");
        expect(!served.empty() && field(of_built, "text") == served) << field(of_built, "text");
        expect(field(of_source, "text") == served) << field(of_source, "text");
        expect(served.starts_with("# ") && served.contains("](https://example.org/docs/guide/start/index.md)")) << served;
        // The facts beside the text: the site's own, its routes each with its Markdown's address, and the requests
        // to go on with — sent as they stand.
        expect(field(of_built, "url") == "https://example.org/docs/" && of_built.data.contains(R"("languages":["en","zh"])")) << of_built.data;
        expect(of_built.data.contains(R"("path":"/guide/start/")") && of_built.data.contains(R"("markdown":"/guide/start/index.md")")) << of_built.data;
        expect(of_built.data.contains(R"("built":true)") && of_source.data.contains(R"("built":false)"));
        expect(of_built.data.contains(std::format(R"({{"op":"page.read","site":"{}","route":"/"}})", built))) << of_built.data;
    };

    "a site as built that is not whole says what is missing and how to have it again; a route it has not, the ones it has"_test = [] {
        const std::string site = (data() / "docs").generic_string(), built = scratch("doors-partial").generic_string();
        expect(fatal(asked({"site.build", "--site", site, "--out", built, "--languages", "en,zh"}).exit == 0));
        const auto missing = asked({"page.read", "--site", built, "--route", "/nowhere/"});
        expect(missing.exit == 1 && missing.data.contains("no route `/nowhere/`") && missing.data.contains("/guide/start/")) << missing.data;
        // A configuration is a source's: a built site is read as it was written.
        const auto configured = asked({"routes.list", "--site", built, "--config", (data() / "stated/oweb.config.ts").generic_string()});
        expect(configured.exit != 0 && configured.data.contains("--config is for a site's source")) << configured.data;
        std::error_code error;
        std::filesystem::remove(std::filesystem::path(built) / "site.search.xdef.json", error);
        const auto no_index = asked({"docs.search", "--site", built, "--query", "window"});
        expect(no_index.exit == 1 && no_index.data.contains("site.search.xdef.json") && no_index.data.contains("oweb site.build")) << no_index.data;
        std::filesystem::remove(std::filesystem::path(built) / "guide/start/index.md", error);
        const auto no_page = asked({"page.read", "--site", built, "--route", "/guide/start/"});
        expect(no_page.exit == 1 && no_page.data.contains("is not there") && no_page.data.contains("oweb site.build")) << no_page.data;
    };
}};
