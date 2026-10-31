// space — where a folder's space went, on the infinite canvas.
//
// scan() walks a folder (sorted by name: the same tree every time), layout()
// places it as a squarified treemap (Bruls, Huizing and van Wijk) in world
// units — a folder a region with a header strip, its children inside, the
// largest first — and app() shows it: a header (the folder, its size, the
// largest files) above a canvas of the scene "space".
export module space;

import std;
import openmui;
import openmui.ui.canvas;

export namespace space {

// What a file is, for its colour.
enum class Category : std::uint8_t { code, docs, data, media, archive, binary, other };
[[nodiscard]] Category category_of(std::string_view name) noexcept;
[[nodiscard]] std::string_view name(Category c) noexcept;
[[nodiscard]] omui::Color colour(Category c) noexcept;

struct Entry {
    std::string          name;
    std::string          path;      // relative to the root, '/' between parts
    std::uint64_t        bytes = 0; // a folder's: of everything in it
    std::size_t          files = 0; // a folder's: how many files in it
    bool                 folder = false;
    Category             category = Category::other;
    std::vector<Entry>   children;  // the largest first, then by name
};

// The folder's tree; unreadable entries are left out (and counted).
struct Scan {
    Entry       root;
    std::size_t skipped = 0;
};
[[nodiscard]] std::expected<Scan, std::string> scan(const std::filesystem::path& folder);

// "1.4 MiB"
[[nodiscard]] std::string human(std::uint64_t bytes);

// A node's key: stable for its path (a hash), whatever the name's characters.
[[nodiscard]] std::string key_of(std::string_view path);

// The tree into a scene (cleared first), `width` × `height` world units for
// the root; registers the kinds "folder" and "file".
void layout(const Entry& root, omui::world::Scene& scene, double width = 1600, double height = 1000);

// The largest files, largest first.
[[nodiscard]] std::vector<const Entry*> largest(const Entry& root, std::size_t n);

struct Model {
    Scan                                     tree;
    std::string                              folder;  // as shown
    omui::Runtime*                           runtime = nullptr;
    // The camera, both ways: the map writes where it looks; the header shows the zoom.
    omui::Signal<double>                     x = omui::signal(0.0), y = omui::signal(0.0), zoom = omui::signal(1.0);
    // Flies the map to a node (its focus_node action, as an agent would).
    void go(std::string_view key) const;
};

[[nodiscard]] omui::AppSpec app(Model& model);

}  // namespace space
