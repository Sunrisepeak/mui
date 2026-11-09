// A display list as bytes, and back (xrender.vector:wire).
module xrender.vector.api;

import std;

namespace xrender {

namespace {

constexpr std::string_view magic = "xrdl";
constexpr std::uint32_t version = 1;

// Little-endian, floats as their bits.
struct Writer {
    std::string bytes;

    template <class T>
        requires std::is_arithmetic_v<T> || std::is_enum_v<T>
    void put(T value) {
        if constexpr (std::is_enum_v<T>) {
            put(static_cast<std::underlying_type_t<T>>(value));
        } else if constexpr (std::is_same_v<T, bool>) {
            put(static_cast<std::uint8_t>(value ? 1 : 0));
        } else if constexpr (std::is_floating_point_v<T>) {
            using Bits = std::conditional_t<sizeof(T) == 4, std::uint32_t, std::uint64_t>;
            put(std::bit_cast<Bits>(value));
        } else {
            auto bits = static_cast<std::make_unsigned_t<T>>(value);
            for (std::size_t index = 0; index < sizeof(T); ++index) {
                bytes.push_back(static_cast<char>(bits & 0xFF));
                if constexpr (sizeof(T) > 1) bits >>= 8;
            }
        }
    }
    void count(std::size_t size) { put(static_cast<std::uint32_t>(size)); }
    void put(Point point) { put(point.x); put(point.y); }
    void put(Size size) { put(size.width); put(size.height); }
    void put(const Rect& rect) { put(rect.x); put(rect.y); put(rect.width); put(rect.height); }
    void put(const Color& color) { put(color.r); put(color.g); put(color.b); put(color.a); }
    void put(const CornerRadii& radii) {
        put(radii.top_left); put(radii.top_right); put(radii.bottom_right); put(radii.bottom_left); put(radii.smoothing);
    }
    void put(const Affine& m) { put(m.a); put(m.b); put(m.c); put(m.d); put(m.e); put(m.f); }
    void put(std::string_view text) {
        count(text.size());
        bytes.append(text);
    }
    void put(const PathView& path) {
        count(path.verbs.size());
        for (const PathVerb verb : path.verbs) put(verb);
        count(path.points.size());
        for (const Point point : path.points) put(point);
    }
    void put(const Material& material) {
        put(material.normal.id); put(material.light); put(material.light_z); put(material.shadow); put(material.shadow_strength);
        put(static_cast<std::int32_t>(material.bands)); put(material.softness); put(material.rim); put(material.rim_strength);
        put(material.rim_width); put(material.key);
    }
    void put(const Paint& paint) {
        put(paint.kind);
        put(paint.color);
        put(paint.gradient.kind); put(paint.gradient.start); put(paint.gradient.end); put(paint.gradient.radius); put(paint.gradient.spread);
        count(paint.gradient.stops.size());
        for (const GradientStop& stop : paint.gradient.stops) { put(stop.offset); put(stop.color); }
        put(paint.image.id);
        put(paint.image_transform);
        put(paint.sampling);
        put(paint.material != nullptr);
        if (paint.material) put(*paint.material);
        count(paint.mix.size());
        for (const WeightedPaint& term : paint.mix) { put(term.weight); put(term.paint); }
        put(paint.mix_opacity);
    }
    void put(const StrokeStyle& style) {
        put(style.width); put(style.cap); put(style.join); put(style.miter_limit);
        count(style.dashes.size());
        for (const float dash : style.dashes) put(dash);
        put(style.dash_offset); put(style.trim_start); put(style.trim_end); put(style.trim_offset);
    }
    void put(const PartFrame& frame) {
        put(frame.owner); put(frame.role); put(frame.id); put(frame.allocation); put(frame.anchor); put(frame.kind);
        count(frame.lineage.size());
        for (const ReferenceWeight& weight : frame.lineage) { put(weight.key); put(weight.weight); }
    }
};

// What a reader holds while a command is made: the spans and strings the
// command points into, kept until the list owns copies (append_command).
struct Held {
    std::deque<std::string>                       texts;
    std::deque<std::vector<PathVerb>>             verbs;
    std::deque<std::vector<Point>>                points;
    std::deque<std::vector<GradientStop>>         stops;
    std::deque<std::vector<float>>                floats;
    std::deque<std::vector<Glyph>>                glyphs;
    std::deque<std::vector<GlyphStyle>>           styles;
    std::deque<std::vector<EffectShape>>          shapes;
    std::deque<std::vector<WeightedPaint>>        mixes;
    std::deque<std::vector<ReferenceWeight>>      lineages;
    std::deque<Material>                          materials;
    std::deque<PartFrame>                         frames;
    void clear() { *this = Held{}; }
};

struct Reader {
    std::string_view bytes;
    std::size_t      at = 0;
    bool             failed = false;
    Held&            held;

