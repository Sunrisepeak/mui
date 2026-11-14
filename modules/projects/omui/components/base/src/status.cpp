module omui.ui.base;

import std;
import omui.core;
import omui.view;
import mlibs.hash;

namespace omui {

using parts::mix;

namespace {

constexpr float badge_h = 18, badge_pad = 6, dot_d = 8;
constexpr float bone_h = 12, bone_gap = 8, default_w = 240;

// The first code point of UTF-8 text (its bytes), ASCII letters made upper case.
std::string first_letter(std::string_view s) {
    if (s.empty()) return {};
    const auto c = static_cast<unsigned char>(s[0]);
    const std::size_t n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xe ? 3 : (c >> 3) == 0x1e ? 4 : 1;
    std::string out(s.substr(0, std::min(n, s.size())));
    if (n == 1 && c >= 'a' && c <= 'z') out[0] = static_cast<char>(c - 'a' + 'A');
    return out;
}

}  // namespace

// ── badge ──

void Badge::mounted() {
    label_.set_style("caption");
    label_.set_text(shown());
}

void Badge::prop_changed(BadgeProp) {
    label_.set_text(shown());
    if (!attached()) return;
    services().invalidate_layout(node());
    services().invalidate_semantics(node());
}

std::string Badge::shown() const {
    if (props.dot) return {};
    if (props.count >= 0) return props.count > props.max ? std::format("{}+", props.max) : std::format("{}", props.count);
    return props.text;
}

std::pair<Color, Color> Badge::colors(const Theme& t, BadgeTone tone) {
    switch (tone) {
        case BadgeTone::accent: return {t.color("accent"), t.color("on_accent")};
        case BadgeTone::success: return {t.color("success"), t.color("on_accent")};
        case BadgeTone::warning: return {t.color("warning"), t.color("on_accent")};
        case BadgeTone::danger: return {t.color("danger"), t.color("on_danger", t.color("on_accent"))};
        default: return {t.color("surface_variant"), t.color("text_muted")};
    }
}

Size Badge::measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, mlibs::layout::Available, mlibs::layout::Available) {
    if (props.dot) return {width.value_or(dot_d), height.value_or(dot_d)};
    const Size t = label_.measure(m.services, colors(m.services.theme(), props.tone).second);
    const float h = std::max(badge_h, std::ceil(t.height) + 2);
    return {width.value_or(std::max(h, std::ceil(t.width) + 2 * badge_pad)), height.value_or(h)};
}

float Badge::baseline(MeasureContext& m, Size content) {
    if (props.dot) return content.height;
    const Color c = colors(m.services.theme(), props.tone).second;
    const Size t = label_.measure(m.services, c);
    return (content.height - t.height) / 2 + label_.baseline(m.services, c);
}

void Badge::paint(PaintContext& ctx) const {
    // A dot has no text to carry it: a neutral one in the accent, not the surface's tint.
    const auto [fill, text] = colors(ctx.theme, props.dot && props.tone == BadgeTone::neutral ? BadgeTone::accent : props.tone);
    const Rect box{0, 0, ctx.size.width, ctx.size.height};
    ctx.list.fill_round_rect(box, CornerRadii::all(std::min(box.width, box.height) / 2), xrender::Paint::solid(fill));
    if (props.dot || label_.text().empty()) return;
    const Size t = label_.measure(ctx.services, text);
    label_.paint(ctx, {(box.width - t.width) / 2, (box.height - t.height) / 2}, text);
}

void Badge::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    if (node.name.empty()) node.name = props.dot ? (props.text.empty() ? std::string("New") : props.text) : shown();
    if (props.count >= 0) node.value = Value(props.count);
}

std::size_t Badge::effect_shapes(Size size, Rect, std::span<xrender::EffectShape> out) const {
    if (out.empty()) return 0;
    out[0] = {{0, 0, size.width, size.height}, CornerRadii::all(std::min(size.width, size.height) / 2)};
    return 1;
}

// ── avatar ──

void Avatar::mounted() {
    initials_.set_style("label");
    initials_.set_text(initials(props.name));
}

void Avatar::prop_changed(AvatarProp p) {
    if (p == AvatarProp::name) initials_.set_text(initials(props.name));
    if (!attached()) return;
    if (p == AvatarProp::size) services().invalidate_layout(node());
    services().invalidate_paint(node());
    services().invalidate_semantics(node());
}

