// The platform style packs (W9: Fluent, Cupertino, Material) over the same
// components: the same semantic tree, each platform's shape — its colours,
// corners, type and shadows — and their gallery's pixels pinned for every
// platform.
import std;
import omui.ui.base;
import omui.host.headless;
import omui.theme;
import omui.theme.styles;
import mlibs.testing;
import mlibs.image;
import mcpplibs.mtest;

using namespace mlibs::testing;
using namespace mcpplibs::mtest;
using namespace omui;

namespace {

std::uint64_t hash_of(const xrender::raster::Surface& pixels) {
    Hash hash;
    for (int y = 0; y < pixels.height(); ++y) hash.bytes(std::as_bytes(std::span(pixels.row(y), static_cast<std::size_t>(pixels.width()))));
    return hash.value();
}

Element settings() {
    return column({.width = 320, .padding = 16, .gap = 12, .background = Fill(std::string("background"))},
                  {text({.text = "Notifications", .style = "heading"}),
                   card({.title = std::string("Delivery")},
                        {text_box({.id = "email", .label = "Email", .placeholder = "you@example.org"}),
                         toggle({.id = "push", .checked = true, .label = "Push notifications"}),
                         segmented({.id = "when", .label = "When", .labels = std::vector<std::string>{"Now", "Daily", "Weekly"},
                                    .options = std::vector<std::string>{"now", "daily", "weekly"}, .value = std::string("daily")}),
                         slider({.id = "volume", .label = "Volume", .value = 60})}),
                   row({.gap = 8}, {button({.text = std::string("Save"), .variant = ButtonVariant::primary}), button({.text = std::string("Cancel")})})});
}

}  // namespace

UTest cases{"ui.base.platform_styles", [] {
    register_base_components();

    "fluent, cupertino and material: the same tree under each, each platform's shape — the same pixels on every platform"_test = [] {
        expect(fatal(themes::add_styles().has_value()));
        mlibs::text::FontCollection own;
        expect(own.add_defaults().has_value());
        std::vector<xrender::raster::Surface> pictures;
        std::vector<std::string> trees;
        for (const std::string_view pack : {"fluent", "cupertino", "material"}) {
            HeadlessOptions made;
            made.size = {320, 440};
            made.fonts = &own;
            made.scale = 2;
            Headless h(made);
            const auto applied = theme::apply_style(h.runtime(), pack, false);
            expect(applied.has_value()) << std::string(pack) << ": " << (applied ? std::string() : applied.error());
            h.mount("main", settings());
            h.settle();
            std::string tree;
            h.runtime().walk([&](NodeId id, int) {
                const auto node = h.runtime().semantic_node(id);
                tree += std::format("{} {} {}\n", node.id, spec::name(node.role), node.name);
            });
            trees.push_back(std::move(tree));
            pictures.push_back(h.pixels());
        }
        expect(trees[0] == trees[1] && trees[1] == trees[2]) << "the same semantic tree under every pack";
        // Side by side, for the eye.
        const int width = pictures.front().width(), height = pictures.front().height();
        std::vector<std::uint8_t> strip(static_cast<std::size_t>(width) * 3 * static_cast<std::size_t>(height) * 4);
        for (std::size_t picture = 0; picture < pictures.size(); ++picture)
            for (int y = 0; y < height; ++y)
                std::memcpy(strip.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) * 3 + picture * static_cast<std::size_t>(width)) * 4,
                            pictures[picture].row(y), static_cast<std::size_t>(width) * 4);
        const mlibs::image::View view{strip.data(), width * 3, height, width * 3 * 4, mlibs::image::Alpha::premultiplied};
        expect(mlibs::image::save_png(scratch_dir("ui-base-platform-styles") / "platforms@2x.png", view).has_value());
        std::vector<std::uint64_t> pinned;
        for (const auto& picture : pictures) pinned.push_back(hash_of(picture));
        std::println("ui.base platform styles {:016x} {:016x} {:016x}", pinned[0], pinned[1], pinned[2]);
        expect(pinned[0] != pinned[1] && pinned[1] != pinned[2] && pinned[0] != pinned[2]) << "each its own";
        expect(pinned == std::vector<std::uint64_t>{0x1b10cc3601679777ULL, 0x75fba3ba2e43528aULL, 0x24a41059e59dea88ULL})
            << std::format("platforms {:016x} {:016x} {:016x}", pinned[0], pinned[1], pinned[2]);
    };
}};
