// mlibs.agent:history — a subject's history (D-151, D-160; D-173: the kernel's, for any domain): its states as
// commits, each every source's text, on branches, with tags, kept in a directory its owner names and addressed by
// content:
//   objects/<id>        a text, its id its hash (a different text of the same
//                       hash gets the next free id: the store checks, it never guesses)
//   commits/<id>.json   {parents, message, actor, time, branch, state, files: {path: object}, changes}
//   refs/heads/<name>   a branch's last commit; refs/tags/<name> a tag's
//   HEAD                the branch being worked on
// What a source is — an interface's TypeScript, a definition of matter, a scene — is the owner's: a design
// adapter gives the texts and takes them back. Live Design's verbs for an interface are omui.designer's over
// this store; a material lab's are its own over the same.
export module mlibs.agent:history;

import std;
import mlibs.json;

export namespace mlibs::agent {

struct Commit {
    std::string id;
    std::vector<std::string> parents;
    std::string message, actor, branch;
    double time = 0;                           // seconds since the epoch
    std::string state;                         // the subject's state when committed, as its hash ("" when not known): what an interface shows, a field's cells
    std::map<std::string, std::string> files;  // a source's path → its text's object
    std::string changes;                       // what the step changed, as the subject says it: JSON ("" when not known)
};

class History {
public:
    // The store in `directory` (an application's .omui/design, a lab's own); made when first written.
    explicit History(std::filesystem::path directory);

    [[nodiscard]] const std::filesystem::path& directory() const noexcept { return directory_; }
    // The branch HEAD names ("main" before the first commit).
    [[nodiscard]] std::string head_branch() const;
    // The commit a name means: a branch, a tag, a commit's id (or a prefix of
    // one that only it has), each with `~n` for its n-th first parent.
    [[nodiscard]] std::expected<std::string, std::string> resolve(std::string_view name) const;
    [[nodiscard]] std::expected<Commit, std::string> commit_of(std::string_view id) const;
    // A commit's texts, by path.
    [[nodiscard]] std::expected<std::map<std::string, std::string>, std::string> texts_of(std::string_view name) const;

    // The texts as a commit on HEAD's branch (or with `parents` given: a
    // merge's two). Texts the same as the branch's last commit's are no commit.
    std::expected<Commit, std::string> commit(const std::map<std::string, std::string>& texts, std::string message, std::string actor,
                                                   std::string state, std::optional<std::vector<std::string>> parents = std::nullopt,
                                                   std::string changes = {});
    // The texts as a commit on another branch than HEAD's (a variant's), the
    // branch made at HEAD's commit when it is not there; HEAD stays.
    std::expected<Commit, std::string> commit_on(std::string_view branch, const std::map<std::string, std::string>& texts, std::string message,
                                                      std::string actor, std::string state);
    // A picture (PNG) kept by content under pictures/: its file.
    std::expected<std::filesystem::path, std::string> keep_picture(std::span<const std::uint8_t> png);
    // A commit's picture, once one is kept for it (a thumbnail: thumbnails.json).
    [[nodiscard]] std::optional<std::filesystem::path> thumbnail(std::string_view commit) const;
    std::expected<void, std::string> set_thumbnail(std::string_view commit, const std::filesystem::path& picture);
    // A branch at a commit (HEAD's when none); HEAD stays where it is.
    std::expected<std::string, std::string> branch(std::string_view name, std::optional<std::string_view> at = std::nullopt);
    // HEAD on another branch; its last commit's id.
    std::expected<std::string, std::string> switch_to(std::string_view branch);
    std::expected<std::string, std::string> tag(std::string_view name, std::optional<std::string_view> at = std::nullopt);

    // Branches and tags, each with its commit.
    [[nodiscard]] std::map<std::string, std::string> branches() const;
    [[nodiscard]] std::map<std::string, std::string> tags() const;
    // A branch's commits, newest first, along first parents.
    [[nodiscard]] std::vector<Commit> log(std::string_view name, std::size_t limit = 100) const;
    // The nearest commit both reach (a merge's base), if any.
    [[nodiscard]] std::optional<std::string> common_ancestor(std::string_view first, std::string_view second) const;

private:
    std::expected<Commit, std::string> commit_to(const std::string& branch, const std::map<std::string, std::string>& texts, std::string message,
                                                      std::string actor, std::string state, std::optional<std::vector<std::string>> parents,
                                                      std::string changes);
    [[nodiscard]] std::expected<std::string, std::string> put_object(std::string_view text);
    [[nodiscard]] std::optional<std::string> read(const std::filesystem::path& file) const;
    std::expected<void, std::string> write(const std::filesystem::path& file, std::string_view text) const;
    [[nodiscard]] std::optional<std::string> ref(std::string_view kind, std::string_view name) const;
    [[nodiscard]] std::map<std::string, std::string> refs(std::string_view kind) const;

