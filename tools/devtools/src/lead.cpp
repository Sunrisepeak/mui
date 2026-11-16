module omui.devtools.lead;

import std;
import mcpplibs.os;
import omui.devtools.run;
import nlohmann.json;

namespace omui::devtools::lead {

namespace {

using json = nlohmann::ordered_json;
namespace fs = std::filesystem;

constexpr std::array statuses{"planned", "evidence", "claimed"};
constexpr std::array lines{"use-safely", "build-reliably", "together", "craft", "foundation"};
constexpr std::array projects{"omui", "omui.components", "hanim", "xrender", "xengine", "gongfu", "spec"};

struct Words {
    std::string_view title, intro, metrics, features, capabilities, head_metric, head_feature, head_capability, planned, pending;
};

constexpr std::pair<std::string_view, Words> words[]{
    {"en",
     {"Lead: measured against the best",
      "Every metric is ours against the strongest competitor, measured on one machine in the same job, the sides taking turns; the verdict is from the "
      "ratio's 95 % interval: behind below 0.95×, par to 1.2×, ahead from 1.2×, far ahead from 10× or where the competitor cannot do it. What is behind "
      "stays here; a device we do not have is said so. Generated from the lead ledger ({}).",
      "Metrics", "Signature features", "Capabilities against the field", "| Project | Metric | Ours | Strongest | Ratio [95 %] | Verdict |",
      "| Project | Feature | Status | Verdict |", "| Capability | Competitors | Verdict |", "planned", "needs a device"}},
    {"zh",
     {"领先度：和最强的比",
      "每个指标都是我们对最强竞品，在同一台机器、同一个作业里交替测量；判定看比值的 95% 区间：低于 0.95× 为落后，到 1.2× 为持平，1.2× 起为领先，10× "
      "起或竞品做不到为远超。落后的照样列在这里；没有设备测的如实写明。由领先度台账生成（{}）。",
      "指标", "标志性特性", "能力对照", "| 项目 | 指标 | 我方 | 最强竞品 | 比值 [95% 区间] | 判定 |", "| 项目 | 特性 | 状态 | 判定 |",
      "| 能力 | 竞品 | 判定 |", "计划中", "待真机验证"}},
};

std::string text_at(const json& value, std::string_view key) {
    const auto found = value.find(key);
    return found != value.end() && found->is_string() ? found->get<std::string>() : std::string{};
}

// Python's format(x, "g"): six significant digits, the shortest of fixed and scientific.
std::string general(const json& number) { return std::format("{:g}", number.is_number() ? number.get<double>() : 0.0); }

std::expected<std::vector<json>, std::string> read_lines(std::string_view text) {
    std::vector<json> out;
    for (auto part : std::views::split(text, '\n')) {
        const std::string line(part.begin(), part.end());
        if (line.find_first_not_of(" \t\r") == std::string::npos) continue;
        auto value = json::parse(line, nullptr, false);
        if (value.is_discarded()) return std::unexpected("a ledger line is not JSON");
        out.push_back(std::move(value));
    }
    return out;
}

// Python's round(x, places) closely enough for a ledger: the nearest at that many places.
double rounded(double value, int places) {
    const double scale = std::pow(10.0, places);
    return std::round(value * scale) / scale;
}

// A uniform index below `count` from the generator, without the modulo's bias.
std::size_t index_below(std::mt19937_64& stream, std::size_t count) {
    const std::uint64_t limit = std::numeric_limits<std::uint64_t>::max() - std::numeric_limits<std::uint64_t>::max() % count;
    std::uint64_t drawn = stream();
    while (drawn >= limit) drawn = stream();
    return static_cast<std::size_t>(drawn % count);
}

// round → value for one series' samples of `key` that carry `field`.
std::map<int, double> series_values(const std::vector<json>& samples, std::string_view series, std::string_view key, std::string_view field) {
    std::map<int, double> out;
    for (const auto& sample : samples)
        if (text_at(sample, "series") == series && text_at(sample, "key") == key && sample.contains(field) && sample[field].is_number())
            out[sample.value("round", 0)] = sample[field].get<double>();
    return out;
}

// One ratio a round, larger meaning better for us.
std::vector<double> ratios(const std::map<int, double>& ours, const std::map<int, double>& theirs, std::string_view better) {
    std::vector<double> out;
    for (const auto& [round, our] : ours) {
        const auto found = theirs.find(round);
        if (found == theirs.end()) continue;
        const double their = found->second;
        if (better == "lower" ? our > 0 : their > 0) out.push_back(better == "lower" ? their / our : our / their);
    }
    return out;
}

std::optional<json> judge(const json& metric, const std::vector<json>& samples, const Context& context) {
    const std::string key = text_at(metric, "key"), field = text_at(metric, "field"), better = text_at(metric, "better");
    const auto ours = series_values(samples, text_at(metric, "ours"), key, field);
    if (ours.empty()) return std::nullopt;
    struct Candidate {
        std::string name;
        double median, ratio;
        std::pair<double, double> ci;
        std::size_t rounds;
    };
    std::vector<Candidate> candidates;
    for (const auto& competitor : metric.value("competitors", json::array())) {
        const std::string name = competitor.get<std::string>();
        const auto theirs = series_values(samples, name, key, field);
        const auto each = ratios(ours, theirs, better);
        if (each.empty()) continue;
        std::vector<double> paired;
        for (const auto& [round, value] : theirs)
            if (ours.contains(round)) paired.push_back(value);
        const auto [low, high] = interval(each);
        candidates.push_back({name, median(paired), median(each), {rounded(low, 4), rounded(high, 4)}, paired.size()});
    }
    if (candidates.empty()) return std::nullopt;
    const auto best = std::ranges::min_element(candidates, {}, &Candidate::ratio);
    std::vector<double> our_values;
    for (const auto& [_, value] : ours) our_values.push_back(value);
    json others = json::array();
    for (auto at = candidates.begin(); at != candidates.end(); ++at)
        if (at != best) others.push_back({{"name", at->name}, {"median", rounded(at->median, 5)}, {"ratio", rounded(at->ratio, 4)}});
    return json{{"kind", "metric"},
                {"date", context.date},
                {"project", text_at(metric, "project")},
                {"metric", text_at(metric, "id")},
                {"better", better},
                {"core", metric.value("core", true)},
                {"ours", {{"name", text_at(metric, "ours")}, {"median", rounded(median(our_values), 5)}}},
                {"best", {{"name", best->name}, {"median", rounded(best->median, 5)}}},
                {"others", others},
                {"ratio", rounded(best->ratio, 4)},
                {"ci", {best->ci.first, best->ci.second}},
                {"rounds", best->rounds},
                {"verdict", tier(best->ci.first)},
                {"machine", context.machine},
                {"commit", context.commit},
                {"found_by", nullptr}};
}

}  // namespace

std::string_view tier(double ratio) {
    if (ratio >= far) return "far_ahead";
    if (ratio >= ahead) return "ahead";
    if (ratio >= behind) return "par";
    return "behind";
}

double median(std::vector<double> values) {
    if (values.empty()) return 0.0;
    std::ranges::sort(values);
    const std::size_t middle = values.size() / 2;
    return values.size() % 2 ? values[middle] : (values[middle - 1] + values[middle]) / 2.0;
}

std::pair<double, double> interval(std::span<const double> values, std::uint64_t seed, int resamples) {
    if (values.empty()) return {0.0, 0.0};
    if (values.size() < 2) return {values.front(), values.front()};
    std::mt19937_64 stream(seed);
    std::vector<double> medians, drawn(values.size());
    medians.reserve(static_cast<std::size_t>(resamples));
    for (int resample = 0; resample < resamples; ++resample) {
        for (auto& value : drawn) value = values[index_below(stream, values.size())];
        medians.push_back(median(drawn));
    }
    std::ranges::sort(medians);
    return {medians[static_cast<std::size_t>(0.025 * resamples)], medians[static_cast<std::size_t>(0.975 * resamples) - 1]};
}

std::vector<std::string> samples(std::string_view output, std::string_view series, std::string_view series_field, std::string_view key, int round) {
    std::vector<std::string> out;
    for (auto part : std::views::split(output, '\n')) {
        std::string_view text(part.begin(), part.end());
        const auto start = text.find_first_not_of(" \t\r");
        if (start == std::string_view::npos || text[start] != '{') continue;
        const auto line = json::parse(text.substr(start), nullptr, false);
        if (line.is_discarded() || !line.is_object()) continue;
        // (series, sample) pairs.
        std::vector<std::pair<std::string, json>> pairs;
        if (!series_field.empty() && line.contains(series_field)) {
            const auto& named = line[series_field];
            pairs.emplace_back(named.is_string() ? named.get<std::string>() : named.dump(), line);
        } else {
            // A vector comparison prints one line a renderer and one comparing the pictures; the latter becomes
            // an error sample for each renderer against the reference.
            if (const std::string renderer = text_at(line, "renderer"); !renderer.empty()) pairs.emplace_back(renderer.substr(0, renderer.find(' ')), line);
            for (const auto& item : line.items()) {
                const std::string& name = item.key();
                const auto& value = item.value();
                const auto against = name.find(" against ");
                if (!value.is_object() || against == std::string::npos || !value.contains("mean_difference")) continue;
                const std::string subject = name.substr(0, against), reference = name.substr(against + 9);
                if (subject.starts_with("its SVG")) continue;
                json error = json::object();
                error["scene"] = line.contains("scene") ? line["scene"] : json(nullptr);
                error["error_vs_" + reference] = value["mean_difference"];
                pairs.emplace_back(subject, std::move(error));
            }
            if (pairs.empty()) pairs.emplace_back(std::string(series), line);
        }
        for (const auto& [name, sample] : pairs) {
            const std::string scene = text_at(sample, "scene");
            json record{{"series", name}, {"round", round}, {"key", scene.empty() ? std::string(key) : scene}};
            for (const auto& item : sample.items())
                if (item.value().is_number()) record[item.key()] = item.value();
            out.push_back(record.dump(-1, ' ', false, json::error_handler_t::replace));
        }
    }
    return out;
}

std::expected<std::vector<Verdict>, std::string> verdicts(std::string_view metrics_text, std::string_view samples_text, std::string_view ledger_text,
                                                          const Context& context, std::vector<std::string>& skipped) {
    const auto metrics = json::parse(metrics_text, nullptr, false);
    if (metrics.is_discarded() || !metrics.contains("metric")) return std::unexpected("the metric list is not JSON with its list");
    const auto samples_read = read_lines(samples_text);
    if (!samples_read) return std::unexpected("a sample line is not JSON");
    const auto ledger = read_lines(ledger_text);
    if (!ledger) return std::unexpected(ledger.error());
    std::map<std::string, std::string> previous;  // each metric's newest verdict
    for (const auto& record : *ledger)
        if (text_at(record, "kind") == "metric") previous[text_at(record, "metric")] = text_at(record, "verdict");
    std::vector<Verdict> out;
    for (const auto& metric : metrics["metric"]) {
        const std::string id = text_at(metric, "id");
        const std::string status = metric.contains("status") ? text_at(metric, "status") : "measured";
        if (status == "unverified") {
            if (previous[id] == "unverified") continue;
            const json record{{"kind", "metric"},          {"date", context.date},          {"project", text_at(metric, "project")},
                              {"metric", id},              {"better", text_at(metric, "better")}, {"core", metric.value("core", true)},
                              {"verdict", "unverified"},   {"needs", text_at(metric, "needs")}, {"commit", context.commit}};
            out.push_back({record.dump(), std::format("{:<44} unverified ({})", id, text_at(metric, "needs"))});
            continue;
        }
        if (status != "measured") continue;
        const auto record = judge(metric, *samples_read, context);
        if (!record) {
            skipped.push_back(id);
            continue;
        }
        out.push_back({record->dump(), std::format("{:<44} {:>8.3f}× [{:.3f}, {:.3f}] against {:<12} {}", id, (*record)["ratio"].get<double>(),
                                                   (*record)["ci"][0].get<double>(), (*record)["ci"][1].get<double>(), text_at((*record)["best"], "name"),
                                                   text_at(*record, "verdict"))});
    }
    return out;
}

std::string_view capability_verdict(std::span<const std::string> support) {
    if (std::ranges::contains(support, "full")) return "par";
    if (std::ranges::all_of(support, [](const std::string& one) { return one == "none"; })) return "far_ahead";
    return "ahead";
}

std::expected<std::map<std::string, std::string>, std::string> pages(std::string_view metrics_text, std::string_view features_text,
                                                                      std::string_view ledger_text) {
    const auto metrics = json::parse(metrics_text, nullptr, false);
    const auto features = json::parse(features_text, nullptr, false);
    const auto ledger = read_lines(ledger_text);
    if (metrics.is_discarded() || features.is_discarded() || !metrics.contains("metric") || !features.contains("feature"))
        return std::unexpected("the metric or feature list is not JSON with its list");
    if (!ledger) return std::unexpected(ledger.error());
    std::map<std::string, const json*> measured;  // each metric's newest record
    std::string newest;
    std::map<std::string, std::set<std::string>> verdicts;
    for (const auto& record : *ledger) {
        newest = std::max(newest, text_at(record, "date"));
        const std::string kind = text_at(record, "kind");
        if (kind == "metric") measured[text_at(record, "metric")] = &record;
        if (kind == "capability" || kind == "task") verdicts[text_at(record, "feature")].insert(text_at(record, "verdict"));
    }
    std::map<std::string, std::string> out;
    for (const auto& [language, w] : words) {
        std::vector<std::string> page{std::format("# {}", w.title), "", std::vformat(w.intro, std::make_format_args(newest)), "",
                                      std::format("## {}", w.metrics), "", std::string(w.head_metric), "|---|---|---|---|---|---|"};
        for (const auto& metric : metrics["metric"]) {
            const std::string id = text_at(metric, "id"), project = text_at(metric, "project");
            const std::string status = metric.contains("status") ? text_at(metric, "status") : std::string{};
            const auto found = measured.find(id);
            if (found != measured.end() && text_at(*found->second, "verdict") != "unverified") {
                const json& record = *found->second;
                page.push_back(std::format("| {} | `{}` | {} {} | {} {} | {:.2f}× [{:.2f}, {:.2f}] | **{}** |", project, id, text_at(record["ours"], "name"),
                                           general(record["ours"]["median"]), text_at(record["best"], "name"), general(record["best"]["median"]),
                                           record["ratio"].get<double>(), record["ci"][0].get<double>(), record["ci"][1].get<double>(), text_at(record, "verdict")));
            } else if (status == "measured") {
                continue;
            } else {
                std::string competitors;
                for (const auto& name : metric.value("competitors", json::array())) competitors += (competitors.empty() ? "" : ", ") + name.get<std::string>();
                page.push_back(std::format("| {} | `{}` | — | {} | — | {} |", project, id, competitors, status == "unverified" ? w.pending : w.planned));
            }
        }
        page.insert(page.end(), {"", std::format("## {}", w.features), "", std::string(w.head_feature), "|---|---|---|---|"});
        for (const auto& feature : features["feature"]) {
            if (text_at(feature, "line") == "foundation") continue;
            std::string said;
            for (const auto& verdict : verdicts[text_at(feature, "id")]) said += (said.empty() ? "" : ", ") + verdict;
            page.push_back(std::format("| {} | {}: {} | {} | {} |", text_at(feature, "project"), text_at(feature, "id"), text_at(feature, "title"),
                                       text_at(feature, "status"), said.empty() ? "—" : said));
        }
        page.insert(page.end(), {"", std::format("## {}", w.capabilities), "", std::string(w.head_capability), "|---|---|---|"});
        for (const auto& record : *ledger) {
            if (text_at(record, "kind") != "capability") continue;
            std::string against;
            for (const auto& competitor : record.value("competitors", json::array()))
                against += std::format("{}{} ({})", against.empty() ? "" : ", ", text_at(competitor, "name"), text_at(competitor, "support"));
            page.push_back(std::format("| {} | {} | **{}** |", text_at(record, "about"), against, text_at(record, "verdict")));
        }
        std::string text;
        for (const auto& line : page) text += line + "\n";
        out[std::format("docs/{}/lead.md", language)] = std::move(text);
    }
    // The evaluation summary: every metric and feature with its line and evidence, for the plan's readers.
    std::vector<std::string> summary{"# 领先度（lead）", "",
                                     std::format("由 `omui-devtools lead docs` 从台账生成（{}）。判定规则见第二部分方案 §2.2。", newest), "",
                                     "## 指标", "", "| 项目 | 指标 | 我方 | 最强竞品 | 比值 [95% 区间] | 判定 |", "|---|---|---|---|---|---|"};
    for (const auto& metric : metrics["metric"]) {
        const std::string id = text_at(metric, "id"), project = text_at(metric, "project");
        const std::string status = metric.contains("status") ? text_at(metric, "status") : "measured";
        const auto found = measured.find(id);
        if (found != measured.end() && text_at(*found->second, "verdict") != "unverified") {
            const json& record = *found->second;
            summary.push_back(std::format("| {} | {} | {} {} | {} {} | {:.2f}× [{:.2f}, {:.2f}] | {} |", project, id, text_at(record["ours"], "name"),
                                          general(record["ours"]["median"]), text_at(record["best"], "name"), general(record["best"]["median"]),
                                          record["ratio"].get<double>(), record["ci"][0].get<double>(), record["ci"][1].get<double>(),
                                          text_at(record, "verdict")));
        } else {
            std::string competitors;
            for (const auto& name : metric.value("competitors", json::array())) competitors += (competitors.empty() ? "" : ", ") + name.get<std::string>();
            summary.push_back(std::format("| {} | {} | — | {} | — | {} |", project, id, competitors,
                                          status == "unverified" || status == "planned" ? status : std::string("未测")));
        }
    }
    summary.insert(summary.end(), {"", "## 标志性特性", "", "| 项目 | 特性 | 主线 | 状态 | 对照判定 |", "|---|---|---|---|---|"});
    for (const auto& feature : features["feature"]) {
        std::string said;
        for (const auto& verdict : verdicts[text_at(feature, "id")]) said += (said.empty() ? "" : ", ") + verdict;
        summary.push_back(std::format("| {} | {} {} | {} | {} | {} |", text_at(feature, "project"), text_at(feature, "id"), text_at(feature, "title"),
                                      text_at(feature, "line"), text_at(feature, "status"), said.empty() ? "—" : said));
    }
    summary.insert(summary.end(), {"", "## 能力对照", "", "| 能力 | 项目 | 竞品 | 判定 | 证据 |", "|---|---|---|---|---|"});
    for (const auto& record : *ledger) {
        if (text_at(record, "kind") != "capability") continue;
        std::string against;
        for (const auto& competitor : record.value("competitors", json::array()))
            against += std::format("{}{}: {}", against.empty() ? "" : ", ", text_at(competitor, "name"), text_at(competitor, "support"));
        summary.push_back(std::format("| {}: {} | {} | {} | {} | `{}` |", text_at(record, "feature"), text_at(record, "about"), text_at(record, "project"), against,
                                      text_at(record, "verdict"), text_at(record, "test")));
    }
    std::string text;
    for (const auto& line : summary) text += line + "\n";
    out[".agents/docs/evaluation/lead.md"] = std::move(text);
    return out;
}

Context context_here(const fs::path& root) {
    Context made;
    made.date = run::today();
    made.machine = std::format("{}-{}t", run::output(root, "hostname", {}).value_or("unknown"), std::thread::hardware_concurrency());
    made.commit = run::commit(root);
    return made;
}

bool append(const fs::path& path, std::span<const std::string> lines) {
    std::error_code error;
    if (path.has_parent_path()) fs::create_directories(path.parent_path(), error);
    std::ofstream out(path, std::ios::binary | std::ios::app);
    for (const auto& line : lines) out << line << '\n';
    return static_cast<bool>(out);
}

std::expected<std::vector<std::string>, std::string> write_pages(const fs::path& root) {
    const auto written = pages(mcpplibs::os::fs::read(root / metrics_path).value_or(""), mcpplibs::os::fs::read(root / features_path).value_or(""),
                               mcpplibs::os::fs::read(root / ledger_path).value_or(""));
    if (!written) return std::unexpected(written.error());
    std::vector<std::string> paths;
    for (const auto& [path, text] : *written) {
        if (!(std::ofstream(root / path, std::ios::binary) << text)) return std::unexpected(std::format("{} cannot be written", path));
        paths.push_back(path);
    }
    return paths;
}

std::vector<std::pair<std::string, std::string>> problems(const fs::path& root) {
    std::vector<std::pair<std::string, std::string>> out;
    const std::string where(features_path);
    const auto features_text = mcpplibs::os::fs::read(root / features_path);
    if (!features_text) return {{where, "missing: the signature features (Part 2 plan §1.5, §4)"}};
    const auto features = json::parse(*features_text, nullptr, false);
    if (features.is_discarded()) return {{where, "not JSON"}};
    const auto ledger_text = mcpplibs::os::fs::read(root / ledger_path).value_or("");
    const auto ledger = read_lines(ledger_text);
    std::set<std::string> far;
    for (const auto& record : ledger.value_or(std::vector<json>{})) {
        const std::string kind = text_at(record, "kind");
        if ((kind == "capability" || kind == "task") && text_at(record, "verdict") == "far_ahead") far.insert(text_at(record, "feature"));
    }
    std::set<std::string> seen;
    std::map<std::string, int> headline;
    const auto list = [](const auto& names) {
        std::string text;
        for (const auto& name : names) text += (text.empty() ? "" : ", ") + std::string(name);
        return text;
    };
    for (const auto& feature : features.value("feature", json::array())) {
        const std::string name = feature.contains("id") ? text_at(feature, "id") : "?";
        if (!seen.insert(name).second) out.emplace_back(where, std::format("{}: listed twice", name));
        for (const std::string_view field : {"id", "project", "title", "line", "status"})
            if (!feature.contains(field)) out.emplace_back(where, std::format("{}: no `{}`", name, field));
        const std::string project = text_at(feature, "project"), line = text_at(feature, "line"), status = text_at(feature, "status");
        if (!std::ranges::contains(projects, std::string_view(project)))
            out.emplace_back(where, std::format("{}: project `{}` is not one of {}", name, feature.contains("project") ? project : "None", list(projects)));
        if (!std::ranges::contains(lines, std::string_view(line)))
            out.emplace_back(where, std::format("{}: line `{}` is not one of {}", name, feature.contains("line") ? line : "None", list(lines)));
        if (!std::ranges::contains(statuses, std::string_view(status)))
            out.emplace_back(where, std::format("{}: status `{}` is not one of {}", name, feature.contains("status") ? status : "None", list(statuses)));
        if (line != "foundation") ++headline[feature.contains("project") ? project : "?"];
        if (status == "evidence" || status == "claimed") {
            const std::string scene = text_at(feature, "scene");
            if (scene.empty()) out.emplace_back(where, std::format("{}: status {} needs `scene`, the signature scene's test", name, status));
            else if (!fs::exists(root / scene)) out.emplace_back(where, std::format("{}: scene {} does not exist", name, scene));
        }
        if (status == "claimed" && !far.contains(name))
            out.emplace_back(where, std::format("{}: claimed, but the ledger has no far_ahead capability or task record for it", name));
    }
    for (const auto project : projects) {
        const int count = headline[std::string(project)];
        if (count < 2 || count > 4) out.emplace_back(where, std::format("{}: {} headline features (two to four, §1.5; the rest are foundations)", project, count));
    }
    // The site's lead page is the ledger's: regenerated with it (omui-devtools lead docs).
    const auto metrics_text = mcpplibs::os::fs::read(root / metrics_path);
    if (metrics_text && fs::exists(root / ledger_path) && fs::exists(root / "docs" / "en" / "lead.md"))
        if (const auto written = pages(*metrics_text, *features_text, ledger_text))
            for (const auto& [path, text] : *written)
                if (mcpplibs::os::fs::read(root / path) != text) out.emplace_back(path, "not the ledger's: run omui-devtools lead docs");
    if (metrics_text) {
        const std::string at(metrics_path);
        const auto metrics = json::parse(*metrics_text, nullptr, false);
        if (metrics.is_discarded()) {
            out.emplace_back(at, "not JSON");
            return out;
        }
        std::set<std::string> ids;
        for (const auto& metric : metrics.value("metric", json::array())) {
            const std::string label = metric.contains("id") ? text_at(metric, "id") : "?";
            if (!ids.insert(label).second) out.emplace_back(at, std::format("{}: listed twice", label));
            const std::string better = text_at(metric, "better");
            if (better != "lower" && better != "higher") out.emplace_back(at, std::format("{}: better is lower or higher", label));
            const std::string status = metric.contains("status") ? text_at(metric, "status") : "measured";
            if (status != "measured" && status != "planned" && status != "unverified")
                out.emplace_back(at, std::format("{}: status is measured, planned or unverified", label));
            if (!metric.contains("competitors") || metric["competitors"].empty()) out.emplace_back(at, std::format("{}: no competitors", label));
        }
    }
    return out;
}

}  // namespace omui::devtools::lead
