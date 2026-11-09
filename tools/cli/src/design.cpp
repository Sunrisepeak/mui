// omui design sync: Live Design's history (.omui/design/, D-151) carried into
// git — each design commit a git commit on refs/heads/design/<branch>, its
// sources' texts over the repository's tree where the design began, its
// parents the design commit's; tags as refs/tags/design/<tag>. Done once a
// commit (.omui/design/git.json remembers which), with git's own commands and
// a separate index, so the work tree, the index and the branch checked out
// are not touched.
module omui.cli;

import std;
import mcpplibs.os;
import mcpplibs.cmdline;
import omui.core;
import omui.designer;

namespace omui::cli {

namespace {

struct Git {
    std::filesystem::path root;  // the repository's top directory
    std::filesystem::path index;  // the index the commits are written with
    std::ostream& err;

    // Who a commit is by and when (GIT_AUTHOR_*, GIT_COMMITTER_*); empty: git's own.
    std::vector<std::pair<std::string, std::optional<std::string>>> identity;

    // `git args…` in the repository, given `input`: its output without the last line break, or why not.
    std::expected<std::string, std::string> operator()(std::vector<std::string> args, std::string_view input = {}) const {
        const auto program = mcpplibs::os::env::find_executable("git");
        if (!program) return std::unexpected(std::string("git is not on PATH"));
        mcpplibs::os::process::spawn_options options;
        options.program = *program;
        options.args = std::move(args);
        options.cwd = root;
        if (!index.empty()) options.env.emplace_back("GIT_INDEX_FILE", mcpplibs::os::fs::utf8(index));
        options.env.insert(options.env.end(), identity.begin(), identity.end());
        const std::string shown = options.args.empty() ? std::string() : options.args.front();
        auto ran = mcpplibs::os::process::run(std::move(options), input);
        if (!ran) return std::unexpected(std::format("git {}: {}", shown, ran.error().message()));
        if (ran->status.shell_code() != 0) return std::unexpected(std::format("git {}: {}", shown, ran->err));
        std::string out = ran->out;
        while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
        return out;
    }
};

// The design commits a branch reaches, each after its parents.
std::vector<designer::DesignCommit> in_order(const designer::DesignHistory& history, const std::string& head) {
    std::vector<designer::DesignCommit> out;
    std::set<std::string> placed;
    std::vector<std::pair<std::string, bool>> stack{{head, false}};
    while (!stack.empty()) {
        auto [id, parents_done] = stack.back();
        stack.pop_back();
        if (placed.contains(id)) continue;
        auto commit = history.commit_of(id);
        if (!commit) continue;
        if (parents_done) {
            placed.insert(id);
            out.push_back(std::move(*commit));
            continue;
        }
        stack.emplace_back(id, true);
        for (const auto& parent : commit->parents)
            if (!placed.contains(parent)) stack.emplace_back(parent, false);
    }
    return out;
}

}  // namespace

int design_sync(const std::filesystem::path& app, bool json, std::ostream& out, std::ostream& err) {
    const designer::DesignHistory history(app / ".omui" / "design");
    const auto branches = history.branches();
    if (branches.empty()) {
        err << std::format("omui design sync: no design history in {} (design.history.commit makes one)\n", mcpplibs::os::fs::utf8(app / ".omui" / "design"));
        return 1;
    }
    Git git{app, {}, err};
    auto top = git({"rev-parse", "--show-toplevel"});
    auto prefix = git({"rev-parse", "--show-prefix"});
    auto start = git({"rev-parse", "HEAD"});
    if (!top || !prefix || !start) {
        err << std::format("omui design sync: {} is not in a git repository with a commit ({})\n", mcpplibs::os::fs::utf8(app),
                           !top ? top.error() : !prefix ? prefix.error() : start.error());
        return 1;
    }
    git.root = mcpplibs::os::fs::from_utf8(*top);
    git.index = history.directory() / "git.index";
    // A design commit is its actor's, at its time; the one who syncs commits it — git's identity, or omui's
    // where git has none (a fresh machine, CI).
    const bool committer_known = git({"config", "user.email"}).has_value();
    // Which design commits are git's already, and as which.
    const auto ledger_file = history.directory() / "git.json";
    Value ledger(Value::Map{});
    if (std::ifstream in(ledger_file, std::ios::binary); in)
        if (auto read = parse_json(std::string{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()}); read && read->map()) ledger = std::move(*read);
    int written = 0;
    for (const auto& [branch, head] : branches) {
        for (const designer::DesignCommit& commit : in_order(history, head)) {
            if (ledger.get(commit.id)) continue;
            // Over the tree the design grew from: its first parent's, or the repository's when it began.
            const Value* first_parent = commit.parents.empty() ? nullptr : ledger.get(commit.parents.front());
            const std::string base = first_parent ? std::string(first_parent->as_string()) : *start;
            auto texts = history.texts_of(commit.id);
            std::expected<std::string, std::string> step = texts ? git({"read-tree", base}) : std::unexpected(texts.error());
            if (step)
                for (const auto& [path, text] : *texts) {
                    auto blob = git({"hash-object", "-w", "--stdin"}, text);
                    step = blob ? git({"update-index", "--add", "--cacheinfo", std::format("100644,{},{}{}", *blob, *prefix, path)}) : blob;
                    if (!step) break;
                }
            std::expected<std::string, std::string> tree = step ? git({"write-tree"}) : std::unexpected(step.error());
            std::vector<std::string> args{"commit-tree", tree ? *tree : std::string()};
            for (const auto& parent : commit.parents)
                if (const Value* mapped = ledger.get(parent)) args.insert(args.end(), {"-p", std::string(mapped->as_string())});
            if (commit.parents.empty()) args.insert(args.end(), {"-p", *start});
            args.insert(args.end(), {"-m", std::format("{}\n\ndesign commit {} on {}, by {}", commit.message, commit.id, commit.branch, commit.actor)});
            std::string author;
            for (const char c : commit.actor) author += std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' ? c : '-';
            git.identity = {{"GIT_AUTHOR_NAME", commit.actor.empty() ? std::string("person") : commit.actor},
                            {"GIT_AUTHOR_EMAIL", std::format("{}@design.omui", author.empty() ? std::string("person") : author)},
                            {"GIT_AUTHOR_DATE", std::format("@{} +0000", static_cast<std::int64_t>(commit.time))}};
            if (!committer_known)
                git.identity.insert(git.identity.end(), {{"GIT_COMMITTER_NAME", std::string("omui design sync")}, {"GIT_COMMITTER_EMAIL", std::string("sync@design.omui")}});
            auto made = tree ? git(std::move(args)) : std::unexpected(tree.error());
            git.identity.clear();
            if (!made) {
                err << std::format("omui design sync: design commit {}: {}\n", commit.id, made.error());
                return 1;
            }
            ledger.set(commit.id, *made);
            ++written;
        }
        const std::string target(ledger.get(head)->as_string());
        if (auto moved = git({"update-ref", std::format("refs/heads/design/{}", branch), target}); !moved) {
            err << std::format("omui design sync: {}\n", moved.error());
            return 1;
        }
        if (json) out << Value{}.set("branch", branch).set("design", head).set("git", target).set("ref", std::format("design/{}", branch)).json() << '\n';
        else out << std::format("design/{:20} {}  (design {})\n", branch, target.substr(0, 12), head.substr(0, 12));
    }
    for (const auto& [tag, commit] : history.tags())
        if (const Value* mapped = ledger.get(commit)) (void)git({"update-ref", std::format("refs/tags/design/{}", tag), std::string(mapped->as_string())});
    std::error_code error;
    std::filesystem::remove(git.index, error);
    if (auto saved = mcpplibs::os::fs::write_atomic(ledger_file, ledger.json()); !saved) {
        err << std::format("omui design sync: {} cannot be written\n", mcpplibs::os::fs::utf8(ledger_file));
        return 1;
    }
    if (!json) out << std::format("{} design commit{} written to git\n", written, written == 1 ? "" : "s");
    return 0;
}

}  // namespace omui::cli
