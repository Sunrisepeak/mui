// openmui.site:live_wall — everything shown at once, alive (the site plan
// §7): a wall of cards (the recipe library, the compositions, the motion
// specimens), every one in full, never a thumbnail or a picker. A few on
// screen play at a time, chosen at random (a seeded stream: the same picks
// each run), so the wall breathes; the card under the pointer plays and keeps
// playing while it stays there (the focus follows the pointer). Off screen,
// with less motion asked, or nothing visible: no timer, no frames (D-70).
export module omui.site:live_wall;

import std;
import omui;
import :motion_catalog;

export namespace omui::site {

struct LiveWall {
    std::vector<std::string> cards;    // each card's node path, in reading order
    std::string              hovered;  // the card under the pointer
    std::uint64_t            timer = 0;
    std::uint64_t            stream = 0x6d75692077616c6c;  // the random picks' state (seeded)
    std::vector<std::string> picked;   // what the last round set playing
    // Cards that are not nodes of their own (a canvas's items): eligible while this node is on screen.
    std::string              container;
    // Its motion paused (a person's choice: what moves by itself can be stopped, WCAG 2.2.2).
    Signal<bool>             paused = signal(false);
};

// One round's cards: up to `few` visible ones at random, and the hovered one.
// `play` sets a card going (by its path).
struct LiveWallPlay {
    std::function<void(const std::string&)> play;
    int    few = 3;
    double every = 1.8;  // seconds between rounds
};

// The pointer on a card (or off it): that card plays at once and goes on while it stays.
void live_wall_hover(LiveWall&, Runtime&, const LiveWallPlay&, const std::string& card, bool on);
// Keeps the rounds going while any card is on screen and motion is full; stops them otherwise.
void live_wall_tick(LiveWall&, Runtime&, const LiveWallPlay&);
void live_wall_stop(LiveWall&, Runtime&);
// Its pause: a button that stops (and starts again) what the wall plays by itself.
[[nodiscard]] Element live_wall_pause(LiveWall&, Signal<std::string>* lang);

// The compositions (creative) or the motion specimens, all at once: each card
// its live scene (its own state, its own scoped material), its name and
// purpose; a round flips a scene between its two states, so it moves as its
// motion says. Its title opens the composition's own page.
struct MotionWall {
    std::map<std::string, std::unique_ptr<MotionSceneState>, std::less<>> states;
    LiveWall live;
};
struct MotionWallSite {
    Runtime**            runtime = nullptr;
    Signal<std::string>* lang = nullptr;
    std::string          root;  // the wall's node path
    // A scope's material (a card's style pack in its own scope).
    std::function<void(std::string_view scope, std::string pack)> style;
    std::function<void(std::string id)>                           open;
};
[[nodiscard]] Element motion_wall(MotionWall&, const MotionWallSite&, bool creative, float width);
void motion_wall_tick(MotionWall&, const MotionWallSite&);
// A scene's motion (its spec sample's defaults): what the wall's cards and the gallery's stage animate by.
[[nodiscard]] Value motion_case_config(const MotionCaseInfo&);

}  // namespace omui::site
