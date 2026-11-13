module xengine.playtest;

import std;

namespace xengine::playtest {

mlibs::json::Document Hands::ask(std::string_view line) {
    std::optional<mlibs::json::Document> first;
    session_->request(line, [&](std::string_view record) {
        if (first) return;
        if (auto parsed = mlibs::json::Document::parse(record, "record")) first.emplace(std::move(*parsed));
    });
    if (first) return std::move(*first);
    ok_ = false;
    return std::move(*mlibs::json::Document::parse("{}", "none"));
}
void Hands::act(std::string_view what) { (void)ask(std::format(R"({{"op":"world.act",{}}})", what)); }
void Hands::step(int steps) { (void)ask(std::format(R"({{"op":"world.step","steps":{}}})", steps)); }
mlibs::json::Document Hands::observe() { return ask(R"({"op":"world.observe"})"); }
mlibs::json::Document Hands::branch(int steps, std::string_view what) { return ask(std::format(R"({{"op":"world.branch","steps":{},{}}})", steps, what)); }

void note(Report& report, const Policy& policy, std::chrono::steady_clock::time_point began, std::string line) {
    if (policy.say) {
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
        std::println("  [{:6.1f} s] {}", seconds, line);
        std::fflush(nullptr);
    }
    report.log.push_back(std::move(line));
}

}  // namespace xengine::playtest
