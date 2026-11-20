module omui.devtools.eval;

import std;
import mcpplibs.os;
import nlohmann.json;
import mlibs.image;
import omui.devtools.run;

namespace omui::devtools::eval {

namespace {

namespace fs = std::filesystem;
namespace os = mcpplibs::os;
using nlohmann::json;
using nlohmann::ordered_json;

// What says the agent itself could not run (a quota, a login): unavailable, not a failure of the work.
constexpr std::array unavailable_markers{"usage limit", "rate limit", "Invalid API key", "Please run /login", "not logged in", "credit balance is too low"};

// A, B, 5 × 7 cells.
constexpr std::array<std::array<std::string_view, 7>, 2> glyphs{{
    {"01110", "10001", "10001", "11111", "10001", "10001", "10001"},
    {"11110", "10001", "10001", "11110", "10001", "10001", "11110"},
}};

mlibs::image::Image white(int width, int height) {
    mlibs::image::Image out;
    out.width = width;
    out.height = height;
    out.rgba.assign(static_cast<std::size_t>(width) * height * 4, 255);
    return out;
}

// `picture` laid over white at (x, y), clipped.
void paste(mlibs::image::Image& onto, const mlibs::image::Image& picture, int x, int y) {
    const bool premultiplied = picture.alpha == mlibs::image::Alpha::premultiplied;
    for (int row = 0; row < picture.height && y + row < onto.height; ++row) {
        const auto from = picture.row(row);
        auto to = onto.row(y + row);
        for (int column = 0; column < picture.width && x + column < onto.width; ++column) {
            const int alpha = from[column * 4 + 3];
            for (int channel = 0; channel < 3; ++channel) {
                const int value = from[column * 4 + channel];
                const int over = premultiplied ? value + 255 - alpha : value * alpha / 255 + 255 - alpha;
                to[(x + column) * 4 + channel] = static_cast<std::uint8_t>(std::clamp(over, 0, 255));
            }
            to[(x + column) * 4 + 3] = 255;
        }
    }
}

void letter(mlibs::image::Image& onto, char which, int x, int y, int scale = 4) {
    const auto& glyph = glyphs[which == 'A' ? 0 : 1];
    for (int line = 0; line < 7; ++line)
        for (int column = 0; column < 5; ++column) {
            if (glyph[line][column] != '1') continue;
            for (int dy = 0; dy < scale; ++dy)
                for (int dx = 0; dx < scale; ++dx) {
                    const int px = x + column * scale + dx, py = y + line * scale + dy;
                    if (px >= onto.width || py >= onto.height) continue;
                    auto pixel = onto.row(py).subspan(static_cast<std::size_t>(px) * 4, 4);
                    pixel[0] = pixel[1] = pixel[2] = 0;
                }
        }
}

std::expected<mlibs::image::Image, std::string> load(const fs::path& path) {
    auto loaded = mlibs::image::load_png(path);
    if (!loaded) return std::unexpected(std::format("{}: {}", os::fs::utf8(path), loaded.error()));
    return loaded;
}

std::string repo_path(const fs::path& path, const fs::path& root) { return os::fs::generic_utf8(fs::relative(path, root)); }

fs::path ledger_of(const fs::path& root) { return root / ".agents/docs/evaluation/ledger.eval.xdef.jsonl"; }

std::expected<json, std::string> comparison_set(const fs::path& root, std::string_view subject) {
    const fs::path path = root / ".agents/docs/evaluation/compare" / std::format("{}.compare.xdef.json", subject);
    const auto text = os::fs::read(path);
    if (!text) return std::unexpected(std::format("no comparison set {}", repo_path(path, root)));
    auto parsed = json::parse(*text, nullptr, false);
    if (parsed.is_discarded()) return std::unexpected(std::format("{} is not JSON", repo_path(path, root)));
    return parsed;
}

constexpr std::string_view judge_prompt =
    "You are judging two pictures of the same user-interface scene, side by side in one image: the left is labelled A, the right B. They "
    "come from two different products; you are not told which is which, and it must not matter.\n\nThe question: {}\n\nLook at the image "
    "file {}. Judge only what the picture shows, as an expert designer would against the best work in the field: craft, detail, clarity, "
    "consistency, polish. If the two are equally good, or differently good with no clear winner, say \"even\".\n\nAnswer with one JSON "
    "object and nothing else:\n{{\"winner\": \"A\" | \"B\" | \"even\", \"confidence\": \"low\" | \"medium\" | \"high\", \"reasons\": "
    "[\"…\", \"…\"]}}";

}  // namespace

std::string ledger_line(const ordered_json& record) {
    // json.dumps(…, ensure_ascii=False, separators=(", ", ": ")): what the ledgers hold.
    std::string out;
    const std::function<void(const ordered_json&)> write = [&](const ordered_json& value) {
        if (value.is_object()) {
            out += '{';
            bool first = true;
            for (const auto& item : value.items()) {
                if (!first) out += ", ";
                first = false;
                out += ordered_json(item.key()).dump(-1, ' ', false);
                out += ": ";
                write(item.value());
            }
            out += '}';
        } else if (value.is_array()) {
            out += '[';
            for (std::size_t index = 0; index < value.size(); ++index) {
                if (index) out += ", ";
                write(value[index]);
            }
            out += ']';
        } else {
            out += value.dump(-1, ' ', false);
        }
    };
    write(record);
    return out;
}

std::expected<void, std::string> append(const fs::path& ledger, const ordered_json& record) {
    std::ofstream out(ledger, std::ios::binary | std::ios::app);
    if (!out) return std::unexpected(std::format("{} cannot be written", os::fs::utf8(ledger)));
    out << ledger_line(record) << '\n';
    return {};
}

std::optional<json> first_object(std::string_view text) {
    // From each `{`, the shortest balanced span that parses (strings and escapes followed).
    for (std::size_t start = text.find('{'); start != std::string_view::npos; start = text.find('{', start + 1)) {
        int depth = 0;
        bool quoted = false, escaped = false;
        for (std::size_t at = start; at < text.size(); ++at) {
            const char here = text[at];
            if (quoted) {
                if (escaped) escaped = false;
                else if (here == '\\') escaped = true;
                else if (here == '"') quoted = false;
                continue;
            }
            if (here == '"') quoted = true;
            else if (here == '{') ++depth;
            else if (here == '}' && --depth == 0) {
                auto parsed = json::parse(text.substr(start, at - start + 1), nullptr, false);
                if (!parsed.is_discarded() && parsed.is_object()) return parsed;
                break;
            }
        }
    }
    return std::nullopt;
}

std::expected<Reply, std::string> ask(const Session& session, std::string_view prompt) {
    std::vector<std::string> args;
    std::string program = session.agent;
    const fs::path answer = session.directory / "answer.md";
    if (session.agent == "claude") {
        args = {"-p", std::string(prompt), "--output-format", "stream-json", "--verbose"};
        if (!session.tools.empty()) {
            args.push_back("--allowedTools");
            args.insert(args.end(), session.tools.begin(), session.tools.end());
        }
    } else if (session.agent == "codex") {
        args = {"exec", "--skip-git-repo-check", "-C", os::fs::utf8(session.directory), "-c", "approval_policy=\"never\"", "-o", os::fs::utf8(answer)};
    } else {
        return std::unexpected(std::format("agent {}", session.agent));
    }
    args.insert(args.end(), session.extra.begin(), session.extra.end());
    if (session.agent == "codex") args.emplace_back(prompt);
    const auto done = run::program(session.directory, program, std::move(args), {.cwd = session.directory, .merged = true, .timeout = session.timeout});
    if (!done) return std::unexpected(done.error());
    Reply reply;
    if (done->timed_out) return std::unexpected(std::format("{} did not finish in {} s", session.agent, session.timeout.count()));
    for (const char* marker : unavailable_markers)
        if (done->out.find(marker) != std::string::npos && done->status != 0) return std::unexpected(std::string(marker));
    if (session.agent == "codex") {
        if (done->status != 0) return std::unexpected(done->out.substr(0, 300));
        reply.text = os::fs::read(answer).value_or("");
        return reply;
    }
    // claude's stream: assistant events (tool calls, text), then a result with the usage.
    for (const auto part : std::views::split(std::string_view(done->out), '\n')) {
        const auto event = json::parse(std::string_view(part.begin(), part.end()), nullptr, false);
        if (event.is_discarded() || !event.is_object()) continue;
        const std::string type = event.value("type", "");
        if (type == "assistant" && event.contains("message"))
            for (const auto& content : event["message"].value("content", json::array())) {
                if (content.value("type", "") == "tool_use") reply.tools.push_back(content.value("name", ""));
                else if (content.value("type", "") == "text") reply.text = content.value("text", "");
            }
        if (type == "result") reply.raw = event;
    }
    if (reply.raw.is_null()) return std::unexpected(done->out.substr(0, 300));
    if (reply.raw.contains("result") && reply.raw["result"].is_string()) reply.text = reply.raw["result"].get<std::string>();
    reply.usage = reply.raw.value("usage", json::object());
    reply.turns = reply.raw.value("num_turns", 0);
    return reply;
}

Tokens tokens_of(const json& usage) {
    Tokens out;
    out.input = usage.value("input_tokens", std::int64_t{0});
    out.cache_write = usage.value("cache_creation_input_tokens", std::int64_t{0});
    out.cache_read = usage.value("cache_read_input_tokens", std::int64_t{0});
    out.output = usage.value("output_tokens", std::int64_t{0});
    return out;
}

// ---- compare

std::string unblind(std::string_view winner, std::string_view ours) {
    if (winner == "even") return "par";
    return winner == ours ? "ahead" : "behind";
}

std::string majority(std::span<const std::string> votes) {
    std::array<int, verdicts.size()> tally{};
    for (const auto& vote : votes)
        for (std::size_t index = 0; index < verdicts.size(); ++index) tally[index] += vote == verdicts[index];
    const int top = *std::ranges::max_element(tally);
    std::vector<std::size_t> winners;
    for (std::size_t index = 0; index < verdicts.size(); ++index)
        if (tally[index] == top) winners.push_back(index);
    return winners.size() == 1 ? std::string(verdicts[winners.front()]) : std::string("par");
}

std::string weakest(std::span<const std::string> pair_verdicts) {
    std::size_t lowest = verdicts.size() - 1;
    for (const auto& verdict : pair_verdicts)
        for (std::size_t index = 0; index < verdicts.size(); ++index)
            if (verdict == verdicts[index]) lowest = std::min(lowest, index);
    return std::string(verdicts[lowest]);
}

mlibs::image::Image row(std::span<const mlibs::image::Image> pictures, int gap) {
    int width = 0, height = 0;
    for (const auto& picture : pictures) {
        width += picture.width;
        height = std::max(height, picture.height);
    }
    width += gap * std::max(0, static_cast<int>(pictures.size()) - 1);
    auto out = white(width, height);
    int x = 0;
    for (const auto& picture : pictures) {
        paste(out, picture, x, 0);
        x += picture.width + gap;
    }
    return out;
}

mlibs::image::Image sheet(const mlibs::image::Image& left, const mlibs::image::Image& right) {
    constexpr int band = 44, gap = 24;
    auto out = white(left.width + right.width + gap, std::max(left.height, right.height) + band);
    paste(out, left, 0, band);
    letter(out, 'A', 8, 8);
    paste(out, right, left.width + gap, band);
    letter(out, 'B', left.width + gap + 8, 8);
    return out;
}

int compare_sheets(const fs::path& root, std::string_view subject) {
    const auto set = comparison_set(root, subject);
    if (!set) {
        std::cerr << "omui-devtools eval compare: " << set.error() << '\n';
        return 2;
    }
    // Which side is ours, from a stream seeded by the subject and the set's seed: the same sheets each time.
    std::seed_seq seed_of(subject.begin(), subject.end());
    std::vector<std::uint32_t> words(1);
    seed_of.generate(words.begin(), words.end());
    std::mt19937_64 stream(words.front() ^ static_cast<std::uint64_t>(set->value("seed", 1)));
    const fs::path out = root / "target/compare" / subject;
    std::error_code error;
    fs::remove_all(out, error);
    fs::create_directories(out, error);
    ordered_json key = ordered_json::array();
    for (const auto& dimension : set->value("dimension", json::array())) {
        const auto& pairs = dimension.value("pair", json::array());
        for (std::size_t index = 0; index < pairs.size(); ++index) {
            const auto& pair = pairs[index];
            const fs::path ours = root / pair.value("ours", ""), theirs = root / pair.value("theirs", "");
            for (const auto& side : {ours, theirs})
                if (!fs::exists(side)) {
                    std::cerr << std::format("omui-devtools eval compare: {} does not exist (make the material first: {})\n", repo_path(side, root),
                                             set->value("material", std::string("see the set")));
                    return 1;
                }
            const bool ours_left = (stream() >> 11) * 0x1.0p-53 < 0.5;
            auto left = load(ours_left ? ours : theirs);
            auto right = load(ours_left ? theirs : ours);
            if (!left || !right) {
                std::cerr << "omui-devtools eval compare: " << (left ? right.error() : left.error()) << '\n';
                return 1;
            }
            const auto picture = sheet(*left, *right);
            const fs::path path = out / std::format("{}-{}.png", dimension.value("id", ""), index);
            if (auto saved = mlibs::image::save_png(path, mlibs::image::view(picture)); !saved) {
                std::cerr << "omui-devtools eval compare: " << saved.error() << '\n';
                return 1;
            }
            key.push_back(ordered_json{{"dimension", dimension.value("id", "")}, {"pair", index}, {"sheet", repo_path(path, root)}, {"ours", ours_left ? "A" : "B"},
                           {"competitor", pair.value("competitor", "")}, {"question", pair.value("question", dimension.value("question", ""))}});
        }
    }
    std::ofstream(out / "key.json", std::ios::binary) << key.dump(1) << '\n';
    std::cout << std::format("omui-devtools eval compare: {} sheets in {}\n", key.size(), repo_path(out, root));
    return 0;
}

int compare_judge(const fs::path& root, std::string_view subject, const Judging& judging) {
    const fs::path key_file = root / "target/compare" / subject / "key.json";
    const auto key_text = os::fs::read(key_file);
    const auto key = key_text ? json::parse(*key_text, nullptr, false) : json();
    if (!key.is_array()) {
        std::cerr << std::format("omui-devtools eval compare: no sheets for {} (omui-devtools eval compare sheets {})\n", subject, subject);
        return 2;
    }
    std::map<std::string, std::vector<ordered_json>> by_dimension;
    std::vector<std::string> order;
    for (const auto& entry : key) {
        // Each sheet judged `samples` times by fresh sessions; its verdict the majority (a tie: par).
        std::vector<json> answers;
        for (int sample = 0; sample < judging.samples; ++sample) {
            const fs::path empty = fs::temp_directory_path() / std::format("omui-compare-{}", std::random_device{}());
            std::error_code error;
            fs::create_directories(empty, error);
            const fs::path local = empty / "sheet.png";
            fs::copy_file(root / entry.value("sheet", ""), local, error);
            auto reply = ask({.agent = judging.agent, .directory = empty, .tools = {"Read"}}, std::format(judge_prompt, entry.value("question", ""), os::fs::utf8(local)));
            fs::remove_all(empty, error);
            if (!reply) continue;
            auto answer = first_object(reply->text);
            if (!answer) continue;
            const std::string winner = answer->value("winner", "");
            if (winner == "A" || winner == "B" || winner == "even") answers.push_back(std::move(*answer));
        }
        const std::string dimension = entry.value("dimension", "");
        if (answers.empty()) {
            std::cout << std::format("{}#{}: unavailable\n", dimension, entry.value("pair", 0));
            continue;
        }
        std::vector<std::string> votes;
        for (const auto& answer : answers) votes.push_back(unblind(answer.value("winner", ""), entry.value("ours", "")));
        const std::string verdict = majority(votes);
        const auto chosen = std::ranges::find(votes, verdict);
        const json& answer = chosen == votes.end() ? answers.front() : answers[static_cast<std::size_t>(chosen - votes.begin())];
        ordered_json reasons = ordered_json::array();
        for (const auto& reason : answer.value("reasons", json::array()))
            if (reasons.size() < 4) reasons.push_back(reason.is_string() ? reason.get<std::string>() : reason.dump());
        if (!by_dimension.contains(dimension)) order.push_back(dimension);
        by_dimension[dimension].push_back({{"competitor", entry.value("competitor", "")}, {"verdict", verdict}, {"confidence", answer.value("confidence", std::string{})},
                                           {"votes", votes}, {"reasons", reasons}, {"sheet", entry.value("sheet", "")}});
        std::string said;
        for (const auto& vote : votes) said += (said.empty() ? "" : ", ") + vote;
        std::cout << std::format("{}#{} vs {}: {} (votes {})\n", dimension, entry.value("pair", 0), entry.value("competitor", ""), verdict, said);
    }
    for (const auto& dimension : order) {
        // A dimension is as good as its weakest pair: "every dimension at least par" means every control (Part 2 plan §2.4).
        std::vector<std::string> pair_verdicts;
        for (const auto& pair : by_dimension[dimension]) pair_verdicts.push_back(pair["verdict"].get<std::string>());
        const std::string verdict = weakest(pair_verdicts);
        ordered_json record{{"date", run::today()},   {"subject", subject},    {"evaluator", judging.agent + "-blind"}, {"rubric", "compare-v1"},
                            {"commit", run::commit(root)}, {"dimension", dimension}, {"verdict", verdict},                {"rule", "weakest pair"},
                            {"pairs", by_dimension[dimension]}};
        std::cout << std::format("{}/{}: {}\n", subject, dimension, verdict);
        if (!judging.dry_run)
            if (auto written = append(ledger_of(root), record); !written) {
                std::cerr << "omui-devtools eval compare: " << written.error() << '\n';
                return 1;
            }
    }
    return 0;
}

int compare_record(const fs::path& root, std::string_view subject, std::string_view dimension, std::string_view verdict, std::string_view by,
                   std::string_view note) {
    if (!std::ranges::contains(verdicts, verdict)) {
        std::cerr << "omui-devtools eval compare: a verdict is behind, par or ahead\n";
        return 2;
    }
    const ordered_json record{{"date", run::today()},      {"subject", subject}, {"evaluator", by},  {"rubric", "compare-v1"},
                              {"commit", run::commit(root)}, {"dimension", dimension}, {"verdict", verdict}, {"note", note}};
    if (auto written = append(ledger_of(root), record); !written) {
        std::cerr << "omui-devtools eval compare: " << written.error() << '\n';
        return 1;
    }
    std::cout << ledger_line(record) << '\n';
    return 0;
}

}  // namespace omui::devtools::eval
