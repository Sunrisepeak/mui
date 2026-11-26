// Faces and fonts, the part that is the same whatever reads them: the faces a collection holds and what they are,
// their fonts interned by weight and slant, the fallback chain and the choice of a face per cluster, what no face
// covered, where the default fonts are. What a face's file says and how its glyphs are made is the collection's
// shaper's (:shaper): HarfBuzz's (mlibs.text.harfbuzz), or the cell measure's (cells.cpp).
module mlibs.text.api;

import std;
import mlibs.compose;
import mlibs.unicode;
import mcpplibs.os;
import :shaper;

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

// What a collection says when it has no shaper to read a face with.
std::string no_shaper() {
    return std::format("no shaper is installed ({}): {}", shaper_slot.info.id, shaper_slot.info.absent);
}

}  // namespace

struct FontCollection::Impl {
    struct Face {
        FaceInfo                    info;
        std::unique_ptr<ShaperFace> made;  // null: no face (`none`)
    };
    struct Instance {
        FaceId                      face;
        float                       weight;
        Slant                       slant;
        std::unique_ptr<ShaperFont> made;  // null: the empty face's
    };

    const Shaper*                      shaper = nullptr;
    std::uint64_t                      id = [] {
        Registry& r = registry();
        std::scoped_lock g(r.lock);
        const std::uint64_t id = r.next++;
        r.alive.insert(id);
        return id;
    }();
    std::deque<Face>                   faces;
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
    // What a collection with no faces shapes with: no glyphs, so text takes
    // no room and draws nothing instead of reaching past the end of `faces`.
    Face                               none;
    Impl() { none.info.units_per_em = 1000; }

    [[nodiscard]] const Face& at(FaceId id) const noexcept {
        std::shared_lock g(lock);
        return id < faces.size() ? faces[id] : none;
    }
    // A reference this collection never made — none (0), or one another process named that was never mapped here —
    // is the empty face's: no glyphs, nothing drawn (not a read past the end of `instances`).
    [[nodiscard]] const Instance* instance(FontRef font) const noexcept {
        std::shared_lock g(lock);
        if (font.id == 0 || font.id > instances.size()) return nullptr;
        return &instances[font.id - 1];
    }

    ~Impl() {
        Registry& r = registry();
        std::scoped_lock g(r.lock);
        r.alive.erase(id);
    }
};

FontCollection::FontCollection() : impl_(std::make_unique<Impl>()) { impl_->shaper = compose::process().get(shaper_slot); }
FontCollection::FontCollection(const Shaper& shaper) : impl_(std::make_unique<Impl>()) { impl_->shaper = &shaper; }
FontCollection::~FontCollection() = default;
FontCollection::FontCollection(FontCollection&&) noexcept = default;
FontCollection& FontCollection::operator=(FontCollection&&) noexcept = default;

std::uint64_t FontCollection::id() const noexcept { return impl_ ? impl_->id : 0; }

bool FontCollection::alive(std::uint64_t id) {
    Registry& r = registry();
    std::scoped_lock g(r.lock);
    return r.alive.contains(id);
}

const Shaper* FontCollection::shaper() const noexcept { return impl_ ? impl_->shaper : nullptr; }