std::string Avatar::initials(std::string_view name) {
    std::vector<std::string_view> words;
    for (auto w : name | std::views::split(' '))
        if (!std::ranges::empty(w)) words.emplace_back(w.begin(), w.end());
    if (words.empty()) return {};
    std::string out = first_letter(words.front());
    // A script without case (林黛玉): its first character alone; else the first and last words' first letters.
    if (words.size() > 1 && static_cast<unsigned char>(words.front()[0]) < 0x80) out += first_letter(words.back());
    return out;
}

Color Avatar::hue_of(std::string_view name, bool dark) {
    // Twelve hues, light enough for white initials and dark enough to read on any surface.
    static constexpr std::array<std::uint32_t, 12> light{0xdc2626, 0xea580c, 0xd97706, 0x65a30d, 0x16a34a, 0x0d9488,
                                                         0x0891b2, 0x2563eb, 0x4f46e5, 0x7c3aed, 0xc026d3, 0xdb2777};
    static constexpr std::array<std::uint32_t, 12> deep{0xb91c1c, 0xc2410c, 0xb45309, 0x4d7c0f, 0x15803d, 0x0f766e,
                                                        0x0e7490, 0x1d4ed8, 0x4338ca, 0x6d28d9, 0xa21caf, 0xbe185d};
    const std::size_t i = static_cast<std::size_t>(mlibs::hash::fnv1a(name) % light.size());
    return Color::hex(dark ? deep[i] : light[i]);
}

Size Avatar::measure(MeasureContext&, std::optional<float> width, std::optional<float> height, mlibs::layout::Available, mlibs::layout::Available) {
    const float d = static_cast<float>(std::max(8.0, props.size));
    return {width.value_or(d), height.value_or(d)};
}

void Avatar::paint(PaintContext& ctx) const {
    const Theme& th = ctx.theme;
    const float d = std::min(ctx.size.width, ctx.size.height);
    const Rect disc{(ctx.size.width - d) / 2, (ctx.size.height - d) / 2, d, d};
    const CornerRadii r = props.shape == AvatarShape::circle ? CornerRadii::all(d / 2) : CornerRadii::all(std::round(d * 0.22f));
    // A picture the program stored ("image:<n>"), cut to the disc; else the initials on the name's colour.
    std::optional<xrender::ImageRef> picture;
    if (props.image.starts_with("image:")) {
        std::uint32_t id = 0;
        const std::string_view n = std::string_view(props.image).substr(6);
        if (std::from_chars(n.data(), n.data() + n.size(), id).ec == std::errc{} && ctx.services.images().get(xrender::ImageRef{id})) picture = xrender::ImageRef{id};
    }
    if (picture) {
        const auto* s = ctx.services.images().get(*picture);
        // Cover: the middle of the picture, as large as fills the disc.
        const float sw = static_cast<float>(s->width()), sh = static_cast<float>(s->height());
        const float side = std::min(sw, sh);
        ctx.list.push_clip(disc, r);
        ctx.list.draw_image(*picture, Rect{(sw - side) / 2, (sh - side) / 2, side, side}, disc, xrender::Sampling::linear, 1);
        ctx.list.pop_clip();
    } else {
        ctx.list.fill_round_rect(disc, r, xrender::Paint::solid(hue_of(props.name, th.dark)));
        if (!initials_.text().empty()) {
            const Color white = Color::hex(0xffffff);
            const Size t = initials_.measure(ctx.services, white);
            // As large as the disc wants (two-fifths of it), however large the label's own style is.
            const float k = t.height > 0 ? d * 0.42f / t.height : 1;
            const Point c = disc.center();
            ctx.list.push_transform(Affine::translate(c.x, c.y) * Affine::scale(k, k) * Affine::translate(-t.width / 2, -t.height / 2));
            initials_.paint(ctx, {0, 0}, white);
            ctx.list.pop_transform();
        }
    }
    if (props.status != AvatarStatus::none) {
        const float s = std::max(8.0f, std::round(d * 0.28f)), ring = std::max(1.5f, s * 0.18f);
        const Rect dot{disc.right() - s + (props.shape == AvatarShape::circle ? -d * 0.02f : ring), disc.bottom() - s + ring, s, s};
        const Color c = props.status == AvatarStatus::online ? th.color("success")
                        : props.status == AvatarStatus::away  ? th.color("warning")
                        : props.status == AvatarStatus::busy  ? th.color("danger")
                                                              : th.color("text_disabled");
        ctx.list.fill_round_rect({dot.x - ring, dot.y - ring, s + 2 * ring, s + 2 * ring}, CornerRadii::all(s / 2 + ring), xrender::Paint::solid(th.color("surface")));
        ctx.list.fill_round_rect(dot, CornerRadii::all(s / 2), xrender::Paint::solid(c));
    }
}

