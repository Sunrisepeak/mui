// A site's doors (0.1.0 plan AG5; oweb's design §4.10): asked from inside one of its pages — the subject a page's
// program adds to its runtime, its files fetched as the page fetches them — a site answers what the `oweb` command
// answers of the same site as built, and says of itself the text its build wrote as llms.txt.
import std;
import oweb;
import oweb.reader;
import mlibs.agent;
import mlibs.json;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace reader = oweb::reader;

namespace {

std::filesystem::path docs() { return std::filesystem::path(std::source_location::current().file_name()).parent_path() / "../../tests/site/data/docs"; }

std::filesystem::path scratch(std::string_view name) {
    const auto directory = std::filesystem::temp_directory_path() / "openmui-tests" / "oweb.reader" / name;
    std::error_code error;
    std::filesystem::remove_all(directory, error);
    std::filesystem::create_directories(directory, error);
    return directory;
}

std::string read(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// The data record of an answer, as sent ("" where none came).
std::string data_of(std::string_view records) {
    std::istringstream lines{std::string(records)};
    for (std::string line; std::getline(lines, line);) {
        constexpr std::string_view head = R"({"kind":"data","data":)";
        if (line.starts_with(head) && line.ends_with('}')) return line.substr(head.size(), line.size() - head.size() - 1);
    }
    return {};
}

// What the command answers.
std::string command(std::vector<std::string_view> words) {
    oweb::Sites sites;
    mlibs::agent::SubjectSession session({&sites});
    std::ostringstream out, errors;
    (void)mlibs::agent::command_line(session, words, out, errors);
    return data_of(out.str());
}

// A built site as one of its pages reaches it: the page at `route` names the index from where it is, and a file
// is the one a host would send for that address. `fetched`: every address asked for.
reader::Site served(std::filesystem::path built, std::string route, std::shared_ptr<std::vector<std::string>> fetched) {
    const std::filesystem::path page = built / route.substr(1);
    std::string up;
    for (const char character : std::string_view(route).substr(1)) up += character == '/' ? "../" : "";
    return {.link = [up](std::string_view relation) { return relation == "search" ? up + "site.search.xdef.json" : std::string(); },
            .file = [page, fetched](std::string_view address, std::function<void(std::expected<std::string, std::string>)> done) {
                fetched->emplace_back(address);
                const std::filesystem::path file = (page / address).lexically_normal();
                if (!std::filesystem::is_regular_file(file)) return done(std::unexpected(std::format("HTTP 404 for {}", address)));
                done(read(file));
            }};
}

// What the page's subject answers.
std::string page(mlibs::agent::Subject& site, std::string_view request) {
    mlibs::agent::SubjectSession session({&site});
    std::string records;
    session.request(request, [&](std::string_view record) { records += std::format("{}\n", record); });
    return data_of(records);
}

std::string field(std::string_view data, std::string_view name) {
    const auto parsed = mlibs::json::Document::parse(data, "the answer");
    return parsed ? parsed->root()[name].dump() : std::string();
}

}  // namespace

static UTest cases{"oweb.reader.doors", [] {
    "asked from inside a page, a site answers what the command answers of the site as built — its routes, a page, a search — and says of itself what llms.txt says"_test = [] {
        const std::string built = scratch("doors").generic_string();
        expect(fatal(command({"site.build", "--site", docs().generic_string(), "--out", built, "--languages", "en,zh", "--url", "https://example.org/docs/"}).contains(R"("ok":true)")));
        const auto fetched = std::make_shared<std::vector<std::string>>();
        const auto site = reader::subject(reader::keeping_tables(served(built, "/guide/start/", fetched)));

        const std::string routes = page(*site, R"({"op":"routes.list"})");
        expect(!routes.empty() && routes == command({"routes.list", "--site", built})) << routes;
        expect(*fetched == std::vector<std::string>{"../../site.routes.xdef.json"}) << fetched->size();

        for (const std::string_view as : {"markdown", "html"}) {
            const std::string read_here = page(*site, std::format(R"({{"op":"page.read","route":"/guide/start/","as":"{}"}})", as));
            expect(!read_here.empty() && read_here == command({"page.read", "--site", built, "--route", "/guide/start/", "--as", as})) << as << read_here.substr(0, 200);
        }
        // A page of another language, from this one: the address is the site's, not the page's.
        const std::string other = page(*site, R"({"op":"page.read","route":"/zh/"})");
        expect(!other.empty() && other == command({"page.read", "--site", built, "--route", "/zh/"})) << other.substr(0, 200);

        const std::string found = page(*site, R"({"op":"docs.search","query":"first window"})");
        expect(found.contains("\"found\":[{") && found == command({"docs.search", "--site", built, "--query", "first window"})) << found;

        // What it says of itself: the command's answer but for the requests to go on with, which each door writes as
        // it takes them (here without `site`) — and the text is the file a build wrote.
        const std::string described = page(*site, R"({"op":"site.describe"})"), by_command = command({"site.describe", "--site", built});
        for (const std::string_view name : {"ok", "title", "url", "languages", "routes", "built", "tables", "text"})
            expect(!field(described, name).empty() && field(described, name) == field(by_command, name)) << name << ": " << field(described, name).substr(0, 200);
        const auto parsed = mlibs::json::Document::parse(described, "the answer");
        expect(fatal(parsed.has_value()));
        expect(parsed->root()["text"].text().value_or({}) == read(std::filesystem::path(built) / "llms.txt"));
        expect(field(described, "next").contains(R"("op":"page.read")") && !field(described, "next").contains("\"site\"")) << field(described, "next");
        // The requests it gives are answered as they stand.
        for (const mlibs::json::Node& next : parsed->root()["next"].items()) expect(page(*site, next.dump()).contains(R"("ok":true)")) << next.dump();
        // The routes' table once, however many askings read it.
        expect(std::ranges::count(*fetched, "../../site.routes.xdef.json") == 1 && std::ranges::count(*fetched, "../../site.search.xdef.json") == 1) << fetched->size();
    };

    "from a page at the site's top the same, and a site published without a file says which and the way on"_test = [] {
        const std::string built = scratch("doors-top").generic_string();
        expect(fatal(command({"site.build", "--site", docs().generic_string(), "--out", built, "--languages", "en,zh"}).contains(R"("ok":true)")));
        const auto fetched = std::make_shared<std::vector<std::string>>();
        const auto site = reader::subject(reader::keeping_tables(served(built, "/", fetched)));
        expect(page(*site, R"({"op":"page.read","route":"/guide/start/"})") == command({"page.read", "--site", built, "--route", "/guide/start/"}));
        expect(std::ranges::contains(*fetched, "guide/start/index.md")) << fetched->back();

        std::filesystem::remove(std::filesystem::path(built) / "guide/start/index.md");
        mlibs::agent::SubjectSession session({site.get()});
        std::string records;
        session.request(R"({"op":"page.read","route":"/guide/start/"})", [&](std::string_view record) { records += std::format("{}\n", record); });
        expect(records.contains("guide/start/index.md did not come: HTTP 404") && records.contains(R"("op":"site.describe")") && records.contains(R"("exitCode":1)")) << records;
    };
}};
