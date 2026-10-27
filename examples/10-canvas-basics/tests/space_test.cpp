// space as its user and an agent use it: a folder's tree, summed and sorted;
// its treemap — every block inside its folder, none over another, each the
// size of its share; the map that starts showing everything, flies to the
// largest file when asked, and turns a file into a live card up close; the
// map's items an agent finds by name; and its pixels, pinned.
import std;
import openmui;
import openmui.ui.canvas;
import space;
import openmui.testing;

using namespace omui::testing;
using namespace omui;

namespace {

// A project of known sizes, in a folder named "project".
std::filesystem::path project() {
    const auto root = scratch_dir("space-test") / "project";
    std::filesystem::remove_all(root);
    auto file = [&](std::string_view rel, std::size_t bytes) {
        const auto p = root / rel;
        std::filesystem::create_directories(p.parent_path());
        std::ofstream(p, std::ios::binary) << std::string(bytes, 'x');
    };
    file("src/main.cpp", 42000);
    file("src/canvas.cpp", 31000);
    file("src/canvas.cppm", 9000);
    file("src/util/strings.cpp", 6000);
    file("docs/guide.md", 24000);
    file("docs/api.md", 18000);
    file("assets/logo.png", 120000);
    file("assets/font.ttf", 260000);
    file("data/scene.json", 15000);
    file("README.md", 4000);
    file("build.zip", 90000);
    file("empty.txt", 0);
    return root;
}

struct App {
    space::Model model;
    Headless     h{HeadlessOptions{.size = {1120, 760}}};
    App() {
        auto tree = space::scan(project());
        check(tree.has_value(), tree ? std::string{} : tree.error());
        model.tree = std::move(*tree);
        model.folder = "project";
        AppSpec spec = space::app(model);
        spec.setup(h.runtime());
        h.mount("main", spec.view());
        h.settle();
    }
    [[nodiscard]] world::Camera camera() const { return {model.x.peek(), model.y.peek(), model.zoom.peek()}; }
};

}  // namespace

