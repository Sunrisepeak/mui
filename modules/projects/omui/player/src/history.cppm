// omui.player:history — Live Design's history (D-151): a design's states as
// commits, each every source's text, on branches, with tags, kept beside the
// application under .omui/design/ and addressed by content:
//   objects/<id>        a text, its id its hash (a different text of the same
//                       hash gets the next free id: the store checks, it never guesses)
//   commits/<id>.json   {parents, message, actor, time, branch, tree, files: {path: object}}
//   refs/heads/<name>   a branch's last commit; refs/tags/<name> a tag's
//   HEAD                the branch being designed on
// The player's design.history verbs read and write it; `omui design sync`
// carries it into git.
export module omui.player:history;

import std;

export namespace omui::player {

struct DesignCommit {
    std::string id;
    std::vector<std::string> parents;
    std::string message, actor, branch;
    double time = 0;                           // seconds since the epoch
    std::string tree;                          // the semantic tree's hash when committed (agent::tree_hash)
    std::map<std::string, std::string> files;  // a source's path → its text's object
};

class DesignHistory {
public:
    // The store in `directory` (<application>/.omui/design); made when first written.
    explicit DesignHistory(std::filesystem::path directory);

    [[nodiscard]] const std::filesystem::path& directory() const noexcept { return directory_; }
    // The branch HEAD names ("main" before the first commit).
    [[nodiscard]] std::string head_branch() const;
    // The commit a name means: a branch, a tag, a commit's id (or a prefix of
    // one that only it has), each with `~n` for its n-th first parent.
    [[nodiscard]] std::expected<std::string, std::string> resolve(std::string_view name) const;
    [[nodiscard]] std::expected<DesignCommit, std::string> commit_of(std::string_view id) const;
    // A commit's texts, by path.
    [[nodiscard]] std::expected<std::map<std::string, std::string>, std::string> texts_of(std::string_view name) const;

    // The texts as a commit on HEAD's branch (or with `parents` given: a
    // merge's two). Texts the same as the branch's last commit's are no commit.
    std::expected<DesignCommit, std::string> commit(const std::map<std::string, std::string>& texts, std::string message, std::string actor,
                                                   std::string tree, std::optional<std::vector<std::string>> parents = std::nullopt);
    // A branch at a commit (HEAD's when none); HEAD stays where it is.
    std::expected<std::string, std::string> branch(std::string_view name, std::optional<std::string_view> at = std::nullopt);
    // HEAD on another branch; its last commit's id.
    std::expected<std::string, std::string> switch_to(std::string_view branch);
    std::expected<std::string, std::string> tag(std::string_view name, std::optional<std::string_view> at = std::nullopt);

    // Branches and tags, each with its commit.
    [[nodiscard]] std::map<std::string, std::string> branches() const;
    [[nodiscard]] std::map<std::string, std::string> tags() const;
    // A branch's commits, newest first, along first parents.
    [[nodiscard]] std::vector<DesignCommit> log(std::string_view name, std::size_t limit = 100) const;
    // The nearest commit both reach (a merge's base), if any.
    [[nodiscard]] std::optional<std::string> common_ancestor(std::string_view first, std::string_view second) const;

private:
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

}  // namespace omui::player
