// The functions the contract declares, over openmui.fuzzy.
module openmui.native.fuzzy;

import std;
import openmui.fuzzy;

namespace omui::natives::fuzzy {

namespace {

Match from(const omui::fuzzy::Match& m, const std::string& text) {
    Match out;
    out.index = m.index;
    out.text = text;
    out.score = m.score;
    out.positions = m.positions;
    for (const auto& s : m.segments) out.segments.push_back({s.text, s.matched});
    return out;
}

}  // namespace

std::expected<std::optional<Match>, std::string> match(const std::string& query, const std::string& candidate) {
    auto m = omui::fuzzy::match(query, candidate);
    if (!m) return std::optional<Match>{};
    return std::optional<Match>{from(*m, candidate)};
}

std::expected<std::vector<Match>, std::string> rank(const std::string& query, const std::vector<std::string>& candidates,
                                                    const std::optional<std::uint32_t>& limit) {
    std::vector<Match> out;
    for (const auto& m : omui::fuzzy::rank(query, candidates, limit.value_or(0))) out.push_back(from(m, candidates[m.index]));
    return out;
}

}  // namespace omui::natives::fuzzy
