module omui.devtools.ci;

import std;

namespace omui::devtools::ci {

namespace {

constexpr std::size_t head_per_case = 4;
constexpr std::size_t head_per_failure = 10;
constexpr std::size_t build_window = 8;  // a compiler's errors follow mcpp's line within a few lines

std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) text.remove_suffix(1);
    return text;
}

// The line without its terminal colours.
std::string without_escapes(std::string_view text) {
    std::string plain;
    plain.reserve(text.size());
    for (std::size_t at = 0; at < text.size(); ++at) {
        if (text[at] == '\x1b' && at + 1 < text.size() && text[at + 1] == '[') {
            at += 2;
            while (at < text.size() && !std::isalpha(static_cast<unsigned char>(text[at]))) ++at;
            continue;
        }
        plain.push_back(text[at]);
    }
    return plain;
}

// "2026-11-15T06:55:32.6215892Z text" → "text" (and a byte-order mark before it).
std::string_view without_stamp(std::string_view text) {
    if (text.starts_with("\xEF\xBB\xBF")) text.remove_prefix(3);
    if (text.size() > 20 && std::isdigit(static_cast<unsigned char>(text[0])) && text[4] == '-' && text[10] == 'T') {
        const auto end = text.find("Z ");
        if (end != std::string_view::npos && end < 40) return text.substr(end + 2);
    }
    return text;
}

// "name ... FAIL (why)" or "name ... ok (…)": the program, whether it passed, why.
struct Status {
    std::string program;
    bool passed = false;
    std::string why;
};

std::optional<Status> program_status(std::string_view line) {
    const auto dots = line.find(" ... ");
    if (dots == std::string_view::npos || dots == 0) return std::nullopt;
    const std::string_view program = line.substr(0, dots);
    if (program.find(' ') != std::string_view::npos) return std::nullopt;
    std::string_view rest = line.substr(dots + 5);
    Status status{std::string(program)};
    if (rest.starts_with("ok")) status.passed = true;
    else if (!rest.starts_with("FAIL")) return std::nullopt;
    const auto open = rest.find('(');
    if (open != std::string_view::npos && rest.ends_with(')')) status.why = std::string(rest.substr(open + 1, rest.size() - open - 2));
    return status;
}

// "Workspace member 'path' (68/83) FAILED — …": the member and whether it failed.
std::optional<std::pair<std::string, bool>> member_result(std::string_view line) {
    constexpr std::string_view lead = "Workspace member '";
    if (!line.starts_with(lead)) return std::nullopt;
    const auto close = line.find("' (", lead.size());
    if (close == std::string_view::npos) return std::nullopt;
    const auto after = line.find(") ", close);
    if (after == std::string_view::npos) return std::nullopt;
    const std::string_view verdict = line.substr(after + 2);
    if (!verdict.starts_with("FAILED") && !verdict.starts_with("ok")) return std::nullopt;
    return std::pair{std::string(line.substr(lead.size(), close - lead.size())), verdict.starts_with("FAILED")};
}

bool name_char(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.'; }

// Whether `name` stands in `text` as a whole name (not inside a longer one).
bool names(std::string_view text, std::string_view name) {
    if (name.empty()) return false;
    for (auto at = text.find(name); at != std::string_view::npos; at = text.find(name, at + 1)) {
        // A dotted name stands at the end of a longer one too: `host.sdl3` in `omui.host.sdl3`.
        const bool before = at == 0 || !name_char(text[at - 1]) || (text[at - 1] == '.' && name.find('.') != std::string_view::npos);
        const std::size_t end = at + name.size();
        // A dot ending a sentence after the name still ends the name.
        const bool after = end == text.size() || !name_char(text[end]) ||
                           (text[end] == '.' && (end + 1 == text.size() || !name_char(text[end + 1])));
        if (before && after) return true;
    }
    return false;
}

// One row's state while its lines are read.
struct Reading {
    Row row;
    std::vector<Failure> pending;  // this member's failures so far
    Failure* current = nullptr;    // the failing program whose output is being read
    std::size_t head_left = 0;     // lines still to keep under the last failing case
    std::size_t build_tail = 0;      // lines after "build failed in …" whose errors are the build's
    std::vector<std::string> last_build;  // the last failed build and its errors
    std::set<std::string> not_built;

    void line(std::string_view text) {
        const std::string_view body = trim(text);
        if (auto status = program_status(body)) {
            head_left = 0;
            if (status->passed) {
                current = nullptr;
                return;
            }
            pending.push_back(Failure{status->program, status->why});
            current = &pending.back();
            return;
        }
        if (auto member = member_result(body)) {
            if (member->second) {
                Member failed{member->first, std::move(pending)};
                if (not_built.contains(failed.path))
                    failed.failures.push_back(Failure{"", "did not build", {}, last_build});
                row.members.push_back(std::move(failed));
            }
            pending.clear();
            current = nullptr;
            head_left = 0;
            return;
        }
        if (body.starts_with("error: member '")) {
            const auto close = body.find('\'', 15);
            if (close != std::string_view::npos) not_built.insert(std::string(body.substr(15, close - 15)));
            return;
        }
        if (body.starts_with("error: workspace test:")) {
            row.summary = std::string(body.substr(7));
            return;
        }
        if (body.starts_with("error: build failed in ")) {
            row.build_errors.push_back(std::string(body.substr(7)));
            last_build = {std::string(body.substr(7))};
            build_tail = build_window;
            return;
        }
        if (body.starts_with("error: test result:")) return;
        // The compiler's errors come within a few lines after mcpp's line that names the package.
        if (build_tail > 0) {
            --build_tail;
            if (body.starts_with("error:")) {
                row.build_errors.push_back(std::string(body.substr(0, 300)));
                last_build.push_back(row.build_errors.back());
            }
        }
        if (!current) return;
        if (body.starts_with("ok ")) {
            current->last_passed = std::string(trim(body.substr(3)));
            head_left = 0;
            return;
        }
        if (body.starts_with("FAIL ")) {
            head_left = 0;
            std::string_view name = trim(body.substr(5));
            const auto counts = name.rfind("  (");
            if (counts != std::string_view::npos) name = name.substr(0, counts);
            // "nav  (1 of 4 cases failed)" is the suite's sum, not a case.
            if (name.find('/') == std::string_view::npos) return;
            current->cases.push_back(std::string(name));
            head_left = head_per_case;
            return;
        }
        if (head_left > 0 && !body.empty() && current->head.size() < head_per_failure) {
            current->head.push_back(std::string(body.substr(0, 300)));
            --head_left;
        }
    }
};

}  // namespace