int main(int argc, char** argv) {
    Suite s{"example.space"};

    s.test("the tree: sizes summed into folders, the largest first, an empty file counted", [] {
        const auto tree = space::scan(project());
        check(tree.has_value());
        const space::Entry& root = tree->root;
        check_eq(root.name, std::string("project"));
        check_eq(root.bytes, std::uint64_t{42000 + 31000 + 9000 + 6000 + 24000 + 18000 + 120000 + 260000 + 15000 + 4000 + 90000});
        check_eq(root.files, std::size_t{12});
        std::vector<std::string> order;
        for (const auto& c : root.children) order.push_back(c.name);
        check_eq(order, (std::vector<std::string>{"assets", "build.zip", "src", "docs", "data", "README.md", "empty.txt"}));
        check_eq(space::human(260000), std::string("253.9 KiB"));
        check(space::category_of("x.CPP") == space::Category::code && space::category_of("logo.png") == space::Category::media &&
              space::category_of("Makefile") == space::Category::other);
        check(!space::scan(project() / "README.md").has_value(), "a file is not a folder");
    });

    s.test("the treemap: every block in its folder, none over another, each its share of the area", [] {
        const auto tree = space::scan(project());
        auto& sc = world::scene("space-layout");
        space::layout(tree->root, sc);
        check_eq(sc.size(), std::size_t{12 + 6});  // the files and the folders (the root, src, util, docs, assets, data)
        std::function<void(const space::Entry&)> visit = [&](const space::Entry& e) {
            if (!e.folder || e.children.empty()) return;
            const auto folder = sc.rect(*sc.find(space::key_of(e.path.empty() ? "." : e.path)));
            double area = 0;
            std::vector<world::WorldRect> kids;
            for (const auto& c : e.children) {
                const auto r = sc.rect(*sc.find(space::key_of(c.path)));
                check(r.x >= folder.x - 1e-9 && r.y >= folder.y - 1e-9 && r.right() <= folder.right() + 1e-6 && r.bottom() <= folder.bottom() + 1e-6,
                      std::format("{} inside {}", c.path, e.path));
                // Widths are floats: neighbours may meet within a float's step of their size.
                for (const auto& k : kids) {
                    const double eps = 1e-6 * (k.width + k.height + r.width + r.height);
                    check(!(k.x < r.right() - eps && r.x < k.right() - eps && k.y < r.bottom() - eps && r.y < k.bottom() - eps),
                          std::format("{} over a sibling", c.path));
                }
                kids.push_back(r);
                area += r.width * r.height;
                visit(c);
            }
            // Shares: a child's area over the children's is its bytes over theirs (an empty file a sliver).
            for (std::size_t i = 0; i < e.children.size(); ++i) {
                const double want = static_cast<double>(std::max<std::uint64_t>(e.children[i].bytes, 1)) /
                                    std::accumulate(e.children.begin(), e.children.end(), 0.0,
                                                    [](double a, const space::Entry& c) { return a + static_cast<double>(std::max<std::uint64_t>(c.bytes, 1)); });
                const double got = kids[i].width * kids[i].height / area;
                check(std::abs(got - want) < 1e-5, std::format("{}: {} of the area for {} of the bytes", e.children[i].path, got, want));
            }
        };
        visit(tree->root);
    });

    s.test("the map starts showing everything; the largest file is a press away and becomes a live card", [] {
        App a;
        const world::Camera start = a.camera();
        check(start.zoom > 0.3 && start.zoom < 1.2, std::format("fitted: zoom {}", start.zoom));
        // The header's "Largest": font.ttf, logo.png, build.zip, main.cpp, canvas.cpp.
        check(a.h.click("main/header/largest/largest-3"), "the button");
        a.h.settle();
        const world::Camera there = a.camera();
        auto& sc = world::scene("space");
        const auto main_cpp = sc.rect(*sc.find(space::key_of("src/main.cpp")));
        check(there.zoom > start.zoom * 2, std::format("closer: {} → {}", start.zoom, there.zoom));
        check(std::abs(there.x - (main_cpp.x + main_cpp.width / 2)) < 1e-6 && std::abs(there.y - (main_cpp.y + main_cpp.height / 2)) < 1e-6, "centred on it");
        // Up close it is a live card: its text is in the semantic tree.
        bool card = false;
        const std::string inside = std::format("main/map/{}/", space::key_of("src/main.cpp"));
        for (const auto& n : a.h.tree().snapshot({}).nodes) card = card || (n.id.starts_with(inside) && n.name == "main.cpp");
        check(card, "the card's heading");
        check(a.h.tree().violations().empty());
    });

    s.test("an agent finds files by name over the whole map, and flies to one", [] {
        App a;
        Invocation call;
        call.actor = {"agent", "test", {}};
        call.args = Value{}.set("query", Value{}.set("name", Value("canvas")));
        auto e = a.h.tree().act("main/map", "find", call);
        check(e.data_value() != nullptr);
        std::vector<std::string> keys;
        if (const auto* l = e.data_value()->list())
            for (const auto& k : *l) keys.emplace_back(k.as_string());
        check_eq(keys.size(), std::size_t{2});
        const auto item = a.h.tree().node(std::format("main/map/{}", space::key_of("src/canvas.cppm")));
        check(item && item->name == "canvas.cppm" && item->role == Role::document, "an item, named by its file");
        call.args = Value{}.set("key", Value(space::key_of("docs")));
        check_eq(a.h.tree().act("main/map", "focus_node", call).exit_code().value_or(-1), std::int64_t{0});
        a.h.settle();
        const auto docs = world::scene("space").rect(*world::scene("space").find(space::key_of("docs")));
        check(std::abs(a.camera().x - (docs.x + docs.width / 2)) < 1e-6, "at the docs folder");
    });

    s.test("the same pixels on every platform", [] {
        App a;
        Hash hash;
        const auto& px = a.h.pixels();
        for (int y = 0; y < px.height(); ++y) hash.bytes(std::as_bytes(std::span(px.row(y), static_cast<std::size_t>(px.width()))));
        check(a.h.save_png(scratch_dir("space-pixels") / "project.png").has_value());
        std::println("    pixels {}", hash.hex());
        // Pinned: GCC and LLVM, Linux, macOS and Windows draw these bytes.
        check_eq(hash.hex(), std::string("026174e7ef273ec5"));
    });

    return s.run(argc, argv);
}
