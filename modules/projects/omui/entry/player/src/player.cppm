// openmui.entry.player — the player's main (component-binding §10.2), for the
// `entry-player` feature: a script-first application's src/main.cpp is
//
//     import omui.entry.player;
//     int main(int argc, char** argv) { return omui::player_main(argc, argv); }
//
// It reads [package.metadata.openmui] of the application's mcpp.toml
// (`--manifest FILE`, default ./mcpp.toml), registers the official components
// and the script languages, and runs the player with openmui's options
// (--headless, --png, --serve, --dark …); saved files reload in place.
// `--dev` is development mode (D-149); `--types DIR` writes, instead of
// running, the TypeScript declarations of what the application registered
// and tsc's configuration over its sources (`omui check`, W1 step 5).
export module omui.entry.player;

import std;
import omui.host.headless;  // the host a show's film is drawn on (film_show)
import omui.player;

export namespace omui {

// The manifest of an application, from its mcpp.toml.
[[nodiscard]] std::expected<player::Manifest, std::string> read_manifest(const std::filesystem::path& mcpp_toml);

// Twins of the application (D-149), as `omui dev` makes them: the application again, headless and unseen, with its own
// runtime and player (TypeScript and JavaScript) — what the designer's previews, variants, film and motion run in.
[[nodiscard]] player::TwinFn twins(const player::Manifest& manifest);

// Every official component (the base components and markdown) and native module (fuzzy) registered, for scripts and
// view documents to create by name: what the `player` entry gives an application, and what a derived registry falls
// back to (omui.build's closure(), B7b).
void register_official();

// A show's film (AG4; the presentation design §4.3): the application as it ships, its replay played as its show
// (omui.config.ts's generation.replay with its `show`, design.show's timeline) on a headless host from its first step
// until it ends — drawn at the show's own rate (its timeline's), as the page draws it, so the film's frame at t is the
// page's at t — and every frame of `fps` (which divides that rate) handed to `frame` as the host drew it (its index
// from 0, the host: pixels(), png()). The components it uses registered first (as player_main does). Answers what was
// filmed, or why not.
struct ShowFilm {
    std::size_t frames = 0;   // handed over
    int         fps = 0;
    double      seconds = 0;  // the show's
    Size        size;
};
[[nodiscard]] std::expected<ShowFilm, std::string> film_show(const player::Manifest& manifest, int fps,
                                                             const std::function<void(std::size_t index, Headless& host)>& frame);

// The player over the application's manifest, everything official registered (the `player` entry; an application's
// own main).
int player_main(int argc, char** argv);
// The same with only what `components` registers: the components and native modules the application's build derived
// from its scripts (the `closure` entry: omui.build's closure(), B7b). A program that calls only this does not carry
// the others.
int player_main(int argc, char** argv, void (*components)());

}  // namespace omui

// The module's own: an application's declaration read from its manifest as JSON (manifest.cpp) — the one reader, which
// read_manifest gives the mcpp.toml as JSON off the Web (native/) and the page's snapshot of it on the Web (web/).
namespace omui {
[[nodiscard]] std::expected<player::Manifest, std::string> manifest_of(std::string_view json, const std::filesystem::path& file);
}