    bool need(std::size_t size) {
        if (failed || bytes.size() - at < size) {
            failed = true;
            return false;
        }
        return true;
    }
    template <class T>
        requires std::is_arithmetic_v<T> || std::is_enum_v<T>
    T get() {
        if constexpr (std::is_enum_v<T>) {
            return static_cast<T>(get<std::underlying_type_t<T>>());
        } else if constexpr (std::is_same_v<T, bool>) {
            return get<std::uint8_t>() != 0;
        } else if constexpr (std::is_floating_point_v<T>) {
            using Bits = std::conditional_t<sizeof(T) == 4, std::uint32_t, std::uint64_t>;
            return std::bit_cast<T>(get<Bits>());
        } else {
            using Unsigned = std::make_unsigned_t<T>;
            if (!need(sizeof(T))) return T{};
            Unsigned bits = 0;
            for (std::size_t index = 0; index < sizeof(T); ++index)
                bits |= static_cast<Unsigned>(static_cast<std::uint8_t>(bytes[at + index])) << (8 * index);
            at += sizeof(T);
            return static_cast<T>(bits);
        }
    }
    // A count no larger than the bytes left could hold (a damaged list cannot ask for a huge allocation).
    std::size_t count(std::size_t smallest = 1) {
        const auto size = static_cast<std::size_t>(get<std::uint32_t>());
        if (!failed && size > (bytes.size() - at) / std::max<std::size_t>(smallest, 1)) failed = true;
        return failed ? 0 : size;
    }
    Point point() { const float x = get<float>(); return {x, get<float>()}; }
    Size size() { const float width = get<float>(); return {width, get<float>()}; }
    Rect rect() {
        const float x = get<float>(), y = get<float>(), width = get<float>();
        return {x, y, width, get<float>()};
    }
    Color color() {
        const float r = get<float>(), g = get<float>(), b = get<float>();
        return {r, g, b, get<float>()};
    }
    CornerRadii radii() {
        CornerRadii out;
        out.top_left = get<float>(); out.top_right = get<float>(); out.bottom_right = get<float>(); out.bottom_left = get<float>();
        out.smoothing = get<float>();
        return out;
    }
    Affine affine() {
        Affine m;
        m.a = get<float>(); m.b = get<float>(); m.c = get<float>(); m.d = get<float>(); m.e = get<float>(); m.f = get<float>();
        return m;
    }
    std::string_view text() {
        const std::size_t size = count();
        if (!need(size)) return {};
        held.texts.emplace_back(bytes.substr(at, size));
        at += size;
        return held.texts.back();
    }
    PathView path() {
        auto& verbs = held.verbs.emplace_back(count());
        for (PathVerb& verb : verbs) verb = get<PathVerb>();
        auto& points = held.points.emplace_back(count(8));
        for (Point& p : points) p = point();
        return {verbs, points};
    }
    Material material() {
        Material out;
        out.normal.id = get<std::uint32_t>(); out.light = point(); out.light_z = get<float>(); out.shadow = color();
        out.shadow_strength = get<float>(); out.bands = get<std::int32_t>(); out.softness = get<float>(); out.rim = color();
        out.rim_strength = get<float>(); out.rim_width = get<float>(); out.key = color();
        return out;
    }
    Paint paint() {
        Paint out;
        out.kind = get<Paint::Kind>();
        out.color = color();
        out.gradient.kind = get<GradientKind>(); out.gradient.start = point(); out.gradient.end = point();
        out.gradient.radius = get<float>(); out.gradient.spread = get<Spread>();
        auto& stops = held.stops.emplace_back(count(20));
        for (GradientStop& stop : stops) { stop.offset = get<float>(); stop.color = color(); }
        out.gradient.stops = stops;
        out.image.id = get<std::uint32_t>();
        out.image_transform = affine();
        out.sampling = get<Sampling>();
        if (get<bool>()) out.material = &held.materials.emplace_back(material());
        auto& mix = held.mixes.emplace_back(count(5));
        for (WeightedPaint& term : mix) {
            term.weight = get<float>();
            term.paint = paint();
        }
        out.mix = std::span<const WeightedPaint>(mix);
        out.mix_opacity = get<float>();
        return out;
    }
    StrokeStyle stroke() {
        StrokeStyle out;
        out.width = get<float>(); out.cap = get<LineCap>(); out.join = get<LineJoin>(); out.miter_limit = get<float>();
        auto& dashes = held.floats.emplace_back(count(4));
        for (float& dash : dashes) dash = get<float>();
        out.dashes = dashes;
        out.dash_offset = get<float>(); out.trim_start = get<float>(); out.trim_end = get<float>(); out.trim_offset = get<float>();
        return out;
    }
    const PartFrame* frame() {
        PartFrame out;
        out.owner = get<std::uint32_t>(); out.role = text(); out.id = text(); out.allocation = rect(); out.anchor = point();
        out.kind = get<PartKind>();
        auto& lineage = held.lineages.emplace_back(count(12));
        for (ReferenceWeight& weight : lineage) {
            weight.key = text();
            weight.weight = get<double>();
        }
        out.lineage = lineage;
        return &held.frames.emplace_back(out);
    }
};

void put_command(Writer& out, const Command& command) {
    out.put(static_cast<std::uint8_t>(command.index()));
    std::visit(
        [&](const auto& c) {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::same_as<T, FillRect>) { out.put(c.rect); out.put(c.paint); }
            else if constexpr (std::same_as<T, FillRoundRect>) { out.put(c.rect); out.put(c.radii); out.put(c.paint); }
            else if constexpr (std::same_as<T, StrokeRoundRect>) { out.put(c.rect); out.put(c.radii); out.put(c.width); out.put(c.paint); }
            else if constexpr (std::same_as<T, BoxShadow>) {
                out.put(c.rect); out.put(c.radii); out.put(c.offset); out.put(c.blur); out.put(c.spread); out.put(c.color); out.put(c.inset);
            }
            else if constexpr (std::same_as<T, FillPath>) { out.put(c.path); out.put(c.rule); out.put(c.paint); }
            else if constexpr (std::same_as<T, StrokePath>) { out.put(c.path); out.put(c.style); out.put(c.paint); }
            else if constexpr (std::same_as<T, DrawImage>) { out.put(c.image.id); out.put(c.source); out.put(c.dest); out.put(c.sampling); out.put(c.opacity); }
            else if constexpr (std::same_as<T, DrawGlyphs>) {
                out.put(c.font.id); out.put(c.size); out.put(c.origin);
                out.count(c.glyphs.size());
                for (const Glyph& glyph : c.glyphs) {
                    out.put(glyph.id); out.put(glyph.position); out.put(glyph.cluster); out.put(glyph.grapheme); out.put(glyph.word); out.put(glyph.line);
                }
                out.put(c.paint);
                out.count(c.styles.size());
                for (const GlyphStyle& style : c.styles) { out.put(style.transform); out.put(style.opacity); out.put(style.tint); }
            }
            else if constexpr (std::same_as<T, PushClipRect>) { out.put(c.rect); out.put(c.radii); }
            else if constexpr (std::same_as<T, PushClipPath>) { out.put(c.path); out.put(c.rule); }
            else if constexpr (std::same_as<T, PushTransform>) { out.put(c.transform); }
            else if constexpr (std::same_as<T, PushLayer>) {
                out.put(c.opacity); out.put(c.blend); out.put(c.effect.id);
                out.count(c.params.size());
                for (const float param : c.params) out.put(param);
                out.put(c.bounds);
                out.count(c.shapes.size());
                for (const EffectShape& shape : c.shapes) { out.put(shape.rect); out.put(shape.radii); }
                out.put(c.bounds_mode);
            }
            else if constexpr (std::same_as<T, ExternalTexture>) { out.put(c.texture); out.put(c.dest); out.put(c.opacity); out.put(c.sampling); }
            else if constexpr (std::same_as<T, BeginNode>) { out.put(c.node); }
            else if constexpr (std::same_as<T, NodeFrame>) {
                out.put(c.node); out.put(c.rect); out.put(c.opacity); out.put(c.placement); out.put(c.constraint); out.put(c.visual);
                out.put(c.sampled_placement_origin.has_value());
                if (c.sampled_placement_origin) out.put(*c.sampled_placement_origin);
            }
            else if constexpr (std::same_as<T, ReferenceBox>) { out.put(c.owner); out.put(c.key); out.put(c.allocation); }
            else if constexpr (std::same_as<T, BeginPart>) {
                out.put(c.frame != nullptr);
                if (c.frame) out.put(*c.frame);
            }
            // PopClip, PopTransform, PopLayer, EndNode, EndPart: the tag alone.
        },
        command);
}

// The command of tag `index`, read.
template <std::size_t Index = 0>
std::optional<Command> get_command(Reader& in, std::size_t index) {
    if constexpr (Index == std::variant_size_v<Command>) {
        return std::nullopt;
    } else {
        if (index != Index) return get_command<Index + 1>(in, index);
        using T = std::variant_alternative_t<Index, Command>;
        T c{};
        if constexpr (std::same_as<T, FillRect>) { c.rect = in.rect(); c.paint = in.paint(); }
        else if constexpr (std::same_as<T, FillRoundRect>) { c.rect = in.rect(); c.radii = in.radii(); c.paint = in.paint(); }
        else if constexpr (std::same_as<T, StrokeRoundRect>) { c.rect = in.rect(); c.radii = in.radii(); c.width = in.get<float>(); c.paint = in.paint(); }
        else if constexpr (std::same_as<T, BoxShadow>) {
            c.rect = in.rect(); c.radii = in.radii(); c.offset = in.point(); c.blur = in.get<float>(); c.spread = in.get<float>();
            c.color = in.color(); c.inset = in.get<bool>();
        }
        else if constexpr (std::same_as<T, FillPath>) { c.path = in.path(); c.rule = in.get<FillRule>(); c.paint = in.paint(); }
        else if constexpr (std::same_as<T, StrokePath>) { c.path = in.path(); c.style = in.stroke(); c.paint = in.paint(); }
        else if constexpr (std::same_as<T, DrawImage>) {
            c.image.id = in.get<std::uint32_t>(); c.source = in.rect(); c.dest = in.rect(); c.sampling = in.get<Sampling>(); c.opacity = in.get<float>();
        }
        else if constexpr (std::same_as<T, DrawGlyphs>) {
            c.font.id = in.get<std::uint32_t>(); c.size = in.get<float>(); c.origin = in.point();
            auto& glyphs = in.held.glyphs.emplace_back(in.count(28));
            for (Glyph& glyph : glyphs) {
                glyph.id = in.get<std::uint32_t>(); glyph.position = in.point(); glyph.cluster = in.get<std::uint32_t>();
                glyph.grapheme = in.get<std::uint32_t>(); glyph.word = in.get<std::uint32_t>(); glyph.line = in.get<std::uint32_t>();
            }
            c.glyphs = glyphs;
            c.paint = in.paint();
            auto& styles = in.held.styles.emplace_back(in.count(44));
            for (GlyphStyle& style : styles) { style.transform = in.affine(); style.opacity = in.get<float>(); style.tint = in.color(); }
            c.styles = styles;
        }
        else if constexpr (std::same_as<T, PushClipRect>) { c.rect = in.rect(); c.radii = in.radii(); }
        else if constexpr (std::same_as<T, PushClipPath>) { c.path = in.path(); c.rule = in.get<FillRule>(); }
        else if constexpr (std::same_as<T, PushTransform>) { c.transform = in.affine(); }
        else if constexpr (std::same_as<T, PushLayer>) {
            c.opacity = in.get<float>(); c.blend = in.get<BlendMode>(); c.effect.id = in.get<std::uint32_t>();
            auto& params = in.held.floats.emplace_back(in.count(4));
            for (float& param : params) param = in.get<float>();
            c.params = params;
            c.bounds = in.rect();
            auto& shapes = in.held.shapes.emplace_back(in.count(36));
            for (EffectShape& shape : shapes) { shape.rect = in.rect(); shape.radii = in.radii(); }
            c.shapes = shapes;
            c.bounds_mode = in.get<LayerBoundsMode>();
        }
        else if constexpr (std::same_as<T, ExternalTexture>) {
            c.texture = in.get<std::uint64_t>(); c.dest = in.rect(); c.opacity = in.get<float>(); c.sampling = in.get<Sampling>();
        }
        else if constexpr (std::same_as<T, BeginNode>) { c.node = in.get<std::uint32_t>(); }
        else if constexpr (std::same_as<T, NodeFrame>) {
            c.node = in.get<std::uint32_t>(); c.rect = in.rect(); c.opacity = in.get<float>(); c.placement = in.rect(); c.constraint = in.size();
            c.visual = in.affine();
            if (in.get<bool>()) c.sampled_placement_origin = in.point();
        }
        else if constexpr (std::same_as<T, ReferenceBox>) { c.owner = in.get<std::uint32_t>(); c.key = in.text(); c.allocation = in.rect(); }
        else if constexpr (std::same_as<T, BeginPart>) { c.frame = in.get<bool>() ? in.frame() : nullptr; }
        return Command(std::move(c));
    }
}

}  // namespace

