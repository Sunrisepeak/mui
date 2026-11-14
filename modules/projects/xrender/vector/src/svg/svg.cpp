module xrender.vector.svg;

import std;
import mlibs.geom;
import mlibs.base64;
import xrender.raster;
import xrender.vector;

namespace xrender::svg {

std::string_view describe(Why why) noexcept {
    switch (why) {
    case Why::effect: return "effect";
    case Why::backdrop_effect: return "backdrop-effect";
    case Why::paint: return "paint";
    case Why::blend: return "blend";
    case Why::inset_shadow: return "inset-shadow";
    case Why::trimmed_stroke: return "trimmed-stroke";
    case Why::colour_glyph: return "colour-glyph";
    }
    return "unknown";
}

namespace {

// Numbers as the shortest text that reads back as the same float: the same
// bytes on every platform.
std::string number(float value) { return std::format("{}", value == 0 ? 0.0f : value); }

std::string matrix(const Affine& transform) {
    return std::format("matrix({} {} {} {} {} {})", number(transform.a), number(transform.b), number(transform.c), number(transform.d),
                       number(transform.e), number(transform.f));
}

int byte_of(float channel) { return static_cast<int>(std::lround(std::clamp(channel, 0.0f, 1.0f) * 255)); }
std::string hex(const Color& color) { return std::format("#{:02x}{:02x}{:02x}", byte_of(color.r), byte_of(color.g), byte_of(color.b)); }

std::string path_data(PathView path) {
    std::string data;
    std::size_t at = 0;
    const auto point = [&] {
        const Point p = path.points[at++];
        return std::format("{} {}", number(p.x), number(p.y));
    };
    for (const PathVerb verb : path.verbs) {
        switch (verb) {
        case PathVerb::move: data += "M" + point(); break;
        case PathVerb::line: data += "L" + point(); break;
        case PathVerb::quad: data += "Q" + point(); data += " " + point(); break;
        case PathVerb::cubic:
            data += "C" + point();
            data += " " + point();
            data += " " + point();
            break;
        case PathVerb::close: data += "Z"; break;
        }
    }
    return data;
}

// A path's contours, each from its move.
std::vector<PathView> contours(PathView path) {
    std::vector<PathView> out;
    std::size_t verb_from = 0, point_from = 0, point_at = 0;
    for (std::size_t verb = 0; verb < path.verbs.size(); ++verb) {
        if (path.verbs[verb] == PathVerb::move && verb > verb_from) {
            out.push_back({path.verbs.subspan(verb_from, verb - verb_from), path.points.subspan(point_from, point_at - point_from)});
            verb_from = verb;
            point_from = point_at;
        }
        switch (path.verbs[verb]) {
        case PathVerb::move: case PathVerb::line: point_at += 1; break;
        case PathVerb::quad: point_at += 2; break;
        case PathVerb::cubic: point_at += 3; break;
        case PathVerb::close: break;
        }
    }
    if (verb_from < path.verbs.size()) out.push_back({path.verbs.subspan(verb_from), path.points.subspan(point_from, point_at - point_from)});
    return out;
}

std::string_view blend_name(BlendMode blend) noexcept {
    switch (blend) {
    case BlendMode::normal: return "normal";
    case BlendMode::multiply: return "multiply";
    case BlendMode::screen: return "screen";
    case BlendMode::overlay: return "overlay";
    case BlendMode::darken: return "darken";
    case BlendMode::lighten: return "lighten";
    case BlendMode::color_dodge: return "color-dodge";
    case BlendMode::color_burn: return "color-burn";
    case BlendMode::hard_light: return "hard-light";
    case BlendMode::soft_light: return "soft-light";
    case BlendMode::difference: return "difference";
    case BlendMode::exclusion: return "exclusion";
    case BlendMode::plus: return "plus-lighter";
    case BlendMode::erase: return {};
    }
    return {};
}

std::string escaped(std::string_view text) {
    std::string out;
    for (const char symbol : text) {
        switch (symbol) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        default: out += symbol;
        }
    }
    return out;
}

enum class Group : std::uint8_t { transform, clip, layer, node, part };

class Writer {
public:
    Writer(const DisplayList& list, const Resources& resources, const Options& options)
        : list_(list), resources_(resources), options_(options) {}

