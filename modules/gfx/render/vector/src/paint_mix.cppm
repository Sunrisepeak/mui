// A prepared material transition is graphics data, independent of UI/hanim.
export module openmui.render.vector:paint_mix;
import std;
import :display;

export namespace omui::render {
struct PaintMixOptions { std::size_t max_terms = max_paint_mix_terms; };

// Flat, owned leaf brushes; no recursively growing rendering layers. Preparing
// another transition from a sampled mix flattens and deduplicates its leaves.
// New distinct brushes can grow linearly: a strict term budget reports that
// limitation instead of dropping history or baking an old geometry mask.
class PreparedPaintMix {
public:
    PreparedPaintMix() = default;
    PreparedPaintMix(PreparedPaintMix&&) noexcept = default;
    PreparedPaintMix& operator=(PreparedPaintMix&&) noexcept = default;
    PreparedPaintMix(const PreparedPaintMix&) = delete;
    PreparedPaintMix& operator=(const PreparedPaintMix&) = delete;
    [[nodiscard]] static std::expected<PreparedPaintMix, PaintMixError>
        prepare(const Paint& from, const Paint& to, PaintMixOptions options = {});
    [[nodiscard]] std::size_t term_count() const noexcept { return leaves_.size(); }
    // Writes borrowed immutable brushes + sampled weights into caller storage.
    // That storage must not outlive this plan unless a DisplayList owns it.
    // No allocation; finite progress clamps, invalid input leaves output intact.
    [[nodiscard]] std::expected<std::size_t, PaintMixError>
        sample(double progress, std::span<WeightedPaint> output) const noexcept;
    // One native arena writer; stops/materials become owned by the list.
    [[nodiscard]] std::expected<Paint, PaintMixError>
        record(double progress, DisplayList& output, float opacity = 1) const;
private:
    struct Leaf {
        Paint paint;
        std::vector<GradientStop> stops;
        std::optional<Material> material;
    };
    std::vector<Leaf> leaves_;
    std::vector<double> from_, to_;
};
} // namespace omui::render
