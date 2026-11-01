// openmui.site:gallery — the components page: every component of
// openmui.ui.base, live, in the states that say what it is, with what it is
// to every reader (its role, its actions and keys, from its contract) and
// its builder call in each of the four languages — under any of the style
// packs, chosen at the top (a theme scope: the page around it keeps its own).
export module openmui.site:gallery;

import std;
import openmui;

export namespace omui::site {

// A component's sample: its builder's name and the properties the code shows.
struct Sample {
    std::string_view                         component;
    std::vector<std::pair<std::string, Value>> props;
};

// The builder call of a sample in a language: "cpp", "ts", "python" or "lua".
// The properties as the language writes them: C++'s in the options' order (by
// name) with a choice as its enum (`BadgeTone::danger`), TypeScript's in
// camelCase, Python's as keywords, Lua's as a table.
[[nodiscard]] std::string code_of(std::string_view language, const Sample& sample);

// The page's own state: the style shown, the code's language, and the samples' values as a person left them.
struct Gallery {
    Signal<std::string> style = signal(std::string("plain"));
    Signal<std::string> language = signal(std::string("cpp"));
    Signal<bool>        wifi = signal(true), agree = signal(false), dialog = signal(false), toast = signal(false), popover = signal(false),
                        details = signal(false);
    Signal<double>      volume = signal(40.0), qty = signal(2.0);
    Signal<std::string> name = signal(std::string("Ada")), size = signal(std::string("medium")), tab = signal(std::string("overview"));
    Signal<std::int64_t> page = signal(std::int64_t{4}), unread = signal(std::int64_t{3});
    Signal<std::vector<std::string>> path = signal(std::vector<std::string>{"Home", "Docs", "Motion"});
};

struct GallerySite {
    Signal<std::string>*               lang = nullptr;
    std::function<void(std::string)>   style;  // a pack shown in the gallery's scope ("plain": none)
};

// The page: live (operable) or a still, `width` wide.
[[nodiscard]] Element gallery(Gallery& g, const GallerySite& site, bool live, float width);

// The gallery's theme scope's name.
inline constexpr std::string_view gallery_scope = "gallery";

}  // namespace omui::site
