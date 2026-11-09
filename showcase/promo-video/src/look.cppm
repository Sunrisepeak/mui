// openmui.showcase.promo_video:look — the film's one visual language
// (promo-video design §3.2): an engineering drawing at night. Navy paper and
// its grid, fine cyan lines, one accent; Inter, Noto Sans SC and JetBrains
// Mono. Its scenes are shown in the blueprint style pack
// (blueprint.style.omui.toml); what the film draws by hand reads the same
// palette here.
export module omui.showcase.promo_video:look;

import std;
import omui;

export namespace omui::promo {

// The palette, as 0xRRGGBB (the blueprint style's colours).
namespace ink {
inline constexpr std::uint32_t paper = 0x06122a;      // the background
inline constexpr std::uint32_t paper_deep = 0x020814;  // the dark the hook starts in
inline constexpr std::uint32_t panel = 0x0a1b38;       // a surface
inline constexpr std::uint32_t line = 0x2b6aa3;        // a drawn line
inline constexpr std::uint32_t faint = 0x16365e;       // the grid
inline constexpr std::uint32_t cyan = 0x22d3ee;        // the accent
inline constexpr std::uint32_t ice = 0xe6f4ff;         // text
inline constexpr std::uint32_t muted = 0x8fb3d6;       // quiet text
inline constexpr std::uint32_t amber = 0xfbbf24;       // a warning
inline constexpr std::uint32_t rose = 0xfb7185;        // danger
inline constexpr std::uint32_t mint = 0x34d399;        // the same, verified
}  // namespace ink

[[nodiscard]] Color hex(std::uint32_t rgb, float alpha = 1);

// The style packs in the program's library, once: theme.styles' five and
// the film's blueprint.
void add_styles();
// The film's theme (the blueprint style's), and its name.
inline constexpr std::string_view look_style = "blueprint";
[[nodiscard]] Theme blueprint();

// ── code, as the film shows it ──────────────────────────────────────────

enum class Token : std::uint8_t { plain, keyword, string, number, comment, punct, call, type };
struct Span {
    std::string text;
    Token       kind = Token::plain;
};
// Each line of `code` as spans by what they are (TypeScript, C++: their
// keywords, strings, numbers, comments). Leading comment lines
// and blank lines are the caller's to drop.
[[nodiscard]] std::vector<std::vector<Span>> highlight(std::string_view code, std::string_view language);
[[nodiscard]] Color token_color(Token t);
// The code worth showing: from its first line that is neither a comment nor
// blank (a file's header goes), its trailing blank lines dropped.
[[nodiscard]] std::string shown_code(std::string_view code, std::string_view language);
// A code panel: a row a line — its number, then its spans as text nodes,
// monospaced — so glyph motion types it span by span in reading order.
[[nodiscard]] Element code_block(std::string id, std::string_view code, std::string_view language, float size);

// ── drawing by hand ─────────────────────────────────────────────────────

// The paper: its colour, a vignette, and its grid (minor every 24 units,
// major every 120) moved by `offset` and scaled by `scale` (a parallax).
void paint_paper(xrender::DisplayList& list, Size size, Point offset = {}, float scale = 1, float grid = 1);
// A drawing's frame round a rectangle: corner ticks and a hairline.
void paint_ticks(xrender::DisplayList& list, Rect r, Color c, float tick = 14, float width = 1.5f, float hairline = 0.25f);
// A straight line.
void paint_line(xrender::DisplayList& list, Point a, Point b, Color c, float width = 1.5f);

// The least size of the film's type, in logical pixels of its 1920 × 1080
// (9 px when it plays 960 across): what a painter's labels and the type
// layers' text are drawn at, at the least — checked (checks.cppm `frames`).
inline constexpr float least_type = 18;

// Text shaped once, by its look, for painters.
class Labels {
public:
    // The smallest size any painter has asked for since the process began (∞ before).
    [[nodiscard]] static float smallest() noexcept;
    const mlibs::text::Paragraph& get(mlibs::text::FontCollection& fonts, std::string_view text, float size, Color color, float weight = 500,
                                  std::string_view family = "Inter", float width = 0);
    void clear() { cache_.clear(); }

private:
    std::unordered_map<std::uint64_t, mlibs::text::Paragraph> cache_;
};

// ── time ────────────────────────────────────────────────────────────────

// Moments read through hanim's windows: hanim::window(t, from, to[, ease::smoothstep]).

}  // namespace omui::promo
