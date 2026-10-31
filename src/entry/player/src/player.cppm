// openmui.entry.player — the player's main (component-binding §10.2), for the
// `entry-player` feature: a script-first application's src/main.cpp is
//
//     import openmui.entry.player;
//     int main(int argc, char** argv) { return omui::player_main(argc, argv); }
//
// It reads [package.metadata.openmui] of the application's mcpp.toml
// (`--manifest FILE`, default ./mcpp.toml), registers the official components
// and the script languages, and runs the player with openmui's options
// (--headless, --png, --serve, --dark …); saved files reload in place.
export module openmui.entry.player;

import std;
import openmui.player;

export namespace omui {

// The manifest of an application, from its mcpp.toml.
[[nodiscard]] std::expected<player::Manifest, std::string> read_manifest(const std::filesystem::path& mcpp_toml);

int player_main(int argc, char** argv);

}  // namespace omui
