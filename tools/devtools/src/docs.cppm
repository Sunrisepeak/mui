// omui.devtools.docs — the user documents' generated pages (release plan §6.3, RC3.5): the maturity page from every
// package's label (`[package.metadata.openmui] maturity`, D-189) and the experimental page from the experimental
// packages' README sections (target, now, gap, limits, graduation; D-199), in English and Chinese.
export module omui.devtools.docs;

import std;

export namespace omui::devtools::docs {

struct Package {
    std::string path;   // as the workspace lists it
    std::string name;   // namespace.name
    std::string label;  // stable, preview or experimental
};

// A README's Experimental section: what it says, item by item, and the experimental packages it speaks for.
struct Section {
    std::string readme;                                     // its path from the root
    std::string title;                                      // the README's first heading
    std::vector<std::pair<std::string, std::string>> items;  // "Target", "Now", "Gap", "Limits", "Graduation to preview" … → its text
    std::vector<std::string> packages;
};

struct Inventory {
    std::vector<Package> packages;     // in the workspace's order
    std::vector<Section> experimental;  // in the order of their first package
};

// The project a member belongs to: the directory under modules/projects/, mlibs for modules/{base,gfx,content}, or its
// top directory (examples, showcase, site, tools, mcpp, tests).
std::string project_of(std::string_view path);

// A README's Experimental section (a heading naming Experimental, to the next heading as high), or none.
std::optional<Section> section_of(std::string_view readme);

// Every member's label and every experimental package's section, read from the tree under `root`.
std::expected<Inventory, std::string> read(const std::filesystem::path& root);

// The pages in a language ("en", "zh").
std::string maturity_page(const Inventory& inventory, std::string_view language);
std::string experimental_page(const Inventory& inventory, std::string_view language);

}  // namespace omui::devtools::docs
