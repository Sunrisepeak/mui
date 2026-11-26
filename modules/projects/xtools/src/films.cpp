// Films with omui's sources (convergence plan RC3.3): films are meng.video's toolkit (the `video` command); the
// frames of an omui view are omui's — a view document drawn on a headless host's clock, a step a frame — and so are
// the kinds its data modules declare (omui.lang.ts). Composed here, in the engine's tier, which knows both (D-231):
// omui's command line does not build the video toolkit in; `omui design.film` runs `xtools video render`.
module xtools;

import std;
import mcpplibs.os;
import omui.core;
import omui.viewdoc;
import omui.host.headless;
import omui.ui.base;
import omui.ui.markdown;
import meng.video;
import meng.video.tool;
import omui.lang.ts;

namespace xtools {

namespace vid = meng::video;

namespace {

// The official components, registered once: what a view document may name.
void register_components() {
    static const bool done = [] {
        omui::register_base_components();
        omui::register_markdown_component();
        return true;
    }();
    (void)done;
}

std::string utf8(const std::filesystem::path& p) {
    const std::u8string u = p.generic_u8string();
    return {reinterpret_cast<const char*>(u.data()), u.size()};
}

// The frames a view document draws: a headless host on the video's clock,
// a step a frame (the first at 0 s), CPU pixels.
using omui::Headless, omui::HeadlessOptions;

class ViewFrames final : public vid::FrameSource {
public:
    explicit ViewFrames(const vid::Description& d)
        : headless_(HeadlessOptions{.size = {static_cast<float>(d.width), static_cast<float>(d.height)}, .fps = d.fps, .app = "omui-video"}) {}

    std::expected<void, std::string> load(const std::filesystem::path& file) {
        register_components();
        auto v = omui::viewdoc::View::load_file(headless_.runtime(), file);
        if (!v) {
            std::string all;
            for (const auto& d : v.error()) all += (all.empty() ? "" : "; ") + d.text(utf8(file));
            return std::unexpected(all);
        }
        view_.emplace(std::move(*v));
        view_->mount(headless_.runtime(), "main");
        return {};
    }

    std::expected<mlibs::image::View, std::string> frame(std::int64_t index) override {
        if (index != next_) return std::unexpected(std::format("a view's frames come in order (frame {} after {})", index, next_ - 1));
        headless_.step();
        ++next_;
        const mrender::raster::Surface& s = headless_.pixels();
        return mlibs::image::View{reinterpret_cast<const std::uint8_t*>(s.row(0)), s.width(), s.height(), s.width() * 4, mlibs::image::Alpha::premultiplied};
    }

private:
    Headless                     headless_;
    std::optional<omui::viewdoc::View> view_;
    std::int64_t                 next_ = 0;
};

}  // namespace

meng::video::tool::Sources video_sources() {
    return {.view = [](const vid::Description& description, const std::filesystem::path& file) -> std::expected<std::unique_ptr<vid::FrameSource>, std::string> {
                auto frames = std::make_unique<ViewFrames>(description);
                if (auto loaded = frames->load(file); !loaded) return std::unexpected(loaded.error());
                return frames;
            },
            .data = [](const std::filesystem::path& file) { return omui::lang::ts::data_text(file); }};
}

std::unique_ptr<mlibs::agent::Subject> films() { return std::make_unique<meng::video::tool::Video>(video_sources()); }

}  // namespace xtools