    std::filesystem::path directory_;
};

// A name a branch or tag may have: letters, digits, `-`, `_`, `.` and `/`
// between words; never a commit's id form, `HEAD`, or with `~`.
[[nodiscard]] bool valid_ref_name(std::string_view name);

// Three texts' merge, by whole lines: what changed on one side only taken, the
// same change on both taken once; overlapping different changes a conflict.
struct TextMerge {
    std::string text;
    bool conflict = false;
};
[[nodiscard]] TextMerge merge_text(std::string_view base, std::string_view ours, std::string_view theirs);

// Where two texts differ, by whole lines: the first line that does (from 1), and the lines there before and after.
struct LineChange {
    std::size_t line = 1;
    std::string before, after;
};
[[nodiscard]] LineChange changed_lines(std::string_view before, std::string_view after);

// What is designed, to its history (D-160's L0): a subject's sources as they are now, and a way to make them
// others. A domain's design adapter is this — an interface's scripts run at once, a project's files a build
// runs, a definition of matter prepared and stepped.
class Sources {
public:
    virtual ~Sources() = default;
    // Every source's text as it is now (path → text): a state of the design.
    [[nodiscard]] virtual std::map<std::string, std::string> texts() = 0;
    // Why texts are put in — how a subject may present the change (an interface shows a switch as a switch).
    enum class Reason : std::uint8_t { edit, switch_to, undo };
    // These texts put into what runs (each that differs; where sources come and go, one that is not among
    // them is no longer there); what changed, as the subject says it: a JSON object.
    virtual std::expected<std::string, std::string> apply(const std::map<std::string, std::string>& texts, Reason reason) = 0;
    // What runs now, as a hash ("" when it is not known here).
    [[nodiscard]] virtual std::string state() = 0;
};

// A commit as an operation answers it: {id, message, actor, branch, time, state, parents, files: [paths]} —
// or, against another commit (its parent), `changed` in place of `files`: the sources that are not the same
// in both, those it has no longer among them.
[[nodiscard]] std::string commit_text(const Commit& commit, const Commit* against = nullptr);

// The history's operations over any subject's sources (D-151's verbs; D-160's L1), one implementation for every
// domain: a subject's catalog declares them under its own names and gives each request here. Each answers a
// JSON object, or says why not.
//   history  {branch?, limit?}  → {head, branches, tags, log, uncommitted}
//   commit   {message}          → the commit
//   branch   {name, at?}        → {branch, at}
//   switch   {to, force?}       → what changed, {branch, at, detached?}; refused with changes not committed
//   revert   {to}               → what changed, {commit}: the texts of `to` put back, as a new commit
//   diff     {from?, to?}       → {from, to, files: [{path, line, before, after}]} (`to` none: what runs now)
//   merge    {from}             → what changed, {commit}: by lines, per source; conflicting lines merge nothing;
//                                 a source one side removed and the other left as it was is removed
//   tag      {name, at?}        → {tag, at}
// What the subject calls the operations an answer advises (its catalog's names): "design.history.commit".
struct HistoryNames {
    std::string list = "history", commit = "commit", edit = "edit";
};
class HistoryOperations {
public:
    HistoryOperations(History& history, Sources& sources, HistoryNames names = {}) : history_(history), sources_(sources), names_(std::move(names)) {}

    std::expected<std::string, std::string> run(std::string_view operation, mlibs::json::Node parameters, std::string_view actor);
    // The commit shown: one seen (a switch to a commit that is no branch's last), else the branch's last ("" when none).
    [[nodiscard]] std::string shown_now() const;
    std::string shown_commit;
    // A commit is answered with the sources it changed (against its first parent) in place of all it holds:
    // for a design of many sources, where a commit touches one.
    bool by_change = false;
    // A commit as these operations answer it.
    [[nodiscard]] std::string said(const Commit& commit) const;

private:
    History& history_;
    Sources& sources_;
    HistoryNames names_;
};

}  // namespace mlibs::agent
