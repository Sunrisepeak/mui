// openmui.showcase.promo_video:cache — what a frame depends on, as a digest
// (the frame cache's first and third layers, D-54): an edit is known to
// change exactly the frames that read what it changed, and the encoded
// chunks of the others are kept (openmui.video.webm's encode_webm_chunked).
//
// A frame depends on the program (its own bytes: the shots' code, the fonts
// and art it carries), the player's options, the files the shots read (all
// but the cuts and the lines, which are taken frame by frame), the clip it
// shows — its whole definition, as the timeline file writes it — and where
// in it, a played-back frame's own digest, the lines up at it, and what the
// overlay draws from the whole cut (its chapters, its clips' places on the
// strip, its markers).
export module openmui.showcase.promo_video:cache;

import std;
import :film;
import :player;

export namespace omui::promo {

class Digests {
public:
    Digests(const Film& film, const Cut& cut, const PlayerOptions& options, std::uint64_t program);
    [[nodiscard]] std::uint64_t frame(std::int64_t i);
    // A run of frames' digests, together.
    [[nodiscard]] std::uint64_t frames(std::int64_t first, std::int64_t count);

private:
    const Film*                              film_;
    const Cut*                               cut_;
    std::uint64_t                            global_ = 0, overlay_ = 0;
    std::map<std::int64_t, std::uint64_t>    known_;
};

// A file's bytes, hashed: the program's own (where its shots' code is).
[[nodiscard]] std::uint64_t file_digest(const std::filesystem::path& file);

}  // namespace omui::promo