const ShaperFont* FontCollection::shaper_font(FontRef font) const noexcept {
    const Impl::Instance* found = impl_->instance(font);
    return found ? found->made.get() : nullptr;
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
    return std::filesystem::path(std::source_location::current().file_name()).parent_path().parent_path() / "fonts";
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

std::expected<FaceId, std::string> FontCollection::add_data(std::vector<std::uint8_t> data) {
    if (!impl_->shaper) return std::unexpected(no_shaper());
    auto made = impl_->shaper->data(std::move(data));
    if (!made) return std::unexpected(std::move(made.error()));
    std::unique_lock g(impl_->lock);
    impl_->faces.push_back({std::move(made->info), std::move(made->face)});
    ++impl_->revision;
    return static_cast<FaceId>(impl_->faces.size() - 1);
}

std::expected<FaceId, std::string> FontCollection::add_file(const std::filesystem::path& file) {
    if (!impl_->shaper) return std::unexpected(no_shaper());
    auto made = impl_->shaper->file(file);
    if (!made) return std::unexpected(std::move(made.error()));
    std::unique_lock g(impl_->lock);
    impl_->faces.push_back({std::move(made->info), std::move(made->face)});
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

std::expected<void, std::string> FontCollection::add_defaults(const std::filesystem::path& directory) {
    if (!impl_->shaper) return std::unexpected(no_shaper());
    if (impl_->shaper->defaults) return impl_->shaper->defaults(*this, directory);
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
    for (const auto& face : impl_->faces) held += face.made ? face.made->held() : 0;
    return held;
}

std::size_t FontCollection::face_count() const noexcept {
    std::shared_lock g(impl_->lock);
    return impl_->faces.size();
}
const FaceInfo& FontCollection::face(FaceId id) const { return impl_->at(id).info; }

std::span<const std::uint8_t> FontCollection::face_data(FaceId id) const {
    const Impl::Face& face = impl_->at(id);
    return face.made ? face.made->bytes() : std::span<const std::uint8_t>{};
}

std::optional<FaceId> FontCollection::find_family(std::string_view family) const {
    {
        std::shared_lock g(impl_->lock);
        for (std::size_t i = 0; i < impl_->faces.size(); ++i)
            if (same_family(impl_->faces[i].info.family, family)) return static_cast<FaceId>(i);
    }
    // Generic families.
    if (same_family(family, "sans-serif") || same_family(family, "system-ui") || same_family(family, "ui")) return find_family("Inter");
    if (same_family(family, "monospace") || same_family(family, "code")) return find_family("JetBrains Mono");
    if (same_family(family, "emoji")) return find_family("Twemoji Mozilla");
    return std::nullopt;
}

bool FontCollection::covers(FaceId id, char32_t cp) const {
    const Impl::Face& face = impl_->at(id);
    return face.made && face.made->covers(cp);
}

void FontCollection::set_fallback(std::vector<FaceId> chain) {
    impl_->fallback = std::move(chain);
    ++impl_->revision;
}
std::span<const FaceId> FontCollection::fallback() const noexcept { return impl_->fallback; }

FaceId FontCollection::select(std::span<const std::string> families, std::u32string_view cluster) const {
    auto covers_cluster = [&](const Impl::Face& face) {
        if (!face.made) return false;
        for (char32_t cp : cluster) {
            // Joiners, variation selectors and tags do not need a glyph of their own.
            if (cp == 0x200D || cp == 0x200C || (cp >= 0xFE00 && cp <= 0xFE0F) || (cp >= 0xE0020 && cp <= 0xE007F)) continue;
            if (!face.made->covers(cp)) return false;
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
            if (same_family(impl_->faces[i].info.family, actual) && covers_cluster(impl_->faces[i])) return static_cast<FaceId>(i);
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
    const Impl::Face& made = impl_->at(face);
    const FaceInfo& info = made.info;
    float w = info.variable ? std::clamp(weight, info.min_weight, info.max_weight) : info.weight;
    auto find = [&]() -> std::optional<FontRef> {
        for (std::size_t i = 0; i < impl_->instances.size(); ++i) {
            const Impl::Instance& in = impl_->instances[i];
            if (in.face == face && in.weight == w && in.slant == slant) return FontRef{static_cast<std::uint32_t>(i + 1)};
        }
        return std::nullopt;
    };
    {
        std::shared_lock g(impl_->lock);
        if (auto r = find()) return *r;
    }
    std::unique_ptr<ShaperFont> font = made.made && impl_->shaper ? impl_->shaper->font(*made.made, info, w, slant) : nullptr;
    std::unique_lock g(impl_->lock);
    if (auto r = find()) return *r;  // made meanwhile on another thread
    impl_->instances.push_back({face, w, slant, std::move(font)});
    return {static_cast<std::uint32_t>(impl_->instances.size())};
}

FaceId FontCollection::face_of(FontRef font) const {
    const Impl::Instance* found = impl_->instance(font);
    return found ? found->face : std::numeric_limits<FaceId>::max();
}
float FontCollection::weight_of(FontRef font) const {
    const Impl::Instance* found = impl_->instance(font);
    return found ? found->weight : 400;
}
Slant FontCollection::slant_of(FontRef font) const {
    const Impl::Instance* found = impl_->instance(font);
    return found ? found->slant : Slant::normal;
}

Metrics FontCollection::metrics(FontRef font, float size) const {
    if (const ShaperFont* made = shaper_font(font)) return made->metrics(size);
    // The empty face's: the proportions a font without its own say (a fifth below the baseline).
    return {.ascent = size * 0.8f, .descent = size * 0.2f, .line_gap = 0, .x_height = size * 0.5f, .cap_height = size * 0.7f,
            .underline_offset = size * 0.1f, .underline_thickness = size * 0.05f};
}

GlyphOutline FontCollection::outline(FontRef font, std::uint32_t glyph) const {
    const ShaperFont* made = shaper_font(font);
    return made ? made->outline(glyph) : GlyphOutline{};
}

std::vector<ColorLayer> FontCollection::color_layers(FontRef font, std::uint32_t glyph) const {
    const ShaperFont* made = shaper_font(font);
    return made ? made->color_layers(glyph) : std::vector<ColorLayer>{};
}

}  // namespace mlibs::text
