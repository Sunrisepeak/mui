// Faces and fonts over HarfBuzz: hb_face from the file's bytes, hb_font
// instances per (face, weight, slant) with the variation axes set, metrics
// from the OS/2 and hhea tables through hb_ot_metrics; the shaper's table.
module mlibs.text.harfbuzz;

import std;
import mlibs.compose;
import mlibs.text;
import mcpplibs.mwrap.harfbuzz;
import :native;
using namespace mcpplibs::mwrap::harfbuzz::macros;

namespace mlibs::text::harfbuzz {

namespace {

// UTF-8 text of a path (generic separators), never through the process's code page: std::filesystem::path::string()
// throws on Windows for a name the ANSI code page cannot spell (lint rule `paths`).
std::string generic_utf8(const std::filesystem::path& p) {
    const std::u8string u = p.generic_u8string();
    return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}

// A file's bytes, read at once (its size asked first: a byte at a time through a stream cost three times the
// file in allocations and most of a program's start).
std::optional<std::vector<std::uint8_t>> read_whole(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::error_code ec;
    const auto size = std::filesystem::file_size(file, ec);
    if (!in || ec) return std::nullopt;
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (static_cast<std::size_t>(in.gcount()) != data.size()) return std::nullopt;
    return data;
}

// A face over a file's tables, each read when HarfBuzz asks for it: what describing the face needs (its names,
// its em, its axes, its colour) without reading the whole file. A single font, not a collection.
struct Tables {
    std::ifstream in;
    struct Entry {
        std::uint32_t tag, offset, length;
    };
    std::vector<Entry> entries;

