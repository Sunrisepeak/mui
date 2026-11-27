// The rule over the user guides (plan §6.3, RC3.5): docs-code. Every fenced block of docs/{en,zh}/guide/*.md
// is an excerpt of a file a workspace member holds — CI builds and runs it — or a ```sh block of commands; and
// the two languages of a guide have the same headings, level for level.
//
// An excerpt is marked by the line right before its block:
//   <!-- code: examples/03-counter/src/tally.cpp#view -->   the region `view` of that file
//   <!-- code: modules/…/toast.animation.xdef.json -->      the whole file (data, which has no comments)
// and the region is delimited in the file by the lines `// docs: view` and `// docs: end`. The block is the
// region's text with its common indentation removed.
module omui.devtools.lint;

import std;
import :tree;

namespace omui::devtools::lint {

namespace fs = std::filesystem;

namespace {

constexpr std::string_view marker_open = "<!-- code:";
constexpr std::string_view region_open = "// docs:";

std::vector<std::string> lines_in(std::string_view text) {
    std::vector<std::string> out;
    std::size_t at = 0;
    while (at < text.size()) {
        auto end = text.find('\n', at);
        if (end == std::string_view::npos) end = text.size();
        std::string line(text.substr(at, end - at));
        if (line.ends_with('\r')) line.pop_back();
        out.push_back(std::move(line));
        at = end + 1;
    }
    return out;
}

std::string_view stripped(std::string_view line) {
    const auto first = line.find_first_not_of(" \t");
    if (first == std::string_view::npos) return {};
    const auto last = line.find_last_not_of(" \t");
    return line.substr(first, last - first + 1);
}

// The lines with their common leading spaces removed (blank lines do not count).
std::vector<std::string> dedented(std::vector<std::string> lines) {
    std::size_t common = std::string::npos;
    for (const auto& line : lines)
        if (!stripped(line).empty()) common = std::min(common, line.find_first_not_of(' '));
    if (common == std::string::npos) common = 0;
    for (auto& line : lines) line = line.size() >= common ? line.substr(common) : std::string{};
    return lines;
}

// The region `name` of a file's lines, or why there is none.
std::expected<std::vector<std::string>, std::string> region_of(const std::vector<std::string>& lines, std::string_view name, std::string_view file) {
    const std::string open = std::format("{} {}", region_open, name);
    const std::string close = std::format("{} end", region_open);
    std::optional<std::size_t> start;
    std::size_t count = 0;
    for (std::size_t index = 0; index < lines.size(); ++index)
        if (stripped(lines[index]) == open) {
            ++count;
            if (!start) start = index;
        }
    if (!start)
        return std::unexpected(std::format("`{}` has no region `{}`: mark it in the file with a line `{}` before it and `{}` after it", file, name, open, close));
    if (count > 1) return std::unexpected(std::format("`{}` opens the region `{}` {} times: a region is named once", file, name, count));
    std::vector<std::string> out;
    for (std::size_t index = *start + 1; index < lines.size(); ++index) {
        const std::string_view line = stripped(lines[index]);
        if (line == close) return dedented(std::move(out));
        if (line.starts_with(region_open)) continue;  // another region's marker inside this one
        out.push_back(lines[index]);
    }
    return std::unexpected(std::format("`{}`: the region `{}` is never closed: end it with a line `{}`", file, name, close));
}

struct Heading {
    std::size_t level, line;
};

struct Guide {
    std::vector<Heading> headings;
};

// One guide's blocks checked against the files they name; its headings, outside the blocks, returned.
Guide check_guide(const Workspace& workspace, const std::string& rel, std::string_view text, std::vector<Problem>& problems) {
    Guide guide;
    const auto lines = lines_in(text);
    const auto add = [&](std::size_t line, std::string message) {
        problems.push_back({"docs-code", std::format("{}:{}", rel, line), std::move(message)});
    };
    const auto in_member = [&](std::string_view file) {
        return std::ranges::any_of(workspace.members, [&](const Member& member) {
            return !member.text.empty() && file.starts_with(member.path + "/");
        });
    };
    std::optional<std::size_t> pending;  // the line of a marker not yet followed by its block
    for (std::size_t index = 0; index < lines.size(); ++index) {
        const std::string_view line = lines[index];
        const std::string_view trimmed = stripped(line);
        if (trimmed.empty()) continue;
        if (trimmed.starts_with(marker_open)) {
            if (pending) add(*pending + 1, "a code marker with no fenced block right after it: put it on the line before the block's ```");
            pending = index;
            continue;
        }
        if (!line.starts_with("```")) {
            if (pending) add(*pending + 1, "a code marker with no fenced block right after it: put it on the line before the block's ```");
            pending.reset();
            const auto hashes = line.find_first_not_of('#');
            if (hashes != 0 && hashes != std::string_view::npos && line[hashes] == ' ') guide.headings.push_back({hashes, index + 1});
            continue;
        }
        // A fenced block: its language, its text, and where it ends.
        const std::string language(stripped(line.substr(3)));
        const std::size_t opened = index;
        std::vector<std::string> block;
        for (++index; index < lines.size() && !stripped(lines[index]).starts_with("```"); ++index) block.push_back(lines[index]);
        if (index >= lines.size()) add(opened + 1, "a fenced block that is never closed: end it with ```");
        const auto marker = pending;
        pending.reset();
        if (!marker) {
            if (language != "sh")
                add(opened + 1, std::format("a ```{} block that no file CI runs holds: put `<!-- code: <file>#<region> -->` on the line before it "
                                            "(the region between `// docs: <region>` and `// docs: end` in a member's source or test), or make it "
                                            "a ```sh block of commands",
                                            language.empty() ? std::string("(plain)") : language));
            continue;
        }
        std::string_view named = stripped(lines[*marker]).substr(marker_open.size());
        if (!named.ends_with("-->")) {
            add(*marker + 1, "a code marker is `<!-- code: <file>#<region> -->` on one line");
            continue;
        }
        named = stripped(named.substr(0, named.size() - 3));
        const auto hash = named.find('#');
        const std::string file(named.substr(0, hash));
        const std::string region = hash == std::string_view::npos ? std::string{} : std::string(named.substr(hash + 1));
        if (file.contains("..") || file.starts_with('/') || file.contains("/target/")) {
            add(*marker + 1, std::format("`{}`: name the file from the repository's root", file));
            continue;
        }
        const auto source = read_text(workspace.root / path_of(file));
        if (!source) {
            add(*marker + 1, std::format("`{}` does not exist: name a file from the repository's root", file));
            continue;
        }
        if (!in_member(file)) {
            add(*marker + 1, std::format("`{}` is in no workspace member, so CI neither builds nor runs it: take the excerpt from a member's source, test or data", file));
            continue;
        }
        auto expected = region.empty() ? std::expected<std::vector<std::string>, std::string>(lines_in(*source)) : region_of(lines_in(*source), region, file);
        if (!expected) {
            add(*marker + 1, expected.error());
            continue;
        }
        const std::string what = region.empty() ? file : std::format("{}#{}", file, region);
        if (*expected == block) continue;
        std::size_t differs = 0;
        while (differs < std::min(expected->size(), block.size()) && (*expected)[differs] == block[differs]) ++differs;
        const std::string theirs = differs < expected->size() ? (*expected)[differs] : std::string("(the region ends)");
        add(opened + 2 + differs, std::format("the block is not `{}` as it is now (its line {} is `{}` there): copy the region again, its common indentation removed",
                                              what, differs + 1, theirs));
    }
    if (pending) add(*pending + 1, "a code marker with no fenced block right after it: put it on the line before the block's ```");
    return guide;
}

std::string levels_of(const Guide& guide) {
    std::string out;
    for (const auto& heading : guide.headings) out += std::format("{}{}", out.empty() ? "" : " ", std::string(heading.level, '#'));
    return out;
}

}  // namespace

std::vector<Problem> check_docs(const Workspace& workspace) {
    std::vector<Problem> problems;
    std::map<std::string, std::map<std::string, Guide>> guides;  // a guide's file name → language → its headings
    for (const std::string language : {"en", "zh"}) {
        const fs::path directory = workspace.root / "docs" / language / "guide";
        std::error_code error;
        if (!fs::is_directory(directory, error)) continue;
        std::vector<std::string> names;
        for (const auto& entry : fs::directory_iterator(directory, error))
            if (entry.is_regular_file(error) && entry.path().extension() == ".md") names.push_back(utf8_of(entry.path().filename()));
        std::ranges::sort(names);
        for (const auto& name : names) {
            const std::string rel = std::format("docs/{}/guide/{}", language, name);
            guides[name][language] = check_guide(workspace, rel, read_text(directory / path_of(name)).value_or(""), problems);
        }
    }
    for (const auto& [name, languages] : guides) {
        const auto english = languages.find("en"), chinese = languages.find("zh");
        if (english == languages.end() || chinese == languages.end()) {
            const std::string have = english == languages.end() ? "zh" : "en", lacks = english == languages.end() ? "en" : "zh";
            problems.push_back({"docs-code", std::format("docs/{}/guide/{}", have, name),
                                std::format("no docs/{}/guide/{} beside it: write the other language, heading for heading", lacks, name)});
            continue;
        }
        const std::string ours = levels_of(english->second), theirs = levels_of(chinese->second);
        if (ours == theirs) continue;
        std::size_t index = 0;
        const auto& left = english->second.headings;
        const auto& right = chinese->second.headings;
        while (index < std::min(left.size(), right.size()) && left[index].level == right[index].level) ++index;
        const std::size_t line = index < right.size() ? right[index].line : (right.empty() ? 1 : right.back().line);
        problems.push_back({"docs-code", std::format("docs/zh/guide/{}:{}", name, line),
                            std::format("its headings ({}) are not docs/en/guide/{}'s ({}): the two languages keep the same sections, level for level "
                                        "(they part at heading {})",
                                        theirs.empty() ? "none" : theirs, name, ours.empty() ? "none" : ours, index + 1)});
    }
    return problems;
}

}  // namespace omui::devtools::lint
