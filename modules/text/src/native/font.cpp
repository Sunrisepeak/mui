// Faces and fonts over HarfBuzz: hb_face from the file's bytes, hb_font
// instances per (face, weight, slant) with the variation axes set, metrics
// from the OS/2 and hhea tables through hb_ot_metrics.
module openmui.text;

import std;
import openmui.unicode;
import mcpplibs.os;
import mcpplibs.mwrap.harfbuzz;
using namespace mcpplibs::mwrap::harfbuzz::macros;

namespace omui::typeset {

namespace {

// UTF-8 text of a path (generic separators for `generic_utf8`), never
// through the process's code page: std::filesystem::path::string() throws on
// Windows for a name the ANSI code page cannot spell (lint rule `paths`).
[[maybe_unused]] std::string utf8(const std::filesystem::path& p) {
    const std::u8string u = p.u8string();
    return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}
[[maybe_unused]] std::string generic_utf8(const std::filesystem::path& p) {
    const std::u8string u = p.generic_u8string();
    return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}

}  // namespace

namespace {

struct Face {
    std::vector<std::uint8_t> data;
    hb_blob_t*                blob = nullptr;
    hb_face_t*                face = nullptr;
    hb_set_t*                 unicodes = nullptr;
    FaceInfo                  info;
};

struct Instance {
    FaceId     face;
    float      weight;
    Slant      slant;
    hb_font_t* font = nullptr;
};

std::string name_of(hb_face_t* face, hb_ot_name_id_t id) {
    unsigned len = 256;
    std::string out(len, '\0');
    unsigned got = hb_ot_name_get_utf8(face, id, HB_LANGUAGE_INVALID, &len, out.data());
    if (got == 0) return {};
    out.resize(len);
    return out;
}

}  // namespace

namespace {

// The collections alive, by id (caches elsewhere drop the dead ones' entries).
struct Registry {
    std::mutex                   lock;
    std::set<std::uint64_t>      alive;
    std::uint64_t                next = 1;
};
Registry& registry() {
    static Registry r;
    return r;
}

}  // namespace

struct FontCollection::Impl {
    std::uint64_t                      id = [] {
        Registry& r = registry();
        std::scoped_lock g(r.lock);
        const std::uint64_t id = r.next++;
        r.alive.insert(id);
        return id;
    }();
    std::deque<std::unique_ptr<Face>>  faces;
    // Fonts are made while text is shaped (a new weight) and read by glyph
    // caches on other threads (a canvas's textures): the lists change under
    // `lock`, and a deque keeps every entry where it is.
    std::deque<Instance>               instances;
    mutable std::shared_mutex          lock;
    std::vector<FaceId>                fallback;
    // What a collection with no faces shapes with: HarfBuzz's empty face, which
    // has no glyphs, so text takes no room and draws nothing instead of
    // reaching past the end of `faces`.
    Face                               none = [] {
        Face f;
        f.face = hb_face_reference(hb_face_get_empty());
        f.unicodes = hb_set_create();
        f.info.units_per_em = 1000;
        return f;
    }();

    [[nodiscard]] const Face& at(FaceId id) const noexcept {
        std::shared_lock g(lock);
        return id < faces.size() ? *faces[id] : none;
    }
    [[nodiscard]] Instance instance(FontRef font) const noexcept {
        std::shared_lock g(lock);
        return instances[font.id - 1];
    }

