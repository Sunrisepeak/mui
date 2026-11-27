// mlibs.text.harfbuzz:native — HarfBuzz's face and font as the shaper keeps them, shared by its units (font.cpp
// reads and describes faces and makes fonts, shape.cpp shapes runs, outline.cpp draws glyphs as geometry).
module mlibs.text.harfbuzz:native;

import std;
import mlibs.text;
import mcpplibs.mwrap.harfbuzz;

namespace mlibs::text::harfbuzz {

// A face over HarfBuzz: described from the few tables that say what it is (font.cpp), its bytes read when first
// needed — shaped with, its bytes sent — so a fallback face no text reaches (a pack of 30,000 ideographs under
// Latin text) is never read; whether it has a character is answered from its character map alone.
class Face final : public ShaperFace {
public:
    Face() = default;
    explicit Face(std::filesystem::path file) : file_(std::move(file)) {}
    ~Face() override;
    Face(const Face&) = delete;
    Face& operator=(const Face&) = delete;

    // Made from bytes there are already.
    void adopt(std::vector<std::uint8_t> bytes);
    // HarfBuzz's face over its bytes (read now if they were not).
    [[nodiscard]] hb_face_t* hb() const;

    [[nodiscard]] bool covers(char32_t code_point) const override;
    [[nodiscard]] std::span<const std::uint8_t> bytes() const override;
    [[nodiscard]] std::size_t held() const override;

private:
    void load() const;
    [[nodiscard]] const hb_set_t* coverage() const;

    std::filesystem::path             file_;  // a face made from a file: its bytes read when first needed
    mutable std::once_flag            loaded_;
    mutable std::atomic<bool>         loaded_now_{false};
    mutable std::vector<std::uint8_t> data_;
    mutable hb_blob_t*                blob_ = nullptr;
    mutable hb_face_t*                face_ = nullptr;
    mutable std::once_flag            collected_;
    mutable hb_set_t*                 unicodes_ = nullptr;
};

// A face at a weight and slant: HarfBuzz's font with the weight axis set, scaled to units per em.
class Font final : public ShaperFont {
public:
    Font(hb_font_t* font, std::uint32_t units_per_em) : font_(font), units_per_em_(units_per_em) {}
    ~Font() override;
    Font(const Font&) = delete;
    Font& operator=(const Font&) = delete;

    [[nodiscard]] hb_font_t* hb() const noexcept { return font_; }
    [[nodiscard]] Metrics metrics(float size) const override;
    [[nodiscard]] GlyphOutline outline(std::uint32_t glyph) const override;  // outline.cpp
    [[nodiscard]] std::vector<ColorLayer> color_layers(std::uint32_t glyph) const override;  // outline.cpp

private:
    hb_font_t*    font_;
    std::uint32_t units_per_em_;
};

// The shaper's entries (font.cpp; shape.cpp).
[[nodiscard]] std::expected<ShaperFaceMade, std::string> from_file(const std::filesystem::path& file);
[[nodiscard]] std::expected<ShaperFaceMade, std::string> from_data(std::vector<std::uint8_t> data);
[[nodiscard]] std::unique_ptr<ShaperFont> make_font(const ShaperFace& face, const FaceInfo& info, float weight, Slant slant);
void shape(Paragraph::Shaped& shaped, std::span<const std::uint8_t> level_at);

// HarfBuzz's font of a reference: its empty one, without glyphs, for a reference the collection never made.
[[nodiscard]] hb_font_t* native(const FontCollection& fonts, FontRef font) noexcept;

}  // namespace mlibs::text::harfbuzz
