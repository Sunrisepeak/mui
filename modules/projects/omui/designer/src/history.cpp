// Live Design's history, kept by content (omui.player:history, D-151).
module omui.designer;

import std;
import omui.core;
import mlibs.hash;

namespace omui::designer {

namespace {

std::string hash_of(std::string_view text) { return std::format("{:016x}", mlibs::hash::Fnv1a{}.text(text).value()); }

std::filesystem::path path_of(std::string_view utf8) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

std::string text_at(const Value& value, std::string_view key) {
    const Value* field = value.get(key);
    return field && field->type() == Value::Type::text ? std::string(field->as_string()) : std::string();
}

bool hex_id(std::string_view word) {
    return word.size() >= 4 && std::ranges::all_of(word, [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || c == '-'; });
}

Value value_of(const DesignCommit& commit) {
    Value::List parents;
    for (const auto& parent : commit.parents) parents.push_back(Value(parent));
    Value files(Value::Map{});
    for (const auto& [path, object] : commit.files) files.set(path, object);
    return Value{}
        .set("parents", Value(std::move(parents)))
        .set("message", commit.message)
        .set("actor", commit.actor)
        .set("branch", commit.branch)
        .set("time", commit.time)
        .set("tree", commit.tree)
        .set("files", std::move(files))
        .set("changes", commit.changes.empty() ? Value{} : parse_json(commit.changes).value_or(Value{}));
}

std::vector<std::string_view> lines_of(std::string_view text) {
    std::vector<std::string_view> out;
    for (const auto line : std::views::split(text, '\n')) out.emplace_back(line.begin(), line.end());
    return out;
}

// What a side did to the base: each change a range of base lines replaced.
struct Hunk {
    std::size_t from = 0, to = 0;  // base lines [from, to)
    std::vector<std::string_view> lines;
    friend bool operator==(const Hunk&, const Hunk&) = default;
};

std::vector<Hunk> hunks(const std::vector<std::string_view>& base, const std::vector<std::string_view>& side) {
    // The longest common subsequence, then the runs between its matches.
    const std::size_t rows = base.size(), columns = side.size();
    std::vector<std::uint32_t> table((rows + 1) * (columns + 1), 0);
    const auto cell = [&](std::size_t row, std::size_t column) -> std::uint32_t& { return table[row * (columns + 1) + column]; };
    for (std::size_t row = rows; row-- > 0;)
        for (std::size_t column = columns; column-- > 0;)
            cell(row, column) = base[row] == side[column] ? cell(row + 1, column + 1) + 1 : std::max(cell(row + 1, column), cell(row, column + 1));
    std::vector<Hunk> out;
    std::size_t row = 0, column = 0;
    Hunk open{0, 0, {}};
    bool changing = false;
    const auto flush = [&] {
        if (changing) out.push_back(open);
        changing = false;
    };
    while (row < rows || column < columns) {
        if (row < rows && column < columns && base[row] == side[column]) {
            flush();
            ++row;
            ++column;
            continue;
        }
        if (!changing) open = Hunk{row, row, {}}, changing = true;
        if (column < columns && (row == rows || cell(row, column + 1) >= cell(row + 1, column))) {
            open.lines.push_back(side[column++]);
        } else {
            open.to = ++row;
        }
    }
    flush();
    return out;
}

}  // namespace

bool valid_ref_name(std::string_view name) {
    if (name.empty() || name.size() > 100 || name == "HEAD" || name.front() == '/' || name.back() == '/' || name.contains("//") || name.contains(".."))
        return false;
    if (hex_id(name) && name.size() >= 16) return false;
    return std::ranges::all_of(name, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '/';
    });
}

TextMerge merge_text(std::string_view base, std::string_view ours, std::string_view theirs) {
    if (ours == theirs || theirs == base) return {std::string(ours), false};
    if (ours == base) return {std::string(theirs), false};
    const auto base_lines = lines_of(base);
    auto our_hunks = hunks(base_lines, lines_of(ours)), their_hunks = hunks(base_lines, lines_of(theirs));
    // Both sides' changes in base order; two that touch the same lines are one change or a conflict.
    std::vector<Hunk> all;
    std::size_t mine = 0, other = 0;
    while (mine < our_hunks.size() || other < their_hunks.size()) {
        if (other == their_hunks.size() || (mine < our_hunks.size() && our_hunks[mine].from < their_hunks[other].from)) {
            all.push_back(our_hunks[mine++]);
        } else if (mine == our_hunks.size() || their_hunks[other].from < our_hunks[mine].from) {
            all.push_back(their_hunks[other++]);
        } else if (our_hunks[mine] == their_hunks[other]) {
            all.push_back(our_hunks[mine++]);
            ++other;
        } else {
            return {std::string(ours), true};
        }
        if (all.size() >= 2) {
            const Hunk& before = all[all.size() - 2];
            const Hunk& last = all.back();
            if (last.from < before.to || (last.from == before.from && last.from == last.to)) return {std::string(ours), true};
        }
    }
    std::string out;
    std::size_t line = 0;
    bool first = true;
    const auto put = [&](std::string_view text) {
        out += std::format("{}{}", first ? "" : "\n", text);
        first = false;
    };
    for (const Hunk& hunk : all) {
        for (; line < hunk.from; ++line) put(base_lines[line]);
        for (const auto added : hunk.lines) put(added);
        line = hunk.to;
    }
    for (; line < base_lines.size(); ++line) put(base_lines[line]);
    return {std::move(out), false};
}