void Avatar::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    if (node.name.empty()) node.name = props.name;
    static constexpr std::array<std::string_view, 5> says{"", "online", "away", "busy", "offline"};
    if (props.status != AvatarStatus::none && node.description.empty()) node.description = std::string(says[static_cast<std::size_t>(props.status)]);
}

std::size_t Avatar::effect_shapes(Size size, Rect, std::span<xrender::EffectShape> out) const {
    if (out.empty()) return 0;
    const float d = std::min(size.width, size.height);
    out[0] = {{(size.width - d) / 2, (size.height - d) / 2, d, d},
              props.shape == AvatarShape::circle ? CornerRadii::all(d / 2) : CornerRadii::all(std::round(d * 0.22f))};
    return 1;
}

// ── skeleton ──

void Skeleton::style(const Theme&, mlibs::layout::Style& s) const {
    if (props.shape == SkeletonShape::circle) s.align_self = mlibs::layout::Align::start;  // a picture's place: round, not stretched across
}

Size Skeleton::measure(MeasureContext&, std::optional<float> width, std::optional<float> height, mlibs::layout::Available aw, mlibs::layout::Available) {
    const float w = width.value_or(aw.is_definite() ? std::min(aw.value, default_w) : default_w);
    switch (props.shape) {
        case SkeletonShape::circle: {
            const float d = width.value_or(height.value_or(40));
            return {d, height.value_or(d)};
        }
        case SkeletonShape::block: return {w, height.value_or(std::round(w * 0.56f))};
        default: {
            const auto lines = static_cast<float>(std::max<std::int64_t>(1, props.lines));
            return {w, height.value_or(lines * bone_h + (lines - 1) * bone_gap)};
        }
    }
}

std::size_t Skeleton::bones(Size size, std::span<Rect> out) const {
    if (out.empty()) return 0;
    if (props.shape != SkeletonShape::text) {
        out[0] = {0, 0, size.width, size.height};
        return 1;
    }
    const std::size_t n = std::min(out.size(), static_cast<std::size_t>(std::max<std::int64_t>(1, props.lines)));
    for (std::size_t i = 0; i < n; ++i) {
        // The last of several lines shorter: a paragraph's end.
        const float w = n > 1 && i + 1 == n ? std::round(size.width * 0.62f) : size.width;
        out[i] = {0, static_cast<float>(i) * (bone_h + bone_gap), w, bone_h};
    }
    return n;
}

void Skeleton::paint(PaintContext& ctx) const {
    std::array<Rect, 8> b{};
    const std::size_t n = bones(ctx.size, b);
    const Color c = mix(ctx.theme.color("surface_variant"), ctx.theme.color("border"), 0.45f);
    for (std::size_t i = 0; i < n; ++i) {
        const float r = props.shape == SkeletonShape::circle ? std::min(b[i].width, b[i].height) / 2 : props.shape == SkeletonShape::text ? bone_h / 2 : 8;
        ctx.list.fill_round_rect(b[i], CornerRadii::all(r), xrender::Paint::solid(c));
    }
}

void Skeleton::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    if (node.name.empty()) node.name = props.label;
    node.states.set(State::busy);
}

std::size_t Skeleton::effect_shapes(Size size, Rect, std::span<xrender::EffectShape> out) const {
    std::array<Rect, 8> b{};
    const std::size_t n = std::min(bones(size, b), out.size());
    for (std::size_t i = 0; i < n; ++i) out[i] = {b[i], CornerRadii::all(props.shape == SkeletonShape::text ? bone_h / 2 : std::min(b[i].width, b[i].height) / 2)};
    return n;
}

}  // namespace omui
