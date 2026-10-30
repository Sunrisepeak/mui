// openmui.site:hero — the home page's opening: one thing, rendered four ways.
//
// A real checklist (three checks, then "Ship 0.1", which waits for them) as a
// person sees it, and beside it the same node — the one last touched — as a
// screen reader says it, as an agent asks for it, and as a test asserts it,
// read from the page's semantic tree (the lens's snapshot), joined to it by a
// line. "Let an agent ship it" performs the same actions a person's clicks
// reach, one after another, as an agent, through the runtime's checks.
export module openmui.site:hero;

import std;
import openmui;

export namespace omui::site {

// The opening's own state: the checklist, the node the panels show, what the agent did.
struct Hero {
    std::array<Signal<bool>, 3> checks{signal(false), signal(false), signal(false)};
    Signal<bool>                shipped = signal(false);
    Signal<std::string>         selected = signal(std::string("ship"));  // a specimen node's id: tests, docs, notes, ship
    Signal<std::string>         exchange = signal(std::string());       // the agent's last request and its answer
    Signal<bool>                agent_running = signal(false);
};

// What the opening needs of the site around it.
struct HeroSite {
    Runtime**                                runtime = nullptr;
    Signal<std::string>*                     lang = nullptr;
    Signal<Value>*                           nodes = nullptr;  // the place's semantic snapshot (its nodes)
    std::function<std::string()>             root;             // the place's path (the specimen's nodes are under it)
    std::function<void(std::string)>         go;               // to another place
    std::function<void()>                    tree_lens;        // the lens to the tree
};

// The opening: live (operable, its panels following the tree) or a still (the atlas's texture).
[[nodiscard]] Element hero(Hero& h, const HeroSite& site, bool live);
// The mark: one node branching into its four renderings (a pixel, a focus ring, braces, a check).
[[nodiscard]] Element brand_mark(std::string id, float size);

// The opening for readers without the program (the static page): the claim,
// the checklist and the three renderings of its button, as HTML.
[[nodiscard]] std::string hero_html(bool zh);

// The panels' lines for one node of a semantic snapshot (exported for the tests).
[[nodiscard]] std::string reader_line(const Value& node, bool zh);
[[nodiscard]] std::string agent_line(const Value& node, std::string_view action);
[[nodiscard]] std::string test_line(const Value& node);

}  // namespace omui::site
