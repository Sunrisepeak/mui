// The release workspace, shared with the task layout scene below it.
// Stable task ids, editable titles, scoped undo and Markdown copying;
// optional views read the selected object from the actual semantic tree.
// The explicitly simulated agent uses those same checked actions.
export module omui.site:hero;

import std;
import omui;

export namespace omui::site {

[[nodiscard]] Value release_tasks();
[[nodiscard]] std::string task_title(const Value& task, bool zh);
[[nodiscard]] bool release_ready(const Value& tasks);

// The opening's own state: the checklist, the node the panels show, what the agent did.
struct Hero {
    Signal<Value>              tasks = signal(release_tasks());
    Signal<std::string>         draft = signal(std::string()), editing = signal(std::string()), edit_text = signal(std::string()), validation = signal(std::string());
    Signal<bool>                inspect = signal(false);
    std::uint64_t              next_task = 1, generation = 0;
    Signal<bool>                shipped = signal(false);
    // Published to the team (simulated: nothing leaves the page): external, so
    // an agent asks the person first (S-11) — the site's permission demonstration.
    Signal<bool>                published = signal(false);
    Signal<std::string>         selected = signal(std::string("ship"));  // a specimen node's id: tests, docs, notes, ship
    Signal<std::string>         copied_tasks = signal(std::string());
    Signal<std::string>         exchange = signal(std::string());       // the agent's last request and its answer
    Signal<bool>                agent_running = signal(false);
};

// The verb behind the board's Publish: external (it reaches the team), so an agent's request asks the person.
inline constexpr std::string_view release_publish_verb = "release.publish";
[[nodiscard]] VerbDesc release_publish_desc();
// Publishes the recorded release (simulated); refused before it is recorded.
[[nodiscard]] VerbResult publish_release(Hero& hero);

[[nodiscard]] Prop<bool> task_binding(Hero& hero, std::string id);

// What the opening needs of the site around it.
struct HeroSite {
    Runtime**                                runtime = nullptr;
    Signal<std::string>*                     lang = nullptr;
    Signal<Value>*                           nodes = nullptr;  // the place's semantic snapshot (its nodes)
    std::function<std::string()>             root;             // the place's path (the specimen's nodes are under it)
    std::function<void(std::string)>         go;               // to another place
    std::function<void()>                    tree_lens;        // the lens to the tree
};

// The opening at its available width; the site uses its live scene.
[[nodiscard]] Element hero(Hero& h, const HeroSite& site, bool live, float width = 1100);
// Readable claim and seed checklist for the static page.
[[nodiscard]] std::string hero_html(bool zh);

// The panels' lines for one node of a semantic snapshot (exported for the tests).
[[nodiscard]] std::string reader_line(const Value& node, bool zh);
[[nodiscard]] std::string agent_line(const Value& node, std::string_view action);
[[nodiscard]] std::string test_line(const Value& node);

}  // namespace omui::site
