// openmui.site:guide — an agent on the page. It is scripted (no model runs in
// the page), but every step is a real request: openmui.tree@0.1 lines over the
// bus, as Claude Code sends them through `omui mcp` and a page's script
// through window.omui. So openas decides each one (S-11): what is external
// asks the person first, and a grant the person gives in the page lets it
// run. The person's own controls are refused to it whatever it claims to be
// (S-10).
export module openmui.site:guide;

import std;
import openmui;

export namespace omui::site {

// What a visitor can ask the agent to do: its steps are requests.
struct GuideTask {
    std::string_view id, en, zh;
};
[[nodiscard]] std::span<const GuideTask> guide_tasks();

// The agent's name on the bus (and in a grant).
inline constexpr std::string_view guide_actor = "agent:guide";

class Guide {
public:
    // `place`: where the visitor is (the agent reads and acts there).
    Guide(Runtime& runtime, std::function<std::string()> place);

    // Runs a task's steps, one every `pace` (so a person sees each happen);
    // a step that needs the person's answer waits for allow() or deny().
    void run(std::string_view task, seconds pace = seconds(0.45));
    // The person's answer to what the agent asks: this once (the request
    // again with the answer it asks for, openas §3.3); for this visit (a
    // grant for that kind of request, then the request again); or no.
    void allow_once();
    void allow();
    void deny();
    // Every grant the person gave the agent.
    void revoke();

    // What was said: each request ("→") with its records ("←"), and the
    // person's answers; the newest first.
    Signal<std::string> transcript = signal(std::string());
    // What the agent waits for the person to allow (empty: nothing).
    Signal<std::string> asking = signal(std::string());
    // The grants it holds, as openas writes them (empty: none).
    Signal<std::string> grants = signal(std::string());
    Signal<bool>        busy = signal(false);

    // The steps a task sends, in order (what a test reads).
    [[nodiscard]] std::vector<std::string> steps(std::string_view task) const;

private:
    void next();
    void send(const std::string& line);
    void answer_of(const std::string& line);
    void say(std::string line, bool begins = false);
    void show_grants();

    Runtime&                       runtime_;
    std::function<std::string()>   place_;
    agent::Protocol                protocol_;
    std::deque<std::string>        queue_;
    std::string                    waiting_;  // the request that asked the person
    std::string                    answer_;   // the answer it asked for
    std::int64_t                   last_exit_ = 0;
    bool                           answered_ = false;
    seconds                        pace_{0.45};
    std::vector<std::string>       exchanges_;
};

}  // namespace omui::site
