// Faces and fonts over HarfBuzz: hb_face from the file's bytes, hb_font
// instances per (face, weight, slant) with the variation axes set, metrics
// from the OS/2 and hhea tables through hb_ot_metrics.
module mlibs.text.api;

import std;
import mlibs.unicode;
import mcpplibs.os;
import mcpplibs.mwrap.harfbuzz;
using namespace mcpplibs::mwrap::harfbuzz::macros;

namespace mlibs::text {

namespace {

// Family names use ASCII case folding. Comparing views avoids allocating
// lowercase strings while choosing a face for every shaped cluster.
bool same_family(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
    for (std::size_t i = 0; i < a.size(); ++i) if (lower(a[i]) != lower(b[i])) return false;
    return true;
}

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

struct Face {
    FaceInfo              info;
    std::filesystem::path file;  // a face made from a file: its bytes read when first needed

    // Made from bytes there are already.
    void adopt(std::vector<std::uint8_t> bytes) {
        std::call_once(loaded_, [&] {
            data_ = std::move(bytes);
            blob_ = hb_blob_create(reinterpret_cast<const char*>(data_.data()), static_cast<unsigned>(data_.size()), HB_MEMORY_MODE_READONLY,
                                   nullptr, nullptr);
            face_ = hb_face_create(blob_, 0);
            loaded_now_.store(true, std::memory_order_release);
        });
    }
    // HarfBuzz's face over its bytes, and the bytes: read when first needed — shaped with, its coverage asked, its
    // bytes sent — so a fallback face no text reaches (a pack of 30,000 ideographs under Latin text) is never read.
    [[nodiscard]] hb_face_t* hb() const {
        load();
        return face_;
    }
    [[nodiscard]] std::span<const std::uint8_t> bytes() const {
        load();
        return data_;
    }
    [[nodiscard]] std::size_t held() const { return loaded_now_.load(std::memory_order_acquire) ? data_.size() : 0; }
    // The code points it has, collected when first asked — from its character map alone while its bytes are
    // not read (a fallback asked whether it has a character is not read whole for it).
    [[nodiscard]] const hb_set_t* coverage() const {
        std::call_once(collected_, [this] {
            unicodes_ = hb_set_create();
            if (!file.empty() && !loaded_now_.load(std::memory_order_acquire)) {
                Tables tables(file);
                hb_face_t* mapped = hb_face_create_for_tables(&Tables::table, &tables, nullptr);
                hb_face_collect_unicodes(mapped, unicodes_);
                hb_face_destroy(mapped);
            } else {
                hb_face_collect_unicodes(hb(), unicodes_);
            }
        });
        return unicodes_;
    }
    ~Face() {
        if (unicodes_) hb_set_destroy(unicodes_);
        if (face_) hb_face_destroy(face_);
        if (blob_) hb_blob_destroy(blob_);
    }

private:
    void load() const {
        std::call_once(loaded_, [this] {
            if (auto read = read_whole(file)) {
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
    mutable std::once_flag            loaded_;
    mutable std::atomic<bool>         loaded_now_{false};
    mutable std::vector<std::uint8_t> data_;
    mutable hb_blob_t*                blob_ = nullptr;
    mutable hb_face_t*                face_ = nullptr;
    mutable std::once_flag            collected_;
    mutable hb_set_t*                 unicodes_ = nullptr;
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
    std::atomic<std::uint64_t>         uncovered{0};  // clusters no face covered (select)
    std::mutex                         uncovered_lock;
    std::vector<char32_t>              uncovered_points;  // distinct, the first 64
    std::atomic<std::uint64_t>         missed{0};          // clusters the asked-for family had no face for (select)
    std::vector<FontCollection::Missed> missed_points;     // distinct, the first 64 (under uncovered_lock)
    // Fonts are made while text is shaped (a new weight) and read by glyph
    // caches on other threads (a canvas's textures): the lists change under
    // `lock`, and a deque keeps every entry where it is.
    std::deque<Instance>               instances;
    mutable std::shared_mutex          lock;
    std::vector<FaceId>                fallback;
    std::atomic<std::uint64_t>         revision{0};
    // What a collection with no faces shapes with: HarfBuzz's empty face, which
    // has no glyphs, so text takes no room and draws nothing instead of
    // reaching past the end of `faces`.
    Face                               none;  // no file: HarfBuzz's empty face when first asked
    Impl() { none.info.units_per_em = 1000; }

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
        for (Instance& i : instances) hb_font_destroy(i.font);
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

namespace {

// The program's own directory: where a self-contained package keeps it (`mcpp pack --mode self-contained` starts it
// through the loader it carries, so the image is that loader's: its launcher says where the package is,
// MCPP_BUNDLE_DIR, and the program is in its bin/), else the image's.
std::optional<std::filesystem::path> program_directory() {
    namespace os = mcpplibs::os;
    if (auto bundle = os::env::get("MCPP_BUNDLE_DIR"); bundle && !bundle->empty()) return os::fs::from_utf8(*bundle) / "bin";
    if (const auto exe = os::env::image()) return exe->parent_path();
    return std::nullopt;
}

// The source tree's fonts: a checkout of openmui above the program or the
// working directory (any build profile in the tree, where release builds trim
// source paths), else this file's own tree (development builds, out-of-tree
// consumers).
std::filesystem::path checkout_fonts() {
    namespace os = mcpplibs::os;
    const auto above = [](std::filesystem::path dir) -> std::optional<std::filesystem::path> {
        std::error_code ec;
        for (int depth = 0; depth < 12 && !dir.empty(); ++depth) {
            const auto fonts = dir / "modules" / "content" / "text" / "fonts";
            if (std::filesystem::is_regular_file(fonts / "Inter.ttf", ec)) return fonts;
            if (dir == dir.parent_path()) break;
            dir = dir.parent_path();
        }
        return std::nullopt;
    };
    if (const auto directory = program_directory())
        if (auto fonts = above(*directory)) return *fonts;
    std::error_code ec;
    if (auto fonts = above(std::filesystem::current_path(ec))) return *fonts;
    return std::filesystem::path(std::source_location::current().file_name()).parent_path().parent_path().parent_path() / "fonts";
}

}  // namespace

std::filesystem::path FontCollection::default_directory() {
    namespace os = mcpplibs::os;
    // 1. OMUI_FONTS; 2. `fonts/` beside the program (a packaged application; a self-contained one's bin/);
    // 3. a checkout of openmui above the program or the working directory (any
    // build profile in the tree, where release builds trim source paths);
    // 4. this file's own tree (development builds, out-of-tree consumers).
    if (auto env = os::env::get("OMUI_FONTS"); env && !env->empty()) return *env;
    auto has_fonts = [](const std::filesystem::path& dir) {
        std::error_code ec;
        return std::filesystem::is_regular_file(dir / "Inter.ttf", ec);
    };
    // On the Web, the page's file system: the page puts the fonts in /fonts before the program starts.
    if constexpr (os::is_emscripten)
        if (has_fonts("/fonts")) return "/fonts";
    const auto directory = program_directory();
    if (directory && has_fonts(*directory / "fonts")) return *directory / "fonts";
    return checkout_fonts();
}

std::optional<std::filesystem::path> FontCollection::default_file(std::string_view file, const std::filesystem::path& directory) {
    namespace os = mcpplibs::os;
    const auto pack = std::ranges::find(default_fonts, file, &DefaultFont::file);
    const auto in = [&](const std::filesystem::path& dir) -> std::optional<std::filesystem::path> {
        std::error_code ec;
        if (std::filesystem::is_regular_file(dir / file, ec)) return dir / file;
        if (pack != default_fonts.end() && !pack->pack.empty() && std::filesystem::is_regular_file(dir / pack->pack / file, ec))
            return dir / pack->pack / file;
        return std::nullopt;
    };
    if (auto found = in(directory)) return found;
    // A pack the program did not take, in the checkout it was built in: only for the program's own fonts/ (under
    // node too, whose file system is the machine's; a page's has no checkout).
    if (auto env = os::env::get("OMUI_FONTS"); env && !env->empty()) return std::nullopt;
    const auto program = program_directory();
    std::error_code ec;
    if (pack == default_fonts.end() || pack->pack.empty() || !program || !std::filesystem::equivalent(directory, *program / "fonts", ec))
        return std::nullopt;
    return in(checkout_fonts());
}

namespace {

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

std::expected<FaceId, std::string> FontCollection::add_data(std::vector<std::uint8_t> data) {
    auto f = std::make_unique<Face>();
    f->adopt(std::move(data));
    auto info = describe(f->hb());
    if (!info) return std::unexpected("not a font (no glyphs)");
    f->info = std::move(*info);
    std::unique_lock g(impl_->lock);
    impl_->faces.push_back(std::move(f));
    ++impl_->revision;
    return static_cast<FaceId>(impl_->faces.size() - 1);
}

std::expected<FaceId, std::string> FontCollection::add_fallback(const std::filesystem::path& file, std::size_t at) {
    auto id = add_file(file);
    if (!id) return id;
    std::vector<FaceId> chain = impl_->fallback;
    chain.insert(chain.begin() + static_cast<std::ptrdiff_t>(std::min(at, chain.size())), *id);
    set_fallback(std::move(chain));
    return id;
}

std::uint64_t FontCollection::revision() const noexcept { return impl_->revision.load(); }
std::uint64_t FontCollection::uncovered() const noexcept { return impl_->uncovered.load(std::memory_order_relaxed); }
std::uint64_t FontCollection::missed() const noexcept { return impl_->missed.load(std::memory_order_relaxed); }
std::vector<FontCollection::Missed> FontCollection::missed_points() const {
    const std::scoped_lock guard(impl_->uncovered_lock);
    return impl_->missed_points;
}
std::vector<char32_t> FontCollection::uncovered_points() const {
    const std::scoped_lock guard(impl_->uncovered_lock);
    return impl_->uncovered_points;
}

std::expected<FaceId, std::string> FontCollection::add_file(const std::filesystem::path& file) {
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
    auto f = std::make_unique<Face>();
    f->info = std::move(*info);
    f->file = file;
    std::unique_lock g(impl_->lock);
    impl_->faces.push_back(std::move(f));
    ++impl_->revision;
    return static_cast<FaceId>(impl_->faces.size() - 1);
}

std::expected<void, std::string> FontCollection::add_defaults(const std::filesystem::path& directory) {
    std::vector<FaceId> chain;
    for (const auto& font : default_fonts) {
        // A program carries the packs it took (D-214), a page the scripts it
        // shows: the ones there, in this order; Inter at least.
        const auto file = default_file(font.file, directory);
        if (!file && chain.size() > 0) continue;
        auto id = add_file(file.value_or(directory / font.file));
        if (!id) return std::unexpected(id.error());
        chain.push_back(*id);
    }
    set_fallback(std::move(chain));
    return {};
}

std::size_t FontCollection::bytes_held() const {
    std::shared_lock g(impl_->lock);
    std::size_t held = 0;
    for (const auto& face : impl_->faces) held += face->held();
    return held;
}

std::size_t FontCollection::face_count() const noexcept {
    std::shared_lock g(impl_->lock);
    return impl_->faces.size();
}
const FaceInfo& FontCollection::face(FaceId id) const { return impl_->at(id).info; }

std::span<const std::uint8_t> FontCollection::face_data(FaceId id) const { return impl_->at(id).bytes(); }

std::optional<FaceId> FontCollection::find_family(std::string_view family) const {
    {
        std::shared_lock g(impl_->lock);
        for (std::size_t i = 0; i < impl_->faces.size(); ++i)
            if (same_family(impl_->faces[i]->info.family, family)) return static_cast<FaceId>(i);
    }
    // Generic families.
    if (same_family(family, "sans-serif") || same_family(family, "system-ui") || same_family(family, "ui")) return find_family("Inter");
    if (same_family(family, "monospace") || same_family(family, "code")) return find_family("JetBrains Mono");
    if (same_family(family, "emoji")) return find_family("Twemoji Mozilla");
    return std::nullopt;
}

bool FontCollection::covers(FaceId id, char32_t cp) const { return hb_set_has(impl_->at(id).coverage(), cp); }

void FontCollection::set_fallback(std::vector<FaceId> chain) {
    impl_->fallback = std::move(chain);
    ++impl_->revision;
}
std::span<const FaceId> FontCollection::fallback() const noexcept { return impl_->fallback; }

FaceId FontCollection::select(std::span<const std::string> families, std::u32string_view cluster) const {
    auto covers_cluster = [&](const Face& face) {
        for (char32_t cp : cluster) {
            // Joiners, variation selectors and tags do not need a glyph of their own.
            if (cp == 0x200D || cp == 0x200C || (cp >= 0xFE00 && cp <= 0xFE0F) || (cp >= 0xE0020 && cp <= 0xE007F)) continue;
            if (!hb_set_has(face.coverage(), cp)) return false;
        }
        return true;
    };
    auto ok = [&](FaceId face) { return covers_cluster(impl_->at(face)); };
    bool emoji = !cluster.empty() && unicode::extended_pictographic(cluster.front()) &&
                 (cluster.size() > 1 || cluster.front() >= 0x1F000);
    if (emoji)
        for (FaceId f : impl_->fallback)
            if (impl_->at(f).info.color && ok(f)) return f;
    std::string_view asked;  // the first family asked for that this collection has: what a miss is told against
    for (const std::string& fam : families) if (const auto first = find_family(fam)) {
        // Resolve a generic name exactly as find_family does. The original
        // sparse face remains preferred where it covers the cluster; later
        // full faces of that family precede every cross-family fallback.
        const std::string_view actual = impl_->at(*first).info.family;
        if (asked.empty()) asked = actual;
        std::shared_lock g(impl_->lock);
        for (std::size_t i = 0; i < impl_->faces.size(); ++i)
            if (same_family(impl_->faces[i]->info.family, actual) && covers_cluster(*impl_->faces[i])) return static_cast<FaceId>(i);
    }
    // The family asked for has no face for it, whatever draws it now: counted with the family (a host may hold
    // that family's full face back until it is asked for, a cut of it being what it has).
    if (const auto cp = std::ranges::find_if(cluster, [](char32_t c) { return c >= 0x20 && c != 0x7F; }); !asked.empty() && cp != cluster.end()) {
        {
            const std::scoped_lock guard(impl_->uncovered_lock);
            auto& points = impl_->missed_points;
            if (points.size() < 64 && std::ranges::none_of(points, [&](const Missed& m) { return m.code_point == *cp && m.family == asked; }))
                points.push_back({*cp, std::string(asked)});
        }
        impl_->missed.fetch_add(1, std::memory_order_relaxed);
    }
    for (FaceId f : impl_->fallback)
        if (ok(f)) return f;
    // Nothing covers it: counted (a host may fetch a face it deferred), and
    // the first family's face draws .notdef.
    if (const auto cp = std::ranges::find_if(cluster, [](char32_t c) { return c >= 0x20 && c != 0x7F; }); cp != cluster.end()) {
        {
            const std::scoped_lock guard(impl_->uncovered_lock);
            auto& points = impl_->uncovered_points;
            if (points.size() < 64 && std::ranges::find(points, *cp) == points.end()) points.push_back(*cp);
        }
        impl_->uncovered.fetch_add(1, std::memory_order_relaxed);
    }
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
    hb_font_t* f = hb_font_create(impl_->at(face).hb());
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
Slant FontCollection::slant_of(FontRef font) const { return impl_->instance(font).slant; }

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

}  // namespace mlibs::text
