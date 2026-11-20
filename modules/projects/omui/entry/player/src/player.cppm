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

// The player over the application's manifest, everything official registered (the `player` entry; an application's
// own main).
int player_main(int argc, char** argv);
// The same with only what `components` registers: the components and native modules the application's build derived
// from its scripts (the `closure` entry: omui.build's closure(), B7b). A program that calls only this does not carry
// the others.
int player_main(int argc, char** argv, void (*components)());

}  // namespace omui
