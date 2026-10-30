// openmui.site:app — the site: its places, its lens, its pages pre-rendered.
export module openmui.site:app;

import std;
import openmui;

export namespace omui::site {

// A place on the site: its id, its name in each language.
struct Place {
    std::string_view id, en, zh;
};
[[nodiscard]] std::span<const Place> places();

// A place's Markdown in a language ("en", "zh"); empty for a place that is not a document.
[[nodiscard]] std::string_view document(std::string_view place, std::string_view lang);

// The timeline the live work opens with.
[[nodiscard]] std::string_view sample_timeline();

// The site as an application, opening at a place in a language (the page's
// hash route: #/zh/features runs it with --zh --features).
[[nodiscard]] AppSpec app(std::string_view place = "home", std::string_view lang = "en");

// What a lens shows of a place's semantic snapshot (openmui.semantic's to_value):
// the JSON an agent reads (a node a line), and the assertions a test would write.
[[nodiscard]] std::string agent_text(const Value& snapshot);
[[nodiscard]] std::string test_text(const Value& snapshot);
// The tree's hash: its nodes' ids, roles, names, values and states (not their
// boxes, how far a scroll view can scroll or what is off screen: those follow
// the window; nor the pointer's hover and press, nor a motion under way),
// FNV-1a over their JSON — the same tree, the same hash, in any browser and
// on any machine.
[[nodiscard]] std::uint64_t tree_hash(const Value& snapshot);
// The home page's tree hash in English, as CI pins it (tests/site_test.cpp).
inline constexpr std::uint64_t pinned_home_hash = 0x4966e6a50c3143a3;

// Every place as static HTML (the documents through openmui.markdown's
// to_html, byte for byte md4c-html's), in a <main id="omui-static"> that a
// reader without JavaScript and a search engine read, hidden when the
// program starts.
[[nodiscard]] std::string static_html(std::string_view lang);
// `page` (dist-toy's index.html) with `fragment` in its body, a style that
// shows it until the program runs and a script that hides it then; the
// page's language set. Nothing when the page is not one dist-toy wrote.
[[nodiscard]] std::optional<std::string> inject(std::string_view page, std::string_view fragment, std::string_view lang);

}  // namespace omui::site
