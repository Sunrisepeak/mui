// ops-gen — an operation catalog to its generated code and listings (mlibs.control.gen): the C++ partition
// (<dir>/<partition>.cppm, catalog.cppm by default) and the listings (<dir>/<product>.ops.json, .ops.md), in
// the directory the catalog names beside it unless --out says otherwise.
import std;
import mcpplibs.cmdline;
import mcpplibs.os;
import mlibs.control.gen;

namespace {

std::string read(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

}  // namespace

int main(int argc, char** argv) {
    using mcpplibs::cmdline::App, mcpplibs::cmdline::Arg, mcpplibs::cmdline::Option;
    const auto app = App("ops-gen")
                         .description("An operation catalog to its C++ table, its tools' listing (JSON) and its reference (Markdown)")
                         .arg(Arg("file").required().help("<product>.ops.omui.ts"))
                         .option(Option("out").takes_value().value_name("DIR").help("where the outputs go (default: where the catalog says, beside it)"))
                         .option(Option("check").help("write nothing; exit 1 when an output differs from what is on disk"));
    auto parsed = app.parse(argc, argv);
    if (!parsed) {
        if (!parsed.error().is_error()) return 0;  // --help
        std::println(std::cerr, "ops-gen: {}", parsed.error().message);
        return 2;
    }
    const auto file = mcpplibs::os::fs::from_utf8(parsed->positional(0));
    const bool check = parsed->is_flag_set("check");
    auto catalog = mlibs::control::gen::read_catalog_file(file);
    if (!catalog) {
        for (const auto& problem : catalog.error()) std::println(std::cerr, "{}", problem);
        return 1;
    }
    const auto dir = parsed->value("out") ? mcpplibs::os::fs::from_utf8(*parsed->value("out")) : file.parent_path() / mcpplibs::os::fs::from_utf8(catalog->generated);
    std::vector<std::pair<std::filesystem::path, std::string>> outputs;
    const auto name = mcpplibs::os::fs::utf8(file.filename());
    if (!catalog->module.empty()) outputs.emplace_back(dir / mcpplibs::os::fs::from_utf8(catalog->partition + ".cppm"), mlibs::control::gen::emit_catalog_cpp(*catalog, name));
    outputs.emplace_back(dir / mcpplibs::os::fs::from_utf8(catalog->product + ".ops.json"), mlibs::control::gen::emit_catalog_json(*catalog));
    outputs.emplace_back(dir / mcpplibs::os::fs::from_utf8(catalog->product + ".ops.md"), mlibs::control::gen::emit_catalog_markdown(*catalog));
    std::size_t stale = 0, written = 0;
    for (const auto& [path, text] : outputs) {
        if (read(path) == text) continue;
        ++stale;
        if (check) {
            std::println(std::cerr, "{} is out of date", mcpplibs::os::fs::utf8(path));
            continue;
        }
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        std::ofstream(path, std::ios::binary) << text;
        ++written;
    }
    if (check && stale) {
        std::println(std::cerr, "{} generated file(s) are out of date; run: ops-gen {}", stale, mcpplibs::os::fs::utf8(file));
        return 1;
    }
    std::println("ops-gen: {} operations, {} tiers, {} file(s) {}", catalog->operations.size(), catalog->tiers.size(), check ? outputs.size() : written,
                 check ? "checked" : "written");
    return 0;
}