    explicit Tables(const std::filesystem::path& file) : in(file, std::ios::binary) {
        std::array<unsigned char, 12> header{};
        if (!in.read(reinterpret_cast<char*>(header.data()), header.size())) return;
        const auto u16 = [](const unsigned char* at) { return static_cast<std::uint32_t>(at[0] << 8 | at[1]); };
        const auto u32 = [](const unsigned char* at) {
            return static_cast<std::uint32_t>(at[0]) << 24 | static_cast<std::uint32_t>(at[1]) << 16 | static_cast<std::uint32_t>(at[2]) << 8 | at[3];
        };
        const std::uint32_t version = u32(header.data());
        if (version != 0x00010000u && version != 0x4F54544Fu && version != 0x74727565u) return;  // TrueType, CFF ('OTTO'), 'true'
        std::vector<unsigned char> records(static_cast<std::size_t>(u16(header.data() + 4)) * 16);
        if (!in.read(reinterpret_cast<char*>(records.data()), static_cast<std::streamsize>(records.size()))) return;
        for (std::size_t at = 0; at < records.size(); at += 16)
            entries.push_back({u32(records.data() + at), u32(records.data() + at + 8), u32(records.data() + at + 12)});
    }
    static hb_blob_t* table(hb_face_t*, hb_tag_t tag, void* self) {
        auto& t = *static_cast<Tables*>(self);
        const auto found = std::ranges::find(t.entries, static_cast<std::uint32_t>(tag), &Entry::tag);
        if (found == t.entries.end()) return nullptr;
        auto* bytes = new std::vector<char>(found->length);
        t.in.clear();
        if (!t.in.seekg(found->offset) || !t.in.read(bytes->data(), static_cast<std::streamsize>(bytes->size()))) {
            delete bytes;
            return nullptr;
        }
        return hb_blob_create(bytes->data(), static_cast<unsigned>(bytes->size()), HB_MEMORY_MODE_READONLY, bytes,
                              [](void* owned) { delete static_cast<std::vector<char>*>(owned); });
    }
};

std::string name_of(hb_face_t* face, hb_ot_name_id_t id) {
    unsigned len = 256;
    std::string out(len, '\0');
    unsigned got = hb_ot_name_get_utf8(face, id, HB_LANGUAGE_INVALID, &len, out.data());
    if (got == 0) return {};
    out.resize(len);
    return out;
}

// What a face is (its family, style, em, weight axis, colour), read from HarfBuzz's face; none without glyphs.
std::optional<FaceInfo> describe(hb_face_t* face) {
    if (hb_face_get_glyph_count(face) == 0) return std::nullopt;
    FaceInfo info;
    info.family = name_of(face, HB_OT_NAME_ID_TYPOGRAPHIC_FAMILY);
    if (info.family.empty()) info.family = name_of(face, HB_OT_NAME_ID_FONT_FAMILY);
    info.style = name_of(face, HB_OT_NAME_ID_FONT_SUBFAMILY);
    info.units_per_em = hb_face_get_upem(face);
    // A variable font without a typographic family name often names its
    // family after its default instance ("Noto Sans SC Thin"): drop the weight.
    if (hb_ot_var_has_data(face) && name_of(face, HB_OT_NAME_ID_TYPOGRAPHIC_FAMILY).empty()) {
        for (std::string_view w : {" Thin", " ExtraLight", " Extra Light", " Light", " Regular", " Medium", " SemiBold",
                                   " Semi Bold", " Bold", " ExtraBold", " Extra Bold", " Black"})
            if (info.family.ends_with(w)) {
                info.family.resize(info.family.size() - w.size());
                break;
            }
    }
    info.color = hb_ot_color_has_layers(face) || hb_ot_color_has_paint(face) || hb_ot_color_has_png(face);
    hb_ot_var_axis_info_t axes[8];
    unsigned count = 8;
    hb_ot_var_get_axis_infos(face, 0, &count, axes);
    for (unsigned i = 0; i < count; ++i)
        if (axes[i].tag == HB_OT_TAG_VAR_AXIS_WEIGHT) {
            info.variable = true;
            info.min_weight = axes[i].min_value;
            info.max_weight = axes[i].max_value;
            info.weight = axes[i].default_value;
        }
    return info;
}

}  // namespace

// ── Face ──

Face::~Face() {
    if (unicodes_) hb_set_destroy(unicodes_);
    if (face_) hb_face_destroy(face_);
    if (blob_) hb_blob_destroy(blob_);
}

void Face::adopt(std::vector<std::uint8_t> bytes) {
    std::call_once(loaded_, [&] {
        data_ = std::move(bytes);
        blob_ = hb_blob_create(reinterpret_cast<const char*>(data_.data()), static_cast<unsigned>(data_.size()), HB_MEMORY_MODE_READONLY,
                               nullptr, nullptr);
        face_ = hb_face_create(blob_, 0);
        loaded_now_.store(true, std::memory_order_release);
    });
}

void Face::load() const {
    std::call_once(loaded_, [this] {
        if (auto read = read_whole(file_)) {
            data_ = std::move(*read);
            blob_ = hb_blob_create(reinterpret_cast<const char*>(data_.data()), static_cast<unsigned>(data_.size()), HB_MEMORY_MODE_READONLY,
                                   nullptr, nullptr);
            face_ = hb_face_create(blob_, 0);
        } else {
            face_ = hb_face_reference(hb_face_get_empty());  // gone since it was described: no glyphs, nothing drawn
        }
        loaded_now_.store(true, std::memory_order_release);
    });
}

hb_face_t* Face::hb() const {
    load();
    return face_;
}

std::span<const std::uint8_t> Face::bytes() const {
    load();
    return data_;
}

std::size_t Face::held() const { return loaded_now_.load(std::memory_order_acquire) ? data_.size() : 0; }

// The code points it has, collected when first asked — from its character map alone while its bytes are
// not read (a fallback asked whether it has a character is not read whole for it).
const hb_set_t* Face::coverage() const {
    std::call_once(collected_, [this] {
        unicodes_ = hb_set_create();
        if (!file_.empty() && !loaded_now_.load(std::memory_order_acquire)) {
            Tables tables(file_);
            hb_face_t* mapped = hb_face_create_for_tables(&Tables::table, &tables, nullptr);
            hb_face_collect_unicodes(mapped, unicodes_);
            hb_face_destroy(mapped);
        } else {
            hb_face_collect_unicodes(hb(), unicodes_);
        }
    });
    return unicodes_;
}

bool Face::covers(char32_t code_point) const { return hb_set_has(coverage(), code_point); }

// ── Font ──

Font::~Font() { hb_font_destroy(font_); }

Metrics Font::metrics(float size) const {
    float k = size / static_cast<float>(units_per_em_);
    auto get = [&](hb_ot_metrics_tag_t tag, hb_position_t fallback) {
        hb_position_t v = 0;
        return hb_ot_metrics_get_position(font_, tag, &v) ? v : fallback;
    };
    hb_position_t upem = static_cast<hb_position_t>(units_per_em_);
    Metrics m;
    m.ascent = static_cast<float>(get(HB_OT_METRICS_TAG_HORIZONTAL_ASCENDER, upem * 8 / 10)) * k;
    m.descent = -static_cast<float>(get(HB_OT_METRICS_TAG_HORIZONTAL_DESCENDER, -upem * 2 / 10)) * k;
    m.line_gap = static_cast<float>(get(HB_OT_METRICS_TAG_HORIZONTAL_LINE_GAP, 0)) * k;
    m.x_height = static_cast<float>(get(HB_OT_METRICS_TAG_X_HEIGHT, upem / 2)) * k;
    m.cap_height = static_cast<float>(get(HB_OT_METRICS_TAG_CAP_HEIGHT, upem * 7 / 10)) * k;
    m.underline_offset = -static_cast<float>(get(HB_OT_METRICS_TAG_UNDERLINE_OFFSET, -upem / 10)) * k;
    m.underline_thickness = static_cast<float>(get(HB_OT_METRICS_TAG_UNDERLINE_SIZE, upem / 20)) * k;
    return m;
}

// ── the shaper's entries ──

std::expected<ShaperFaceMade, std::string> from_data(std::vector<std::uint8_t> data) {
    auto face = std::make_unique<Face>();
    face->adopt(std::move(data));
    auto info = describe(face->hb());
    if (!info) return std::unexpected("not a font (no glyphs)");
    return ShaperFaceMade{std::move(*info), std::move(face)};
}

std::expected<ShaperFaceMade, std::string> from_file(const std::filesystem::path& file) {
    // Described from the few tables that say what it is; its bytes read when it is first needed (Face::hb).
    Tables tables(file);
    if (tables.entries.empty()) {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(file, ec)) return std::unexpected(std::format("cannot read {}", generic_utf8(file)));
        return std::unexpected(std::format("{}: not a font (no table directory)", generic_utf8(file)));
    }
    hb_face_t* described = hb_face_create_for_tables(&Tables::table, &tables, nullptr);
    auto info = describe(described);
    hb_face_destroy(described);
    if (!info) return std::unexpected(std::format("{}: not a font (no glyphs)", generic_utf8(file)));
    return ShaperFaceMade{std::move(*info), std::make_unique<Face>(file)};
}

