module omui.devtools.lead;

import std;
import nlohmann.json;

namespace omui::devtools::lead {

namespace {

using json = nlohmann::ordered_json;
namespace fs = std::filesystem;

constexpr std::string_view features_path = ".agents/docs/evaluation/lead.features.omui.json";
constexpr std::string_view ledger_path = ".agents/docs/evaluation/lead.ledger.omui.jsonl";
constexpr std::string_view metrics_path = "tests/bench/competitors/lead.metrics.omui.json";
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

std::optional<std::string> read_text(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    return std::string(std::istreambuf_iterator<char>(in), {});
}

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

}  // namespace

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
        out[std::string(language)] = std::move(text);
    }
    return out;
}

std::vector<std::pair<std::string, std::string>> problems(const fs::path& root) {
    std::vector<std::pair<std::string, std::string>> out;
    const std::string where(features_path);
    const auto features_text = read_text(root / features_path);
    if (!features_text) return {{where, "missing: the signature features (Part 2 plan §1.5, §4)"}};
    const auto features = json::parse(*features_text, nullptr, false);
    if (features.is_discarded()) return {{where, "not JSON"}};
    const auto ledger_text = read_text(root / ledger_path).value_or("");
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
    const auto metrics_text = read_text(root / metrics_path);
    if (metrics_text && fs::exists(root / ledger_path) && fs::exists(root / "docs" / "en" / "lead.md"))
        if (const auto written = pages(*metrics_text, *features_text, ledger_text))
            for (const auto& [language, text] : *written)
                if (read_text(root / "docs" / language / "lead.md") != text)
                    out.emplace_back(std::format("docs/{}/lead.md", language), "not the ledger's: run omui-devtools lead docs");
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