    ~Impl() {
        {
            Registry& r = registry();
            std::scoped_lock g(r.lock);
            r.alive.erase(id);
        }
        hb_set_destroy(none.unicodes);
        hb_face_destroy(none.face);
        for (Instance& i : instances) hb_font_destroy(i.font);
        for (auto& f : faces) {
            hb_set_destroy(f->unicodes);
            hb_face_destroy(f->face);
            hb_blob_destroy(f->blob);
        }
    }
};

FontCollection::FontCollection() : impl_(std::make_unique<Impl>()) {}
FontCollection::~FontCollection() = default;
FontCollection::FontCollection(FontCollection&&) noexcept = default;
FontCollection& FontCollection::operator=(FontCollection&&) noexcept = default;

std::uint64_t FontCollection::id() const noexcept { return impl_ ? impl_->id : 0; }

bool FontCollection::alive(std::uint64_t id) {
    Registry& r = registry();
    std::scoped_lock g(r.lock);
    return r.alive.contains(id);
}

std::filesystem::path FontCollection::default_directory() {
    namespace os = mcpplibs::os;
    // 1. OMUI_FONTS; 2. `fonts/` beside the program (a packaged application);
    // 3. a checkout of openmui above the program or the working directory (any
    // build profile in the tree, where release builds trim source paths);
    // 4. this file's own tree (development builds, out-of-tree consumers).
    if (auto env = os::env::get("OMUI_FONTS"); env && !env->empty()) return *env;
    auto has_fonts = [](const std::filesystem::path& dir) {
        std::error_code ec;
        return std::filesystem::is_regular_file(dir / "Inter.ttf", ec);
    };
    auto checkout_above = [&](std::filesystem::path dir) -> std::optional<std::filesystem::path> {
        for (int depth = 0; depth < 12 && !dir.empty(); ++depth) {
            if (has_fonts(dir / "modules" / "text" / "fonts")) return dir / "modules" / "text" / "fonts";
            if (dir == dir.parent_path()) break;
            dir = dir.parent_path();
        }
        return std::nullopt;
    };
    const auto exe = os::env::image();
    if (exe && has_fonts(exe->parent_path() / "fonts")) return exe->parent_path() / "fonts";
    if (exe)
        if (auto d = checkout_above(exe->parent_path())) return *d;
    std::error_code ec;
    if (auto d = checkout_above(std::filesystem::current_path(ec))) return *d;
    return std::filesystem::path(std::source_location::current().file_name()).parent_path().parent_path().parent_path() / "fonts";
}

std::expected<FaceId, std::string> FontCollection::add_data(std::vector<std::uint8_t> data) {
    auto f = std::make_unique<Face>();
    f->data = std::move(data);
    f->blob = hb_blob_create(reinterpret_cast<const char*>(f->data.data()), static_cast<unsigned>(f->data.size()),
                             HB_MEMORY_MODE_READONLY, nullptr, nullptr);
    f->face = hb_face_create(f->blob, 0);
    if (hb_face_get_glyph_count(f->face) == 0) {
        hb_face_destroy(f->face);
        hb_blob_destroy(f->blob);
        return std::unexpected("not a font (no glyphs)");
    }
    f->unicodes = hb_set_create();
    hb_face_collect_unicodes(f->face, f->unicodes);
    FaceInfo& info = f->info;
    info.family = name_of(f->face, HB_OT_NAME_ID_TYPOGRAPHIC_FAMILY);
    if (info.family.empty()) info.family = name_of(f->face, HB_OT_NAME_ID_FONT_FAMILY);
    info.style = name_of(f->face, HB_OT_NAME_ID_FONT_SUBFAMILY);
    info.units_per_em = hb_face_get_upem(f->face);
    // A variable font without a typographic family name often names its
    // family after its default instance ("Noto Sans SC Thin"): drop the weight.
    if (hb_ot_var_has_data(f->face) && name_of(f->face, HB_OT_NAME_ID_TYPOGRAPHIC_FAMILY).empty()) {
        for (std::string_view w : {" Thin", " ExtraLight", " Extra Light", " Light", " Regular", " Medium", " SemiBold",
                                   " Semi Bold", " Bold", " ExtraBold", " Extra Bold", " Black"})
            if (info.family.ends_with(w)) {
                info.family.resize(info.family.size() - w.size());
                break;
            }
    }
    info.color = hb_ot_color_has_layers(f->face) || hb_ot_color_has_paint(f->face) || hb_ot_color_has_png(f->face);
    hb_ot_var_axis_info_t axes[8];
    unsigned count = 8;
    hb_ot_var_get_axis_infos(f->face, 0, &count, axes);
    for (unsigned i = 0; i < count; ++i)
        if (axes[i].tag == HB_OT_TAG_VAR_AXIS_WEIGHT) {
            info.variable = true;
            info.min_weight = axes[i].min_value;
            info.max_weight = axes[i].max_value;
            info.weight = axes[i].default_value;
        }
    std::unique_lock g(impl_->lock);
    impl_->faces.push_back(std::move(f));
    return static_cast<FaceId>(impl_->faces.size() - 1);
}

std::expected<FaceId, std::string> FontCollection::add_file(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::unexpected(std::format("cannot read {}", generic_utf8(file)));
    std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto id = add_data(std::move(data));
    if (!id) return std::unexpected(std::format("{}: {}", generic_utf8(file), id.error()));
    return id;
}

std::expected<void, std::string> FontCollection::add_defaults(const std::filesystem::path& directory) {
    std::vector<FaceId> chain;
    for (auto name : {"Inter.ttf", "NotoSansSC.ttf", "NotoSansArabic.ttf", "TwemojiMozilla.ttf", "JetBrainsMono.ttf"}) {
        auto id = add_file(directory / name);
        if (!id) return std::unexpected(id.error());
        chain.push_back(*id);
    }
    set_fallback(std::move(chain));
    return {};
}

std::size_t FontCollection::face_count() const noexcept {
    std::shared_lock g(impl_->lock);
    return impl_->faces.size();
}
const FaceInfo& FontCollection::face(FaceId id) const { return impl_->at(id).info; }

std::optional<FaceId> FontCollection::find_family(std::string_view family) const {
    auto lower = [](std::string_view s) {
        std::string o(s);
        for (char& c : o) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return o;
    };
    std::string want = lower(family);
    {
        std::shared_lock g(impl_->lock);
        for (std::size_t i = 0; i < impl_->faces.size(); ++i)
            if (lower(impl_->faces[i]->info.family) == want) return static_cast<FaceId>(i);
    }
    // Generic families.
    if (want == "sans-serif" || want == "system-ui" || want == "ui") return find_family("Inter");
    if (want == "monospace" || want == "code") return find_family("JetBrains Mono");
    if (want == "emoji") return find_family("Twemoji Mozilla");
    return std::nullopt;
}

bool FontCollection::covers(FaceId id, char32_t cp) const { return hb_set_has(impl_->at(id).unicodes, cp); }

void FontCollection::set_fallback(std::vector<FaceId> chain) { impl_->fallback = std::move(chain); }
std::span<const FaceId> FontCollection::fallback() const noexcept { return impl_->fallback; }

FaceId FontCollection::select(std::span<const std::string> families, std::u32string_view cluster) const {
    auto ok = [&](FaceId f) {
        for (char32_t cp : cluster) {
            // Joiners, variation selectors and tags do not need a glyph of their own.
            if (cp == 0x200D || cp == 0x200C || (cp >= 0xFE00 && cp <= 0xFE0F) || (cp >= 0xE0020 && cp <= 0xE007F)) continue;
            if (!covers(f, cp)) return false;
        }
        return true;
    };
    bool emoji = !cluster.empty() && unicode::extended_pictographic(cluster.front()) &&
                 (cluster.size() > 1 || cluster.front() >= 0x1F000);
    if (emoji)
        for (FaceId f : impl_->fallback)
            if (impl_->at(f).info.color && ok(f)) return f;
    for (const std::string& fam : families)
        if (auto f = find_family(fam); f && ok(*f)) return *f;
    for (FaceId f : impl_->fallback)
        if (ok(f)) return f;
    // Nothing covers it: the first family's face (which draws .notdef).
    for (const std::string& fam : families)
        if (auto f = find_family(fam)) return *f;
    return impl_->fallback.empty() ? 0 : impl_->fallback.front();
}

FontRef FontCollection::font(FaceId face, float weight, Slant slant) {
    const FaceInfo& info = impl_->at(face).info;
    float w = info.variable ? std::clamp(weight, info.min_weight, info.max_weight) : info.weight;
    auto find = [&]() -> std::optional<FontRef> {
        for (std::size_t i = 0; i < impl_->instances.size(); ++i) {
            const Instance& in = impl_->instances[i];
            if (in.face == face && in.weight == w && in.slant == slant) return FontRef{static_cast<std::uint32_t>(i + 1)};
        }
        return std::nullopt;
    };
    {
        std::shared_lock g(impl_->lock);
        if (auto r = find()) return *r;
    }
    hb_font_t* f = hb_font_create(impl_->at(face).face);
    hb_font_set_scale(f, static_cast<int>(info.units_per_em), static_cast<int>(info.units_per_em));
    if (info.variable) {
        hb_variation_t v{HB_OT_TAG_VAR_AXIS_WEIGHT, w};
        hb_font_set_variations(f, &v, 1);
    }
    if (slant == Slant::italic && !info.variable) hb_font_set_synthetic_slant(f, 0.2f);
    std::unique_lock g(impl_->lock);
    if (auto r = find()) {  // made meanwhile on another thread
        hb_font_destroy(f);
        return *r;
    }
    impl_->instances.push_back({face, w, slant, f});
    return {static_cast<std::uint32_t>(impl_->instances.size())};
}

FaceId FontCollection::face_of(FontRef font) const { return impl_->instance(font).face; }
float FontCollection::weight_of(FontRef font) const { return impl_->instance(font).weight; }

void* FontCollection::hb_font(FontRef font) const { return impl_->instance(font).font; }

Metrics FontCollection::metrics(FontRef font, float size) const {
    hb_font_t* f = impl_->instance(font).font;
    const FaceInfo& info = impl_->at(face_of(font)).info;
    float k = size / static_cast<float>(info.units_per_em);
    auto get = [&](hb_ot_metrics_tag_t tag, hb_position_t fallback) {
        hb_position_t v = 0;
        return hb_ot_metrics_get_position(f, tag, &v) ? v : fallback;
    };
    hb_position_t upem = static_cast<hb_position_t>(info.units_per_em);
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

}  // namespace omui::typeset
