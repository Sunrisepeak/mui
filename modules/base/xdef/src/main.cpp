// xdef — declared data's reference evaluator and checker (mlibs.xdef, D-209): the kinds a workspace's owners
// register, a data module's canonical snapshot, and the container's rules over a tree.
import std;
import mcpplibs.cmdline;
import mcpplibs.os;
import mlibs.xdef;

namespace {

using mcpplibs::cmdline::App;
using mcpplibs::cmdline::Arg;
using mcpplibs::cmdline::Option;

// The workspace's registry: the one `--root` names, or the one the path is in.
std::optional<mlibs::xdef::Registry> registry_for(const mcpplibs::cmdline::ParsedArgs& args, const std::filesystem::path& near) {
    const auto root = args.value("root") ? std::optional(mcpplibs::os::fs::from_utf8(*args.value("root"))) : mlibs::xdef::workspace_of(near);
    if (!root) {
        std::println(std::cerr, "xdef: no workspace above {} (a mcpp.toml with [workspace]); name one with --root", mcpplibs::os::fs::utf8(near));
        return std::nullopt;
    }
    auto registry = mlibs::xdef::Registry::read(*root);
    if (!registry) {
        std::println(std::cerr, "xdef: {}", registry.error());
        return std::nullopt;
    }
    return std::move(*registry);
}

int kinds(const mcpplibs::cmdline::ParsedArgs& args) {
    const auto registry = registry_for(args, std::filesystem::current_path());
    if (!registry) return 2;
    for (const auto& kind : registry->kinds)
        std::println("{}@{}\t{}\t{}", kind.name, kind.major, kind.owner, kind.module.empty() ? "(written by tools)" : kind.module);
    for (const auto& problem : registry->problems) std::println(std::cerr, "xdef: {}", problem);
    return registry->problems.empty() ? 0 : 1;
}

int snapshot(const mcpplibs::cmdline::ParsedArgs& args) {
    const auto file = mcpplibs::os::fs::from_utf8(args.positional(0));
    const auto registry = registry_for(args, file);
    if (!registry) return 2;
    auto text = mlibs::xdef::snapshot(file, *registry);
    if (!text) {
        std::println(std::cerr, "xdef snapshot: {}", text.error().text());
        return 1;
    }
    if (!args.is_flag_set("write")) {
        std::print("{}", *text);
        return 0;
    }
    auto out = file;
    out.replace_extension(".json");
    std::ofstream(out, std::ios::binary) << *text;
    std::println("wrote {}", mcpplibs::os::fs::utf8(out));
    return 0;
}

int check(const mcpplibs::cmdline::ParsedArgs& args) {
    const auto root = args.positional(0).empty() ? std::filesystem::current_path() : mcpplibs::os::fs::from_utf8(args.positional(0));
    const auto registry = registry_for(args, root);
    if (!registry) return 2;
    const auto problems = mlibs::xdef::check(root, *registry);
    for (const auto& problem : problems) std::println("{}: {}", problem.file, problem.text);
    std::println("xdef check: {} problem(s), {} kind(s) registered", problems.size(), registry->kinds.size());
    return problems.empty() ? 0 : 1;
}

int migrate(const mcpplibs::cmdline::ParsedArgs& args) {
    const auto root = args.positional(0).empty() ? std::filesystem::current_path() : mcpplibs::os::fs::from_utf8(args.positional(0));
    const auto registry = registry_for(args, root);
    if (!registry) return 2;
    std::vector<std::string> notes;
    if (const auto kept = args.value("keep-notes")) {
        for (const auto part : std::views::split(std::string_view(*kept), ',')) notes.emplace_back(part.begin(), part.end());
    }
    const bool write = args.is_flag_set("write");
    const auto migration = mlibs::xdef::migrate(root, *registry, write, notes);
    for (const auto& [from, to] : migration.renamed) std::println("{} → {}", from, to);
    std::println("{}: {} file(s) renamed, {} suffix(es) rewritten in {} text(s)", write ? "migrated" : "would migrate (--write to do it)", migration.renamed.size(),
                 migration.replacements, migration.edited.size());
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    const auto root = Option("root").takes_value().value_name("DIR").help("the workspace (default: the one the path is in)");
    const auto app = App("xdef")
                         .description("Declared data's reference evaluator and checker (D-209)")
                         .subcommand(App("kinds").description("the kinds the workspace's owners register: kind@major, owner, define module").option(root))
                         .subcommand(App("snapshot")
                                         .description("a data module's snapshot in its canonical form")
                                         .arg(Arg("file").required().help("<name>.<kind>.xdef.ts"))
                                         .option(Option("write").help("write it beside the module (.json for .ts)"))
                                         .option(root))
                         .subcommand(App("migrate")
                                         .description("the earlier container's files (.omui.) moved to xdef: renamed, every text naming them rewritten")
                                         .arg(Arg("dir").help("the tree (default: here)"))
                                         .option(Option("write").help("do it (default: say what would change)"))
                                         .option(Option("keep-notes").takes_value().value_name("PREFIX,…").help("Markdown under these relative prefixes kept as written"))
                                         .option(root))
                         .subcommand(App("check")
                                         .description("the container's rules: every data file's kind registered, every committed snapshot current")
                                         .arg(Arg("dir").help("the tree (default: here)"))
                                         .option(root));
    auto parsed = app.parse(argc, argv);
    if (!parsed) {
        if (!parsed.error().is_error()) return 0;  // --help
        std::println(std::cerr, "xdef: {}", parsed.error().message);
        return 2;
    }
    if (!parsed->has_subcommand()) {
        std::println(std::cerr, "xdef: a command: kinds, snapshot, migrate or check (xdef --help)");
        return 2;
    }
    const std::string command(parsed->subcommand_name());
    const auto& args = parsed->subcommand()->get();
    if (command == "kinds") return kinds(args);
    if (command == "snapshot") return snapshot(args);
    if (command == "migrate") return migrate(args);
    return check(args);
}
