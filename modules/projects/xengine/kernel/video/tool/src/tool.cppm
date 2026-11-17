// xengine.video.tool — films without a GUI as an agent's subject, `video`: its catalog's operations
// (video.ops.xdef.ts) — render, still, check, probe, timing. The `video` command is this subject behind the tool
// kernel's entries (mlibs.agent:host): `video render card.video.xdef.ts --out card.webm`, `video mcp`.
//
// Frames come from the sync card or PNG files (xengine.video's own sources). A description whose frames are a
// view's (`frames: { view: … }`, or a view document given directly) needs a view source, which a composition adds
// (L4): xtools gives this subject omui's, which draws the view on a headless host's clock. Without one, such a file
// is refused with the way out.
export module xengine.video.tool;

import std;
export import mlibs.agent;
export import xengine.video;
export import :catalog;

export namespace xengine::video::tool {

// A view's frames for a description, the file read from the description's directory: the view loaded and ready to
// be asked frame by frame in order, or why not.
using ViewSource = std::function<std::expected<std::unique_ptr<FrameSource>, std::string>(const Description& description,
                                                                                       const std::filesystem::path& file)>;

struct Sources {
    ViewSource view;  // none: a view's frames are refused
};

// One operation by its last name (render, still, check, probe, timing): records out (one JSON object a line), what
// a person reads to `err`; 0 done, 1 failed, 2 a request that is not one. omui's design film renders its frames
// through this.
int run(std::string_view operation, const mlibs::agent::Arguments& arguments, std::ostream& out, std::ostream& err,
        const Sources& sources = {});

class Video final : public mlibs::agent::Subject {
public:
    explicit Video(Sources sources = {}) : sources_(std::move(sources)) {}
    [[nodiscard]] std::string_view name() const noexcept override { return "video"; }
    [[nodiscard]] std::string_view interface_name() const noexcept override { return "xengine.video.tool@1"; }
    [[nodiscard]] mlibs::agent::Capabilities capabilities() const noexcept override { return {.observation = true}; }
    [[nodiscard]] std::span<const mlibs::control::CatalogEntry> operations() const noexcept override { return catalog; }
    [[nodiscard]] mlibs::control::Guide guide() const noexcept override;
    [[nodiscard]] std::string_view listing() const noexcept override { return tool::listing; }
    [[nodiscard]] std::uint64_t state_hash() const override { return 0; }  // it keeps nothing between requests
    void operate(std::string_view operation, const mlibs::agent::Request& request, mlibs::agent::Answer& answer) override;

private:
    Sources sources_;
};

}  // namespace xengine::video::tool