std::string encode(const DisplayList& list) {
    Writer out;
    out.bytes.append(magic);
    out.put(version);
    out.count(list.size());
    for (const Command& command : list.commands()) put_command(out, command);
    return std::move(out.bytes);
}

std::expected<void, std::string> decode(std::string_view bytes, DisplayList& into) {
    if (!bytes.starts_with(magic)) return std::unexpected(std::string("not a display list (no `xrdl`)"));
    Held held;
    Reader in{bytes, magic.size(), false, held};
    if (const auto written = in.get<std::uint32_t>(); written != version)
        return std::unexpected(std::format("a display list of version {} (this reads {})", written, version));
    const std::size_t count = in.count();
    // Read whole first: a damaged list appends nothing.
    std::vector<Command> commands;
    commands.reserve(count);
    for (std::size_t index = 0; index < count && !in.failed; ++index) {
        const auto tag = in.get<std::uint8_t>();
        auto command = get_command(in, tag);
        if (!command) return std::unexpected(std::format("command {}: no kind {}", index, tag));
        if (in.failed) break;
        commands.push_back(std::move(*command));
    }
    if (in.failed) return std::unexpected(std::format("cut short at byte {} of {}", in.at, bytes.size()));
    if (in.at != bytes.size()) return std::unexpected(std::format("{} bytes left after the last command", bytes.size() - in.at));
    for (const Command& command : commands) into.append_command(command);
    return {};
}

}  // namespace xrender
