module openmui.site;

import std;
import openmui;

namespace omui::site {

namespace {

constexpr std::array<GuideTask, 5> tasks{{
    {"read", "Read this page", "读这一页"},
    {"tour", "Switch the language, open Features", "切换语言，打开“特性”"},
    {"edit", "Split the title clip, then undo it", "切开 title 片段，再撤销"},
    {"link", "Open the openas standard (external)", "打开 openas 标准（外部）"},
    {"self", "Answer for the person (S-10)", "替人回答（S-10）"},
}};

constexpr std::string_view editor = "main/body/view/atlas/timeline/page/editor";
constexpr std::string_view openas_url = "https://github.com/openlifa/openas";

std::string act(std::string_view node, std::string_view action, std::string_view args = {}) {
    return std::format(R"({{"op":"act","actor":"{}","node":"{}","action":"{}"{}}})", guide_actor, node, action,
                       args.empty() ? std::string() : std::format(R"(,"args":{})", args));
}

// A record, short enough to read in the panel: data cut, errors whole.
std::string brief(std::string_view line) {
    constexpr std::size_t most = 150;
    if (line.size() <= most) return std::string(line);
    std::string s(line.substr(0, most));
    // Not inside a UTF-8 sequence.
    while (!s.empty() && (static_cast<unsigned char>(s.back()) & 0xC0) == 0x80) s.pop_back();
    if (!s.empty() && static_cast<unsigned char>(s.back()) >= 0xC0) s.pop_back();
    return s + std::format(" … ({} bytes)", line.size());
}

// The answer a needs-answer record asks for (its options' first).
std::string asked_answer(const Value& v) {
    if (const Value* a = v.get("answer"); a && a->list() && !a->list()->empty()) return std::string(a->list()->front().as_string());
    if (const Value::Map* m = v.map())
        for (const auto& [k, x] : *m)
            if (auto found = asked_answer(x); !found.empty()) return found;
    return {};
}

}  // namespace

std::span<const GuideTask> guide_tasks() { return tasks; }

Guide::Guide(Runtime& runtime, std::function<std::string()> place)
    : runtime_(runtime), place_(std::move(place)), protocol_(runtime, [] {
          agent::ProtocolOptions o;
          o.app = "org.openmui.site";
          o.default_actor = std::string(guide_actor);
          o.remote = true;  // the bus: openas decides, and the person is never present (S-10, S-11)
          return o;
      }()) {}

std::vector<std::string> Guide::steps(std::string_view task) const {
    // A place's window is live once the camera has flown to it: an agent waits for what it will act on, as any agent does.
    auto wait = [](std::string_view node) { return std::format(R"({{"op":"wait","actor":"{}","q":{{"id":"{}"}},"timeout":5}})", guide_actor, node); };
    if (task == "read") {
        const std::string at = place_() == "overview" ? std::string("main/body/view/atlas") : std::format("main/body/view/atlas/{}/page", place_());
        return {std::format(R"({{"op":"snapshot","actor":"{}","root":"{}","depth":2}})", guide_actor, at)};
    }
    if (task == "tour") return {act("main/bar/lang", "activate"), act("main/body/nav/to_features", "activate"), wait("main/body/view/atlas/features/page/doc")};
    if (task == "edit")
        return {act("main/body/nav/to_timeline", "activate"), wait(editor), act(editor, "seek", R"({"frame":30})"), act(editor, "select", R"({"track":0,"clip":0})"),
                act(editor, "split"), std::format(R"({{"op":"undo","actor":"{}","scope":"{}"}})", guide_actor, editor)};
    if (task == "link")
        return {act("main/body/nav/to_home", "activate"), wait("main/body/view/atlas/home/page/doc"),
                act("main/body/view/atlas/home/page/doc", "open_link", std::format(R"({{"href":"{}"}})", openas_url))};
    if (task == "self") return {act("main/body/guide/asking/answer/allow_once", "activate")};
    return {};
}

void Guide::say(std::string line, bool begins) {
    // An exchange (a request and its records, or a note) is a block; the newest block first.
    if (begins || exchanges_.empty()) exchanges_.emplace_back();
    exchanges_.back() += line + "\n";
    if (exchanges_.size() > 24) exchanges_.erase(exchanges_.begin());
    std::string all;
    for (auto it = exchanges_.rbegin(); it != exchanges_.rend(); ++it) all += *it;
    transcript.set(std::move(all));
}

void Guide::show_grants() {
    std::string out;
    for (const openas::Grant& g : agent_access().grants())
        if (g.actor == to_openas(*Actor::parse(guide_actor))) out += (out.empty() ? "" : "  ") + openas::format(g);
    grants.set(std::move(out));
}

void Guide::send(const std::string& line) {
    say("→ " + line, true);
    answered_ = false;
    protocol_.request(line, [this](std::string_view record) {
        say("← " + brief(record));
        auto v = parse_json(record);
        if (!v) return;
        if (auto a = asked_answer(*v); !a.empty()) answer_ = std::move(a);
        if (v->get("kind") && v->get("kind")->as_string() == "result") {
            last_exit_ = v->get("exitCode") ? v->get("exitCode")->as_int() : 0;
            answered_ = true;
        }
    });
}

void Guide::run(std::string_view task, seconds pace) {
    // While it waits for the person only one thing is left to try: answering for them (S-10).
    if (busy.get() || (!asking.get().empty() && task != "self")) return;
    pace_ = pace;
    for (std::string& s : steps(task)) queue_.push_back(std::move(s));
    if (queue_.empty()) return;
    busy.set(true);
    (void)runtime_.after(seconds(0.0), [this] { next(); });
}

void Guide::next() {
    if (queue_.empty()) {
        busy.set(false);
        return;
    }
    const std::string line = std::move(queue_.front());
    queue_.pop_front();
    send(line);
    answer_of(line);
}

// After a request: its answer now, or (a wait) when the protocol gives it.
void Guide::answer_of(const std::string& line) {
    if (!answered_) {
        (void)runtime_.after(seconds(0.05), [this, line] {
            protocol_.poll();
            answer_of(line);
        });
        return;
    }
    if (last_exit_ == spec::exit_code::needs_answer) {
        // It asks the person (openas §3.3): the rest waits for the answer.
        waiting_ = line;
        auto req = parse_json(line);
        const std::string what = req ? std::format("{} on {}", req->get("action") ? req->get("action")->as_string() : "", req->get("node") ? req->get("node")->as_string() : "")
                                     : line;
        asking.set(what);
        busy.set(false);
        return;
    }
    (void)runtime_.after(pace_, [this] { next(); });
}

void Guide::allow_once() {
    if (waiting_.empty()) return;
    say(std::format("  the person answers \"{}\", this once", answer_), true);
    asking.set(std::string());
    std::string again = std::exchange(waiting_, std::string());
    again.insert(again.size() - 1, std::format(R"(,"answer":"{}")", answer_));
    queue_.push_front(std::move(again));
    busy.set(true);
    (void)runtime_.after(pace_, [this] { next(); });
}

void Guide::allow() {
    if (waiting_.empty()) return;
    // For this visit: the agent may do this kind of request without asking (a grant, S-11).
    auto req = parse_json(waiting_);
    const std::string action = req && req->get("action") ? std::string(req->get("action")->as_string()) : std::string("*");
    if (auto g = openas::parse_grant(std::format("{}=external@{}", guide_actor, action))) agent_access().give(*g);
    show_grants();
    say(std::format("  the person allows {} for this visit", action), true);
    asking.set(std::string());
    queue_.push_front(std::exchange(waiting_, std::string()));
    busy.set(true);
    (void)runtime_.after(pace_, [this] { next(); });
}

void Guide::deny() {
    if (waiting_.empty()) return;
    say("  the person says no", true);
    waiting_.clear();
    queue_.clear();
    asking.set(std::string());
}

void Guide::revoke() {
    const std::size_t n = agent_access().revoke(to_openas(*Actor::parse(guide_actor)));
    say(std::format("  the person revokes the agent's grants ({})", n), true);
    show_grants();
}

}  // namespace omui::site