std::vector<Row> read_log(std::string_view log) {
    std::vector<Reading> rows;
    std::map<std::string, std::size_t, std::less<>> index;
    while (!log.empty()) {
        const auto end = log.find('\n');
        const std::string_view raw = log.substr(0, end);
        log = end == std::string_view::npos ? std::string_view{} : log.substr(end + 1);
        // `gh run view --log-failed`: job, step, line; one job's log from the API (a run still going): the line.
        std::string_view job = "log", text = raw;
        if (const auto first = raw.find('\t'); first != std::string_view::npos) {
            const auto second = raw.find('\t', first + 1);
            if (second == std::string_view::npos) continue;
            job = raw.substr(0, first), text = raw.substr(second + 1);
        }
        auto found = index.find(job);
        if (found == index.end()) {
            found = index.emplace(std::string(job), rows.size()).first;
            rows.push_back(Reading{Row{std::string(job)}});
        }
        rows[found->second].line(without_escapes(without_stamp(text)));
    }
    std::vector<Row> read;
    for (auto& reading : rows) read.push_back(std::move(reading.row));
    return read;
}

std::vector<Red> read_red_list(std::string_view status) {
    std::vector<Red> reds;
    const auto start = status.find("\n## CI red list");
    if (start == std::string_view::npos) return reds;
    std::string_view rest = status.substr(start + 1);
    rest.remove_prefix(std::min(rest.size(), rest.find('\n')));
    const auto next = rest.find("\n## ");
    if (next != std::string_view::npos) rest = rest.substr(0, next);
    for (const auto line : std::views::split(rest, '\n')) {
        const std::string_view text(line.begin(), line.end());
        if (!text.starts_with("| ")) continue;
        std::vector<std::string_view> cells;
        for (const auto cell : std::views::split(text.substr(1), '|')) cells.push_back(trim(std::string_view(cell.begin(), cell.end())));
        while (!cells.empty() && cells.back().empty()) cells.pop_back();
        if (cells.size() < 3) continue;
        int number = 0;
        const auto [end, error] = std::from_chars(cells[0].data(), cells[0].data() + cells[0].size(), number);
        if (error != std::errc{} || end != cells[0].data() + cells[0].size()) continue;
        reds.push_back(Red{number, std::string(cells[1]), std::string(cells.back())});
    }
    return reds;
}

Names names_of(const Member& member, const Failure& failure, std::string_view package) {
    Names found;
    const auto add = [](std::vector<std::string>& list, std::string name) {
        if (!name.empty() && !std::ranges::contains(list, name)) list.push_back(std::move(name));
    };
    for (const auto& name : failure.cases) {
        const auto slash = name.find('/');
        const std::string label = name.substr(0, slash);
        if (label.find('.') != std::string::npos) add(found.own, label);
        // The case's words: its first sixty characters are its own.
        add(found.own, name.substr(slash + 1, 60));
    }
    if (!failure.program.empty()) add(found.own, failure.program);
    add(found.own, member.path);
    add(found.package, std::string(package));
    if (package.ends_with(".cts")) add(found.package, std::string(package.substr(0, package.size() - 4)));
    return found;
}

namespace {

std::vector<int> rows_naming(std::span<const std::string> names_given, std::span<const Red> reds) {
    std::vector<int> found;
    for (const Red& red : reds)
        for (const std::string& name : names_given)
            if (names(red.failure, name) || names(red.state, name)) {
                found.push_back(red.number);
                break;
            }
    std::ranges::sort(found, std::greater{});
    return found;
}

}  // namespace

Known known_in(const Names& names_given, std::span<const Red> reds) {
    Known known{rows_naming(names_given.own, reds)};
    if (known.rows.empty()) known.package_rows = rows_naming(names_given.package, reds);
    return known;
}

std::string package_of(std::string_view manifest) {
    std::string space, name;
    bool in_package = false;
    for (const auto line : std::views::split(manifest, '\n')) {
        const std::string_view text = trim(std::string_view(line.begin(), line.end()));
        if (text.starts_with('[')) {
            in_package = text == "[package]";
            continue;
        }
        if (!in_package) continue;
        const auto equals = text.find('=');
        if (equals == std::string_view::npos) continue;
        const std::string_view key = trim(text.substr(0, equals));
        std::string_view value = trim(text.substr(equals + 1));
        if (value.size() < 2 || value.front() != '"') continue;
        value = value.substr(1, value.find('"', 1) - 1);
        if (key == "namespace") space = value;
        else if (key == "name") name = value;
    }
    if (name.empty()) return {};
    return space.empty() ? name : space + "." + name;
}

}  // namespace omui::devtools::ci