DesignHistory::DesignHistory(std::filesystem::path directory) : directory_(std::move(directory)) {}

std::optional<std::string> DesignHistory::read(const std::filesystem::path& file) const {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;
    return std::string{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::expected<void, std::string> DesignHistory::write(const std::filesystem::path& file, std::string_view text) const {
    std::error_code error;
    std::filesystem::create_directories(file.parent_path(), error);
    const auto partial = std::filesystem::path(file).concat(".partial");
    {
        std::ofstream out(partial, std::ios::binary | std::ios::trunc);
        out << text;
        if (!out) return std::unexpected(std::format("{} cannot be written", generic_utf8(file)));
    }
    std::filesystem::rename(partial, file, error);
    if (error) return std::unexpected(std::format("{}: {}", generic_utf8(file), error.message()));
    return {};
}

std::expected<std::string, std::string> DesignHistory::put_object(std::string_view text) {
    const std::string hash = hash_of(text);
    for (int attempt = 0; attempt < 64; ++attempt) {
        const std::string id = attempt == 0 ? hash : std::format("{}-{}", hash, attempt);
        const auto file = directory_ / "objects" / id;
        const auto there = read(file);
        if (there && *there == text) return id;
        if (there) continue;  // the same hash, another text: the next id
        if (auto written = write(file, text); !written) return std::unexpected(written.error());
        return id;
    }
    return std::unexpected(std::string("the store has too many texts of one hash"));
}

std::optional<std::string> DesignHistory::ref(std::string_view kind, std::string_view name) const {
    auto text = read(directory_ / "refs" / kind / path_of(name));
    if (!text) return std::nullopt;
    while (!text->empty() && (text->back() == '\n' || text->back() == '\r')) text->pop_back();
    return text;
}

std::map<std::string, std::string> DesignHistory::refs(std::string_view kind) const {
    std::map<std::string, std::string> out;
    const auto root = directory_ / "refs" / kind;
    std::error_code error;
    for (auto it = std::filesystem::recursive_directory_iterator(root, error); !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error)) {
        if (!it->is_regular_file()) continue;
        const std::string name = generic_utf8(it->path().lexically_relative(root));
        if (auto commit = ref(kind, name)) out.emplace(name, std::move(*commit));
    }
    return out;
}

std::string DesignHistory::head_branch() const {
    auto head = read(directory_ / "HEAD");
    if (!head) return "main";
    while (!head->empty() && (head->back() == '\n' || head->back() == '\r')) head->pop_back();
    return head->empty() ? std::string("main") : *head;
}

std::map<std::string, std::string> DesignHistory::branches() const { return refs("heads"); }
std::map<std::string, std::string> DesignHistory::tags() const { return refs("tags"); }

std::expected<DesignCommit, std::string> DesignHistory::commit_of(std::string_view id) const {
    const auto text = read(directory_ / "commits" / path_of(std::format("{}.json", id)));
    if (!text) return std::unexpected(std::format("no design commit `{}`", id));
    auto value = parse_json(*text);
    if (!value) return std::unexpected(std::format("design commit {}: {}", id, value.error().text()));
    DesignCommit commit;
    commit.id = std::string(id);
    if (const Value* parents = value->get("parents"); parents && parents->list())
        for (const Value& parent : *parents->list()) commit.parents.emplace_back(parent.as_string());
    commit.message = text_at(*value, "message");
    commit.actor = text_at(*value, "actor");
    commit.branch = text_at(*value, "branch");
    commit.tree = text_at(*value, "tree");
    if (const Value* time = value->get("time"); time && time->is_number()) commit.time = time->as_double();
    if (const Value* changes = value->get("changes"); changes && !changes->is_null()) commit.changes = changes->json();
    if (const Value* files = value->get("files"); files && files->map())
        for (const auto& [path, object] : *files->map()) commit.files.emplace(path, std::string(object.as_string()));
    return commit;
}

std::expected<std::string, std::string> DesignHistory::resolve(std::string_view name) const {
    std::size_t back = 0;
    if (const auto tilde = name.find('~'); tilde != std::string_view::npos) {
        const auto count = name.substr(tilde + 1);
        if (count.empty()) back = 1;
        else if (std::from_chars(count.data(), count.data() + count.size(), back).ec != std::errc{}) return std::unexpected(std::format("`{}`: ~ takes a count", name));
        name = name.substr(0, tilde);
    }
    std::optional<std::string> found;
    if (name == "HEAD") found = ref("heads", head_branch());
    if (!found && valid_ref_name(name)) found = ref("heads", name);
    if (!found && valid_ref_name(name)) found = ref("tags", name);
    if (!found && hex_id(name)) {
        // A commit's id, or a prefix of one that only it has.
        std::vector<std::string> matches;
        std::error_code error;
        for (const auto& entry : std::filesystem::directory_iterator(directory_ / "commits", error)) {
            const std::string stem = generic_utf8(entry.path().stem());
            if (stem.starts_with(name)) matches.push_back(stem);
        }
        if (matches.size() > 1) return std::unexpected(std::format("`{}` names {} commits: give more of the id", name, matches.size()));
        if (matches.size() == 1) found = matches.front();
    }
    if (!found) return std::unexpected(std::format("no branch, tag or design commit `{}`", name));
    for (; back > 0; --back) {
        auto commit = commit_of(*found);
        if (!commit) return std::unexpected(commit.error());
        if (commit->parents.empty()) return std::unexpected(std::format("`{}` goes back past the first commit", name));
        found = commit->parents.front();
    }
    return *found;
}

std::expected<std::map<std::string, std::string>, std::string> DesignHistory::texts_of(std::string_view name) const {
    auto id = resolve(name);
    if (!id) return std::unexpected(id.error());
    auto commit = commit_of(*id);
    if (!commit) return std::unexpected(commit.error());
    std::map<std::string, std::string> out;
    for (const auto& [path, object] : commit->files) {
        auto text = read(directory_ / "objects" / path_of(object));
        if (!text) return std::unexpected(std::format("design commit {}: the text of {} is missing ({})", *id, path, object));
        out.emplace(path, std::move(*text));
    }
    return out;
}

std::expected<DesignCommit, std::string> DesignHistory::commit(const std::map<std::string, std::string>& texts, std::string message, std::string actor,
                                                               std::string tree, std::optional<std::vector<std::string>> parents, std::string changes) {
    return commit_to(head_branch(), texts, std::move(message), std::move(actor), std::move(tree), std::move(parents), std::move(changes));
}

std::expected<DesignCommit, std::string> DesignHistory::commit_on(std::string_view branch, const std::map<std::string, std::string>& texts,
                                                                  std::string message, std::string actor, std::string tree) {
    if (!valid_ref_name(branch)) return std::unexpected(std::format("`{}` cannot name a branch (letters, digits, - _ . /)", branch));
    if (!ref("heads", branch))
        if (const auto at = resolve("HEAD"))
            if (auto made = write(directory_ / "refs" / "heads" / path_of(branch), *at + "\n"); !made) return std::unexpected(made.error());
    return commit_to(std::string(branch), texts, std::move(message), std::move(actor), std::move(tree), std::nullopt, {});
}

std::expected<std::filesystem::path, std::string> DesignHistory::keep_picture(std::span<const std::uint8_t> png) {
    const std::string_view bytes(reinterpret_cast<const char*>(png.data()), png.size());
    const auto file = directory_ / "pictures" / path_of(hash_of(bytes) + ".png");
    if (const auto there = read(file); there && *there == bytes) return file;
    if (auto written = write(file, bytes); !written) return std::unexpected(written.error());
    return file;
}

std::optional<std::filesystem::path> DesignHistory::thumbnail(std::string_view commit) const {
    const auto text = read(directory_ / "thumbnails.json");
    if (!text) return std::nullopt;
    const auto map = parse_json(*text);
    const Value* name = map ? map->get(commit) : nullptr;
    if (!name || name->type() != Value::Type::text) return std::nullopt;
    auto file = directory_ / "pictures" / path_of(name->as_string());
    std::error_code error;
    return std::filesystem::exists(file, error) ? std::optional(std::move(file)) : std::nullopt;
}

std::expected<void, std::string> DesignHistory::set_thumbnail(std::string_view commit, const std::filesystem::path& picture) {
    const auto text = read(directory_ / "thumbnails.json");
    Value map(Value::Map{});
    if (text)
        if (auto read_back = parse_json(*text); read_back && read_back->map()) map = std::move(*read_back);
    map.set(commit, generic_utf8(picture.filename()));
    return write(directory_ / "thumbnails.json", map.json());
}

std::expected<DesignCommit, std::string> DesignHistory::commit_to(const std::string& branch_name, const std::map<std::string, std::string>& texts,
                                                                  std::string message, std::string actor, std::string tree,
                                                                  std::optional<std::vector<std::string>> parents, std::string changes) {
    DesignCommit commit;
    commit.message = std::move(message);
    commit.actor = std::move(actor);
    commit.branch = branch_name;
    commit.tree = std::move(tree);
    commit.changes = std::move(changes);
    commit.time = std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
    const auto last = ref("heads", branch_name);
    if (parents) commit.parents = std::move(*parents);
    else if (last) commit.parents.push_back(*last);
    for (const auto& [path, text] : texts) {
        auto object = put_object(text);
        if (!object) return std::unexpected(object.error());
        commit.files.emplace(path, std::move(*object));
    }
    if (last && commit.parents.size() <= 1)
        if (auto previous = commit_of(*last); previous && previous->files == commit.files)
            return std::unexpected(std::format("nothing changed since {}", *last));
    const std::string text = value_of(commit).json();
    commit.id = hash_of(text);
    if (auto written = write(directory_ / "commits" / path_of(commit.id + ".json"), text); !written) return std::unexpected(written.error());
    if (auto moved = write(directory_ / "refs" / "heads" / path_of(branch_name), commit.id + "\n"); !moved) return std::unexpected(moved.error());
    if (!read(directory_ / "HEAD") && branch_name == head_branch())
        if (auto head = write(directory_ / "HEAD", branch_name + "\n"); !head) return std::unexpected(head.error());
    return commit;
}

std::expected<std::string, std::string> DesignHistory::branch(std::string_view name, std::optional<std::string_view> at) {
    if (!valid_ref_name(name)) return std::unexpected(std::format("`{}` cannot name a branch (letters, digits, - _ . /)", name));
    if (ref("heads", name)) return std::unexpected(std::format("a branch `{}` is there already", name));
    auto commit = resolve(at.value_or("HEAD"));
    if (!commit) return std::unexpected(commit.error());
    if (auto written = write(directory_ / "refs" / "heads" / path_of(name), *commit + "\n"); !written) return std::unexpected(written.error());
    return *commit;
}

std::expected<std::string, std::string> DesignHistory::switch_to(std::string_view branch_name) {
    const auto last = ref("heads", branch_name);
    if (!last) return std::unexpected(std::format("no branch `{}`", branch_name));
    if (auto written = write(directory_ / "HEAD", std::format("{}\n", branch_name)); !written) return std::unexpected(written.error());
    return *last;
}

std::expected<std::string, std::string> DesignHistory::tag(std::string_view name, std::optional<std::string_view> at) {
    if (!valid_ref_name(name)) return std::unexpected(std::format("`{}` cannot name a tag (letters, digits, - _ . /)", name));
    if (ref("tags", name)) return std::unexpected(std::format("a tag `{}` is there already", name));
    auto commit = resolve(at.value_or("HEAD"));
    if (!commit) return std::unexpected(commit.error());
    if (auto written = write(directory_ / "refs" / "tags" / path_of(name), *commit + "\n"); !written) return std::unexpected(written.error());
    return *commit;
}

std::vector<DesignCommit> DesignHistory::log(std::string_view name, std::size_t limit) const {
    std::vector<DesignCommit> out;
    auto id = resolve(name);
    for (std::optional<std::string> next = id ? std::optional(*id) : std::nullopt; next && out.size() < limit;) {
        auto commit = commit_of(*next);
        if (!commit) break;
        next = commit->parents.empty() ? std::nullopt : std::optional(commit->parents.front());
        out.push_back(std::move(*commit));
    }
    return out;
}

std::optional<std::string> DesignHistory::common_ancestor(std::string_view first, std::string_view second) const {
    auto from = resolve(first), to = resolve(second);
    if (!from || !to) return std::nullopt;
    // Every commit the first reaches, then the second's nearest among them (breadth first).
    std::set<std::string> reached;
    std::deque<std::string> queue{*from};
    while (!queue.empty()) {
        const std::string id = queue.front();
        queue.pop_front();
        if (!reached.insert(id).second) continue;
        if (auto commit = commit_of(id))
            for (const auto& parent : commit->parents) queue.push_back(parent);
    }
    std::set<std::string> seen;
    queue = {*to};
    while (!queue.empty()) {
        const std::string id = queue.front();
        queue.pop_front();
        if (reached.contains(id)) return id;
        if (!seen.insert(id).second) continue;
        if (auto commit = commit_of(id))
            for (const auto& parent : commit->parents) queue.push_back(parent);
    }
    return std::nullopt;
}

}  // namespace omui::designer