    std::expected<Picture, std::string> run() {
        const Rect view = options_.view;
        out_ = std::format(R"(<svg xmlns="http://www.w3.org/2000/svg" width="{}" height="{}" viewBox="{} {} {} {}">)", number(view.width),
                           number(view.height), number(view.x), number(view.y), number(view.width), number(view.height));
        out_ += "\n";
        if (options_.background)
            out_ += std::format(R"(<rect x="{}" y="{}" width="{}" height="{}" fill="{}"{}/>)", number(view.x), number(view.y), number(view.width),
                                number(view.height), hex(*options_.background), opacity_attribute("fill-opacity", options_.background->a)) +
                    "\n";
        const auto commands = list_.commands();
        for (std::size_t index = 0; index < commands.size(); ++index) {
            ++picture_.report.commands;
            if (auto next = command(commands, index); !next) return std::unexpected(next.error());
            else index = *next;
        }
        if (!open_.empty()) return std::unexpected(std::format("{} groups still open at the end of the list", open_.size()));
        out_ += "</svg>\n";
        picture_.text = std::move(out_);
        return std::move(picture_);
    }

private:
    const DisplayList& list_;
    const Resources&   resources_;
    const Options&     options_;
    Picture            picture_;
    std::string        out_;
    std::vector<Group> open_;
    std::vector<Affine> transforms_{Affine{}};  // the current one last
    std::size_t        next_id_ = 0;
    std::set<std::uint64_t> glyphs_defined_;
    std::set<std::uint32_t> images_defined_;

    std::string id(std::string_view kind) { return std::format("{}{}", kind, next_id_++); }
    std::optional<std::vector<std::uint8_t>> encode(std::span<const std::uint8_t> rgba, int width, int height) {
        if (options_.encode_png)
            if (auto png = options_.encode_png(rgba, width, height)) return std::move(*png);
        ++picture_.report.unencoded;
        return std::nullopt;
    }
    static std::string opacity_attribute(std::string_view name, float opacity) {
        return opacity < 1 ? std::format(R"( {}="{}")", name, number(std::max(opacity, 0.0f))) : std::string{};
    }

    void open(Group group, std::string attributes) {
        out_ += "<g" + attributes + ">\n";
        open_.push_back(group);
    }
    std::expected<void, std::string> close(Group group, std::size_t index) {
        if (open_.empty() || open_.back() != group) return std::unexpected(std::format("command {}: a pop that closes no matching push", index));
        open_.pop_back();
        out_ += "</g>\n";
        if (group == Group::transform) transforms_.pop_back();
        return {};
    }