std::unique_ptr<ShaperFont> make_font(const ShaperFace& face, const FaceInfo& info, float weight, Slant slant) {
    hb_font_t* f = hb_font_create(static_cast<const Face&>(face).hb());
    hb_font_set_scale(f, static_cast<int>(info.units_per_em), static_cast<int>(info.units_per_em));
    if (info.variable) {
        hb_variation_t v{HB_OT_TAG_VAR_AXIS_WEIGHT, weight};
        hb_font_set_variations(f, &v, 1);
    }
    if (slant == Slant::italic && !info.variable) hb_font_set_synthetic_slant(f, 0.2f);
    return std::make_unique<Font>(f, info.units_per_em);
}

hb_font_t* native(const FontCollection& fonts, FontRef font) noexcept {
    const auto* made = static_cast<const Font*>(fonts.shaper_font(font));
    return made ? made->hb() : hb_font_get_empty();
}

const Shaper& shaper() {
    // Made when first asked for (at install), never at program start: a program that installs another has none of it.
    static const Shaper table{.name = "harfbuzz", .file = &from_file, .data = &from_data, .font = &make_font, .shape = &shape};
    return table;
}

std::expected<void, compose::Error> install() {
    compose::Composition& composition = compose::process();
    if (composition.provider(shaper_slot.info) == shaper().name) return {};
    return composition.install(shaper_slot, shaper(), shaper().name);
}

}  // namespace mlibs::text::harfbuzz
