// xdef — declared data's reference evaluator and checker (mlibs.xdef, D-209): the kinds a workspace's owners
// register, a data module's canonical snapshot, and the container's rules over a tree.
import std;
import mcpplibs.cmdline;
import mcpplibs.os;
import mlibs.xdef.tool;

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

// A generated unit written where it goes, unless it is already that; with `check`, only whether it is.
int put(const std::filesystem::path& file, const std::string& text, bool check) {
    const auto current = mcpplibs::os::fs::read(file);
    if (current && *current == text) return 0;
    if (check) {
        std::println(std::cerr, "xdef: {} is not current", mcpplibs::os::fs::utf8(file));
        return 1;
    }
    std::error_code error;
    std::filesystem::create_directories(file.parent_path(), error);
    std::ofstream(file, std::ios::binary) << text;
    if (!std::filesystem::exists(file, error)) {
        std::println(std::cerr, "xdef: cannot write {}", mcpplibs::os::fs::utf8(file));
        return 1;
    }
    return 0;
}

// The namespace given, or the module's (acme.ui → acme::ui).
std::string namespace_for(const mcpplibs::cmdline::ParsedArgs& args) {
    if (const auto given = args.value("namespace")) return std::string(*given);
    std::string ns;
    for (const char c : args.value("module").value_or("")) ns += c == '.' ? std::string("::") : std::string(1, c);
    return ns;
}

// --module, --name and --out are what every unit needs.
bool asked_fully(const mcpplibs::cmdline::ParsedArgs& args, std::string_view command) {
    for (const std::string_view option : {"module", "name", "out"})
        if (!args.value(option)) {
            std::println(std::cerr, "xdef {}: --{} is needed (--module, --name and --out)", command, option);
            return false;
        }
    return true;
}

int embed(const mcpplibs::cmdline::ParsedArgs& args) {
    if (!asked_fully(args, "embed")) return 2;
    const auto file = mcpplibs::os::fs::from_utf8(args.positional(0));
    const std::string name = mcpplibs::os::fs::utf8(file.filename());
    if (name.ends_with(".xdef.ts")) {
        // A data module is carried as what it evaluates to, with its owner's kinds: its owner's build plugin does it.
        std::println(std::cerr, "xdef embed: {} is a data module: embed its snapshot (xdef snapshot --write, then the .json), or let its owner's build plugin carry it", name);
        return 2;
    }
    const auto text = mcpplibs::os::fs::read(file);
    if (!text) {
        std::println(std::cerr, "xdef embed: cannot read {}", mcpplibs::os::fs::utf8(file));
        return 1;
    }
    const std::string part(args.value("name").value_or(""));
    const auto out = mcpplibs::os::fs::from_utf8(args.value("out").value_or(".")) / mcpplibs::os::fs::from_utf8(part + ".cppm");
    return put(out, mlibs::xdef::emit_embed(*text, name, args.value("module").value_or(""), namespace_for(args), part), args.is_flag_set("check"));
}

int embed_index(const mcpplibs::cmdline::ParsedArgs& args) {
    if (!asked_fully(args, "embed-index")) return 2;
    std::vector<std::pair<std::string, std::string>> files;
    for (std::size_t index = 0; index < args.positional_count(); ++index) {
        const std::string pair(args.positional(index));
        const auto equals = pair.find('=');
        if (equals == std::string::npos) {
            std::println(std::cerr, "xdef embed-index: `{}` is not <file>=<partition>", pair);
            return 2;
        }
        files.emplace_back(pair.substr(0, equals), pair.substr(equals + 1));
    }
    const std::string part(args.value("name").value_or(""));
    const auto out = mcpplibs::os::fs::from_utf8(args.value("out").value_or(".")) / mcpplibs::os::fs::from_utf8(part + ".cppm");
    return put(out, mlibs::xdef::emit_embed_index(files, args.value("module").value_or(""), namespace_for(args), part), args.is_flag_set("check"));
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
                         .subcommand(App("embed")
                                         .description("a data file a program carries: its text as a module partition (a build plugin's step, D-73)")
                                         .arg(Arg("file").required().help("the file (a data module is its owner's to carry)"))
                                         .option(Option("module").takes_value().value_name("M").help("the package's module"))
                                         .option(Option("name").takes_value().value_name("P").help("the partition's and the constant's name"))
                                         .option(Option("namespace").takes_value().value_name("NS").help("the constant's namespace (default: the module's)"))
                                         .option(Option("out").takes_value().value_name("DIR").help("where <name>.cppm goes"))
                                         .option(Option("check").help("only say whether the unit there is current")))
                         .subcommand(App("embed-index")
                                         .description("the files embedded under one tag as one table, a partition re-exporting theirs")
                                         .arg(Arg("files").help("<file>=<partition>, each (as many as there are)"))
                                         .option(Option("module").takes_value().value_name("M").help("the package's module"))
                                         .option(Option("name").takes_value().value_name("P").help("the table's partition and name"))
                                         .option(Option("namespace").takes_value().value_name("NS").help("the table's namespace (default: the module's)"))
                                         .option(Option("out").takes_value().value_name("DIR").help("where <name>.cppm goes"))
                                         .option(Option("check").help("only say whether the unit there is current")))
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
        std::println(std::cerr, "xdef: a command: kinds, snapshot, migrate, check, embed or embed-index (xdef --help)");
        return 2;
    }
    const std::string command(parsed->subcommand_name());
    const auto& args = parsed->subcommand()->get();
    if (command == "kinds") return kinds(args);
    if (command == "snapshot") return snapshot(args);
    if (command == "migrate") return migrate(args);
    if (command == "embed") return embed(args);
    if (command == "embed-index") return embed_index(args);
    return check(args);
}