    // The attributes that paint with `paint` (`property`: fill or stroke),
    // its gradient defined first; none when SVG has no such paint.
    std::optional<std::string> paint(const Paint& paint, std::string_view property) {
        if (paint.kind == Paint::Kind::solid)
            return std::format(R"( {}="{}"{})", property, hex(paint.color), opacity_attribute(std::format("{}-opacity", property), paint.color.a));
        if (paint.kind != Paint::Kind::gradient || paint.gradient.kind == GradientKind::conic || paint.gradient.stops.empty()) return std::nullopt;
        const Gradient& gradient = paint.gradient;
        const std::string name = id("p");
        const std::string_view spread = gradient.spread == Spread::repeat ? "repeat" : gradient.spread == Spread::reflect ? "reflect" : "pad";
        if (gradient.kind == GradientKind::linear)
            out_ += std::format(R"(<defs><linearGradient id="{}" gradientUnits="userSpaceOnUse" x1="{}" y1="{}" x2="{}" y2="{}" spreadMethod="{}">)", name,
                                number(gradient.start.x), number(gradient.start.y), number(gradient.end.x), number(gradient.end.y), spread);
        else
            out_ += std::format(R"(<defs><radialGradient id="{}" gradientUnits="userSpaceOnUse" cx="{}" cy="{}" r="{}" fx="{}" fy="{}" spreadMethod="{}">)",
                                name, number(gradient.start.x), number(gradient.start.y), number(gradient.radius), number(gradient.end.x),
                                number(gradient.end.y), spread);
        for (const GradientStop& stop : gradient.stops)
            out_ += std::format(R"(<stop offset="{}" stop-color="{}"{}/>)", number(std::clamp(stop.offset, 0.0f, 1.0f)), hex(stop.color),
                                opacity_attribute("stop-opacity", stop.color.a));
        out_ += gradient.kind == GradientKind::linear ? "</linearGradient></defs>\n" : "</radialGradient></defs>\n";
        return std::format(R"svg( {}="url(#{})")svg", property, name);
    }

    static std::string rounded(const Rect& rect, const CornerRadii& radii) {
        Path path;
        path.add_rounded_rect(rect, radii);
        return path_data(path.view());
    }
    void shape(const Rect& rect, const CornerRadii& radii, const std::string& painted) {
        ++picture_.report.shapes;
        if (radii.zero())
            out_ += std::format(R"(<rect x="{}" y="{}" width="{}" height="{}"{}/>)", number(rect.x), number(rect.y), number(rect.width),
                                number(rect.height), painted) + "\n";
        else
            out_ += std::format(R"(<path d="{}"{}/>)", rounded(rect, radii), painted) + "\n";
    }

    // Commands [from, to] drawn by the CPU reference at island_scale and kept
    // as a picture in their place. A backdrop effect is drawn over everything
    // before it, as it reads it, and cut to its layer.
    void island(std::span<const Command> commands, std::size_t from, std::size_t to, Why why, std::optional<Rect> cut = {},
                std::string_view effect_name = {}) {
        const Rect view = options_.view;
        const float scale = options_.island_scale;
        const Affine& current = transforms_.back();
        const auto back = current.inverse();
        if (!back) return;  // drawn flat: nothing shows
        const Affine device = Affine::scale(scale, scale) * Affine::translate(-view.x, -view.y);
        DisplayList alone;
        if (why == Why::backdrop_effect) {
            // Everything up to it, as the list draws it, and its open groups closed.
            alone.push_transform(device);
            for (std::size_t index = 0; index <= to; ++index) alone.append_command(commands[index]);
            for (auto group = open_.rbegin(); group != open_.rend(); ++group) {
                switch (*group) {
                case Group::transform: alone.pop_transform(); break;
                case Group::clip: alone.pop_clip(); break;
                case Group::layer: alone.pop_layer(); break;
                case Group::node: alone.end_node(); break;
                case Group::part: alone.end_part(); break;
                }
            }
            alone.pop_transform();
        } else {
            alone.push_transform(device * current);
            for (std::size_t index = from; index <= to; ++index) alone.append_command(commands[index]);
            alone.pop_transform();
        }
        const int width = std::max(1, static_cast<int>(std::ceil(view.width * scale)));
        const int height = std::max(1, static_cast<int>(std::ceil(view.height * scale)));
        xrender::raster::Surface surface(width, height);
        CpuRenderer renderer;
        (void)renderer.render(alone, surface, resources_, {.scale = 1, .effect_scale = scale});
        // What it covers: the layer's own rectangle when cut, else every pixel drawn.
        int left = width, top = height, right = 0, bottom = 0;
        if (cut) {
            const Rect device_cut = (device * current).map(*cut);
            left = std::clamp(static_cast<int>(std::floor(device_cut.x)), 0, width);
            top = std::clamp(static_cast<int>(std::floor(device_cut.y)), 0, height);
            right = std::clamp(static_cast<int>(std::ceil(device_cut.right())), 0, width);
            bottom = std::clamp(static_cast<int>(std::ceil(device_cut.bottom())), 0, height);
        } else {
            for (int y = 0; y < height; ++y)
                for (int x = 0; x < width; ++x)
                    if (surface.at(x, y).a) {
                        left = std::min(left, x);
                        right = std::max(right, x + 1);
                        top = std::min(top, y);
                        bottom = std::max(bottom, y + 1);
                    }
        }
        if (right <= left || bottom <= top) return;
        const int cut_width = right - left, cut_height = bottom - top;
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(cut_width) * cut_height * 4);
        for (int y = 0; y < cut_height; ++y)
            std::memcpy(pixels.data() + static_cast<std::size_t>(y) * cut_width * 4, surface.row(top + y) + left, static_cast<std::size_t>(cut_width) * 4);
        const auto png = encode(pixels, cut_width, cut_height);
        if (!png) return;
        const Rect bounds{view.x + static_cast<float>(left) / scale, view.y + static_cast<float>(top) / scale, static_cast<float>(cut_width) / scale,
                          static_cast<float>(cut_height) / scale};
        picture_.report.islands.push_back({from, why, bounds, std::string(effect_name)});
        out_ += std::format(R"(<image data-island="{}"{} x="{}" y="{}" width="{}" height="{}" preserveAspectRatio="none"{} href="data:image/png;base64,{}"/>)",
                            describe(why), effect_name.empty() ? std::string{} : std::format(R"( data-effect="{}")", escaped(effect_name)), number(bounds.x), number(bounds.y), number(bounds.width), number(bounds.height),
                            back->identity() ? std::string{} : std::format(R"( transform="{}")", matrix(*back)), mlibs::base64::encode(*png)) +
                "\n";
    }

    // The index of the push's matching pop.
    static std::size_t matching_pop(std::span<const Command> commands, std::size_t push) {
        int depth = 0;
        for (std::size_t index = push; index < commands.size(); ++index) {
            if (std::holds_alternative<PushLayer>(commands[index])) ++depth;
            else if (std::holds_alternative<PopLayer>(commands[index]) && --depth == 0) return index;
        }
        return commands.size() - 1;
    }

    void image(const DrawImage& draw) {
        const xrender::raster::Surface* surface = resources_.images ? resources_.images->get(draw.image) : nullptr;
        if (!surface) {
            ++picture_.report.missing_images;
            return;
        }
        ++picture_.report.images;
        if (images_defined_.insert(draw.image.id).second) {
            const auto png = encode(surface->bytes(), surface->width(), surface->height());
            if (!png) return;
            out_ += std::format(R"(<defs><image id="i{}" width="{}" height="{}" href="data:image/png;base64,{}"/></defs>)", draw.image.id,
                                surface->width(), surface->height(), mlibs::base64::encode(*png)) +
                    "\n";
        }
        out_ += std::format(R"(<svg x="{}" y="{}" width="{}" height="{}" viewBox="{} {} {} {}" preserveAspectRatio="none" overflow="hidden"><use href="#i{}"{}{}/></svg>)",
                            number(draw.dest.x), number(draw.dest.y), number(draw.dest.width), number(draw.dest.height), number(draw.source.x),
                            number(draw.source.y), number(draw.source.width), number(draw.source.height), draw.image.id,
                            opacity_attribute("opacity", draw.opacity),
                            draw.sampling == Sampling::nearest ? R"( style="image-rendering:pixelated")" : "") +
                "\n";
    }

    // Glyphs as their outlines, each (font, glyph) defined once in ems and
    // placed where the renderers place it.
    bool glyphs(const DrawGlyphs& run) {
        if (!resources_.glyphs) {
            picture_.report.missing_glyphs += run.glyphs.size();
            return true;
        }
        if (run.paint.kind != Paint::Kind::solid) return false;
        std::vector<xrender::raster::PathData> outlines;
        for (const Glyph& glyph : run.glyphs) {
            outlines.push_back(resources_.glyphs->outline(run.font, glyph.id));
            if (outlines.back().verbs.empty() && resources_.glyphs->mask(run.font, glyph.id, run.size, {0, 0}).color) return false;
        }
        out_ += "<g" + *paint(run.paint, "fill") + ">\n";
        for (std::size_t index = 0; index < run.glyphs.size(); ++index) {
            const Glyph& glyph = run.glyphs[index];
            if (outlines[index].verbs.empty()) continue;  // a blank glyph (a space)
            const std::uint64_t key = (std::uint64_t{run.font.id} << 32) | glyph.id;
            if (glyphs_defined_.insert(key).second)
                out_ += std::format(R"(<defs><path id="g{}_{}" d="{}"/></defs>)", run.font.id, glyph.id, path_data(outlines[index]));
            Point pen{run.origin.x + glyph.position.x, run.origin.y + glyph.position.y};
            const GlyphStyle* style = run.styles.empty() || run.styles[index].plain() ? nullptr : &run.styles[index];
            Affine place = Affine::translate(pen.x, pen.y) * Affine::scale(run.size, run.size);
            std::string own;
            if (style) {
                if (style->translation_only()) place = Affine::translate(pen.x + style->transform.e, pen.y + style->transform.f) * Affine::scale(run.size, run.size);
                else place = Affine::translate(pen.x, pen.y) * style->transform * Affine::scale(run.size, run.size);
                // Its own opacity and tint over the run's colour, as the renderers mix them.
                const float opacity = std::clamp(style->opacity, 0.0f, 1.0f);
                const float tint = std::clamp(style->tint.a, 0.0f, 1.0f);
                const Color base = run.paint.color;
                const Color mixed{base.r + (style->tint.r - base.r) * tint, base.g + (style->tint.g - base.g) * tint, base.b + (style->tint.b - base.b) * tint,
                                  base.a * opacity};
                own = std::format(R"( fill="{}"{})", hex(mixed), opacity_attribute("fill-opacity", mixed.a));
            }
            out_ += std::format(R"(<use href="#g{}_{}" transform="{}"{}/>)", run.font.id, glyph.id, matrix(place), own) + "\n";
            ++picture_.report.glyphs;
        }
        out_ += "</g>\n";
        return true;
    }

    // Writes commands[index] (and, for a layer drawn as pixels, all of it);
    // returns the last index written.
    std::expected<std::size_t, std::string> command(std::span<const Command> commands, std::size_t index) {
        const Command& current = commands[index];
        std::expected<std::size_t, std::string> done = index;
        std::visit(
            [&](const auto& c) {
                using T = std::decay_t<decltype(c)>;
                if constexpr (std::same_as<T, FillRect>) {
                    if (const auto painted = paint(c.paint, "fill")) shape(c.rect, {}, *painted);
                    else island(commands, index, index, Why::paint);
                } else if constexpr (std::same_as<T, FillRoundRect>) {
                    if (const auto painted = paint(c.paint, "fill")) shape(c.rect, c.radii, *painted);
                    else island(commands, index, index, Why::paint);
                } else if constexpr (std::same_as<T, StrokeRoundRect>) {
                    if (!(c.width > 0)) return;
                    const auto painted = paint(c.paint, "fill");
                    if (!painted) return island(commands, index, index, Why::paint);
                    const Ring ring = stroke_ring(c.rect, c.radii, c.width);
                    std::string data = rounded(ring.outer, ring.outer_radii);
                    if (ring.hollow) data += rounded(ring.inner, ring.inner_radii);
                    ++picture_.report.shapes;
                    out_ += std::format(R"(<path d="{}" fill-rule="evenodd"{}/>)", data, *painted) + "\n";
                } else if constexpr (std::same_as<T, BoxShadow>) {
                    if (c.inset) return island(commands, index, index, Why::inset_shadow);
                    // CSS's outer shadow: the box grown by the spread, moved, blurred
                    // (σ = blur / 2), shown only outside the box.
                    const Rect shadow{c.rect.x - c.spread + c.offset.x, c.rect.y - c.spread + c.offset.y, c.rect.width + 2 * c.spread,
                                      c.rect.height + 2 * c.spread};
                    const auto grow = [&](float radius) { return radius > 0 ? std::max(0.0f, radius + c.spread) : 0.0f; };
                    const CornerRadii radii{grow(c.radii.top_left), grow(c.radii.top_right), grow(c.radii.bottom_right), grow(c.radii.bottom_left)};
                    const float sigma = c.blur / 2;
                    const float reach = 3 * sigma + 1;
                    const float left = std::min(shadow.x, c.rect.x) - reach, top = std::min(shadow.y, c.rect.y) - reach;
                    const float right = std::max(shadow.right(), c.rect.right()) + reach, bottom = std::max(shadow.bottom(), c.rect.bottom()) + reach;
                    const std::string mask = id("m");
                    out_ += std::format(R"(<defs><mask id="{}" maskUnits="userSpaceOnUse" x="{}" y="{}" width="{}" height="{}"><rect x="{}" y="{}" width="{}" height="{}" fill="#fff"/><path d="{}" fill="#000"/></mask>)",
                                        mask, number(left), number(top), number(right - left), number(bottom - top), number(left), number(top),
                                        number(right - left), number(bottom - top), rounded(c.rect, c.radii));
                    std::string filter;
                    if (sigma > 0) {
                        const std::string name = id("f");
                        out_ += std::format(R"(<filter id="{}" filterUnits="userSpaceOnUse" x="{}" y="{}" width="{}" height="{}"><feGaussianBlur stdDeviation="{}"/></filter>)",
                                            name, number(left), number(top), number(right - left), number(bottom - top), number(sigma));
                        filter = std::format(R"svg( filter="url(#{})")svg", name);
                    }
                    out_ += "</defs>\n";
                    out_ += std::format(R"svg(<g mask="url(#{})"><path d="{}" fill="{}"{}{}/></g>)svg", mask, rounded(shadow, radii), hex(c.color),
                                        opacity_attribute("fill-opacity", c.color.a), filter) + "\n";
                    ++picture_.report.shapes;
                } else if constexpr (std::same_as<T, FillPath>) {
                    const auto painted = paint(c.paint, "fill");
                    if (!painted) return island(commands, index, index, Why::paint);
                    ++picture_.report.shapes;
                    out_ += std::format(R"(<path d="{}"{}{}/>)", path_data(c.path), c.rule == FillRule::even_odd ? R"( fill-rule="evenodd")" : "", *painted) +
                            "\n";
                } else if constexpr (std::same_as<T, StrokePath>) {
                    const StrokeStyle& style = c.style;
                    const bool trimmed = style.trim_start != 0 || style.trim_end != 1 || style.trim_offset != 0;
                    if (trimmed && !style.dashes.empty()) return island(commands, index, index, Why::trimmed_stroke);
                    if (!(style.width > 0)) return;
                    const auto painted = paint(c.paint, "stroke");
                    if (!painted) return island(commands, index, index, Why::paint);
                    std::string attributes = std::format(R"( fill="none" stroke-width="{}")", number(style.width));
                    if (style.cap != LineCap::butt) attributes += style.cap == LineCap::round ? R"( stroke-linecap="round")" : R"( stroke-linecap="square")";
                    if (style.join != LineJoin::miter) attributes += style.join == LineJoin::round ? R"( stroke-linejoin="round")" : R"( stroke-linejoin="bevel")";
                    else if (style.miter_limit != 4) attributes += std::format(R"( stroke-miterlimit="{}")", number(style.miter_limit));
                    if (!style.dashes.empty()) {
                        std::string dashes;
                        for (const float dash : style.dashes) dashes += (dashes.empty() ? "" : " ") + number(dash);
                        attributes += std::format(R"( stroke-dasharray="{}")", dashes);
                        if (style.dash_offset != 0) attributes += std::format(R"( stroke-dashoffset="{}")", number(style.dash_offset));
                    }
                    ++picture_.report.shapes;
                    if (trimmed) {
                        // The renderers' trim, each contour by fractions of its own
                        // length (measured on the flattening the stroker trims): a dash
                        // as long as the part drawn, its pattern one contour long, so a
                        // start moved past the end wraps round.
                        const auto finite_or = [](float value, float fallback) { return std::isfinite(value) ? value : fallback; };
                        float first = std::clamp(finite_or(style.trim_start, 0), 0.0f, 1.0f), last = std::clamp(finite_or(style.trim_end, 1), 0.0f, 1.0f);
                        if (first > last) std::swap(first, last);
                        if (last - first <= 0) return;
                        double from = static_cast<double>(first) + finite_or(style.trim_offset, 0);
                        from -= std::floor(from);
                        const float scale = transforms_.back().max_scale();
                        for (const PathView contour : contours(c.path)) {
                            mlibs::geom::PathPolylines flat;
                            mlibs::geom::flatten_path(contour, Affine{}, flat, (1.0f / 32.0f) / (scale > 0 ? scale : 1.0f));
                            double length = 0;
                            for (std::size_t line = 0; line < flat.count(); ++line) {
                                const auto points = flat.polygon(line);
                                const std::size_t segments = flat.closed[line] ? points.size() : points.size() - 1;
                                for (std::size_t at = 0; at < segments && points.size() > 1; ++at) {
                                    const Point head = points[at], tail = points[(at + 1) % points.size()];
                                    const double across = static_cast<double>(tail.x - head.x), down = static_cast<double>(tail.y - head.y);
                                    length += std::sqrt(across * across + down * down);  // sqrt is IEEE's everywhere; hypot is each libm's
                                }
                            }
                            if (!(length > 0)) continue;
                            const std::string dash =
                                last - first >= 1 ? std::string{}
                                                  : std::format(R"( stroke-dasharray="{} {}" stroke-dashoffset="{}")", number(static_cast<float>((last - first) * length)),
                                                                number(static_cast<float>((1 - (last - first)) * length)), number(static_cast<float>(-from * length)));
                            out_ += std::format(R"(<path d="{}"{}{}{}/>)", path_data(contour), attributes, dash, *painted) + "\n";
                        }
                        return;
                    }
                    out_ += std::format(R"(<path d="{}"{}{}/>)", path_data(c.path), attributes, *painted) + "\n";
                } else if constexpr (std::same_as<T, DrawImage>) {
                    image(c);
                } else if constexpr (std::same_as<T, DrawGlyphs>) {
                    if (!glyphs(c)) island(commands, index, index, c.paint.kind == Paint::Kind::solid ? Why::colour_glyph : Why::paint);
                } else if constexpr (std::same_as<T, PushClipRect>) {
                    const std::string name = id("c");
                    out_ += std::format(R"(<clipPath id="{}">)", name);
                    if (c.radii.zero())
                        out_ += std::format(R"(<rect x="{}" y="{}" width="{}" height="{}"/>)", number(c.rect.x), number(c.rect.y), number(c.rect.width),
                                            number(c.rect.height));
                    else
                        out_ += std::format(R"(<path d="{}"/>)", rounded(c.rect, c.radii));
                    out_ += "</clipPath>\n";
                    open(Group::clip, std::format(R"svg( clip-path="url(#{})")svg", name));
                } else if constexpr (std::same_as<T, PushClipPath>) {
                    const std::string name = id("c");
                    out_ += std::format(R"(<clipPath id="{}"><path d="{}"{}/></clipPath>)", name, path_data(c.path),
                                        c.rule == FillRule::even_odd ? R"( clip-rule="evenodd")" : "") +
                            "\n";
                    open(Group::clip, std::format(R"svg( clip-path="url(#{})")svg", name));
                } else if constexpr (std::same_as<T, PopClip>) {
                    if (auto closed = close(Group::clip, index); !closed) done = std::unexpected(closed.error());
                } else if constexpr (std::same_as<T, PushTransform>) {
                    transforms_.push_back(transforms_.back() * c.transform);
                    open(Group::transform, std::format(R"( transform="{}")", matrix(c.transform)));
                } else if constexpr (std::same_as<T, PopTransform>) {
                    if (auto closed = close(Group::transform, index); !closed) done = std::unexpected(closed.error());
                } else if constexpr (std::same_as<T, PushLayer>) {
                    const std::string_view blend = blend_name(c.blend);
                    const EffectInfo* effect = c.effect.id != 0 ? effects().info(c.effect) : nullptr;
                    // The opacity effect scales the composed layer once: SVG's group opacity exactly.
                    float opacity = c.opacity;
                    if (effect && effect->name == "opacity" && !c.params.empty()) opacity *= std::clamp(c.params[0], 0.0f, 1.0f);
                    else if (c.effect.id != 0 || blend.empty()) {
                        const std::size_t pop = matching_pop(commands, index);
                        const Why why = c.effect.id == 0 ? Why::blend : effect && effect->backdrop ? Why::backdrop_effect : Why::effect;
                        island(commands, index, pop, why, why == Why::backdrop_effect && c.bounds.width > 0 ? std::optional<Rect>(c.bounds) : std::nullopt,
                               effect ? effect->name : std::string_view{});
                        done = pop;
                        return;
                    }
                    std::string attributes = opacity_attribute("opacity", opacity);
                    if (c.blend != BlendMode::normal) attributes += std::format(R"( style="mix-blend-mode:{}")", blend);
                    open(Group::layer, attributes);
                } else if constexpr (std::same_as<T, PopLayer>) {
                    if (auto closed = close(Group::layer, index); !closed) done = std::unexpected(closed.error());
                } else if constexpr (std::same_as<T, ExternalTexture>) {
                    ++picture_.report.external;
                    out_ += std::format(R"(<rect data-external="{}" x="{}" y="{}" width="{}" height="{}" fill="none"/>)", c.texture,
                                        number(c.dest.x), number(c.dest.y), number(c.dest.width), number(c.dest.height)) +
                            "\n";
                } else if constexpr (std::same_as<T, BeginNode>) {
                    const std::string name = options_.node_name ? options_.node_name(c.node) : std::string{};
                    open(Group::node, std::format(R"( data-node="{}")", escaped(name.empty() ? std::to_string(c.node) : name)));
                } else if constexpr (std::same_as<T, EndNode>) {
                    if (auto closed = close(Group::node, index); !closed) done = std::unexpected(closed.error());
                } else if constexpr (std::same_as<T, BeginPart>) {
                    std::string attributes;
                    if (c.frame) {
                        attributes = std::format(R"( data-part="{}")", escaped(c.frame->role));
                        if (!c.frame->id.empty()) attributes += std::format(R"( data-part-id="{}")", escaped(c.frame->id));
                    }
                    open(Group::part, attributes);
                } else if constexpr (std::same_as<T, EndPart>) {
                    if (auto closed = close(Group::part, index); !closed) done = std::unexpected(closed.error());
                }
                // NodeFrame, ReferenceBox: layout metadata, nothing drawn.
            },
            current);
        return done;
    }
};

}  // namespace

std::expected<Picture, std::string> write(const DisplayList& list, const Resources& resources, const Options& options) {
    if (!(options.view.width > 0 && options.view.height > 0)) return std::unexpected("the picture needs a size (Options::view)");
    if (!(options.island_scale > 0)) return std::unexpected("island_scale is above zero");
    return Writer(list, resources, options).run();
}

}  // namespace xrender::svg
