// A catalogue of live previews and individually addressable specimens.
// Builder expressions are API references; they do not claim to export
// the fixtures' complete event bindings, child views or models.
export module omui.site:gallery;

import std;
import omui;
export import :catalog;

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
    Signal<std::string> selected = signal(std::string()), query = signal(std::string()), category = signal(std::string("featured")), pane = signal(std::string());
    Signal<std::string> focus = signal(std::string("button")), copied_source = signal(std::string());
    Signal<bool> copied = signal(false), states = signal(false), browser_open = signal(false), variants = signal(false);
    Signal<bool> preview_paused = signal(false), manual_interaction = signal(false);
    Signal<std::string> preview_status = signal(std::string("playing"));
    Signal<double> browser_y = signal(0.0);
    std::string preview_path;
    NodeId preview_node{};
    std::uint64_t preview_timer = 0, preview_generation = 0;
    bool preview_changed = false;
    std::shared_ptr<unsigned char> lifetime = std::make_shared<unsigned char>(0);
    std::map<std::string, Signal<bool>, std::less<>> disabled;
    std::map<std::string, Signal<std::string>, std::less<>> motions;
    Signal<std::int64_t> saves = signal(std::int64_t{0});
    Signal<std::string> style = signal(std::string("plain"));
    Signal<std::string> language = signal(std::string("cpp"));
    Signal<bool>        wifi = signal(true), agree = signal(false), dialog = signal(false), toast = signal(false), popover = signal(false),
                        details = signal(false);
    Signal<double>      volume = signal(40.0), qty = signal(2.0);
    Signal<double>      upload = signal(0.6);
    Signal<std::int64_t> skeleton_lines = signal(std::int64_t{3});
    Signal<std::string> avatar_name = signal(std::string("Ada Lovelace"));
    Signal<bool>        card_selected = signal(false);
    Signal<std::string> name = signal(std::string("Ada")), size = signal(std::string("medium")), tab = signal(std::string("overview"));
    Signal<std::int64_t> page = signal(std::int64_t{4}), unread = signal(std::int64_t{3});
    Signal<std::vector<std::string>> path = signal(std::vector<std::string>{"Home", "Docs", "Motion"});
    // The second batch's (C2·W4).
    Signal<std::string> view = signal(std::string("week")), date = signal(std::string("2026-11-12")), time = signal(std::string("09:30")),
                        ink = signal(std::string("#4f46e5")), file = signal(std::string("button")), lang_choice = signal(std::string("ts")), sort = signal(std::string("-age")),
                        code = signal(std::string("import std;\n\nint main() {\n    std::println(\"hello, omui\");\n}"));
    Signal<bool>        filter = signal(true), alert = signal(true), drawer = signal(false);
    Signal<double>      low = signal(20.0), high = signal(80.0), ratio = signal(0.4);
    Signal<std::int64_t> step = signal(std::int64_t{2});
    Signal<std::vector<std::string>> open = signal(std::vector<std::string>{"src", "ui"}), picked = signal(std::vector<std::string>{"alan"}),
                                     tags = signal(std::vector<std::string>{"design", "motion"});
};

// The rows the gallery's tree view and data grid show (models of the runtime's).
void fill_gallery_models(Runtime& rt);

struct GallerySite {
    Signal<std::string>*               lang = nullptr;
    Signal<Value>* tree = nullptr;
    std::function<void(std::string)>   open;
    std::function<void(std::string)>   clipboard;
    std::function<void(std::string)>   style;  // a pack shown in the gallery's scope ("plain": none)
    Runtime**                         runtime = nullptr;
};

// The page at its available width; selected state chooses overview or detail.
[[nodiscard]] Element gallery(Gallery& g, const GallerySite& site, bool live, float width);

// One gallery-level scheduler; the site's frame observer calls this after
// paint. Only the visible preview advances, and pointer/focus wins at once.
void tick_gallery(Gallery& gallery, const GallerySite& site);

[[nodiscard]] std::string gallery_html(bool zh, std::string_view component = {});

// The gallery's theme scope's name.
inline constexpr std::string_view gallery_scope = "gallery";

}  // namespace omui::site
